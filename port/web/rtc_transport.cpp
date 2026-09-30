/* Native DTLS/SCTP transport. Game input stays behind the socket abstraction. */
#include "rtc_transport.h"
#include "frames.h"
#include <rtc/rtc.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

static constexpr size_t QueueLimit = 1024 * 1024, ControlReserve = 4096,
                        UnreliableHighWater = 64 * 1024;
static constexpr const char *Reliable = "halo-reliable-v1", *Unreliable = "halo-unreliable-v1";
struct hw_rtc {
    hw_net *net = nullptr;
    uint32_t peer = 0;
    int pc = -1, reliable = -1, unreliable = -1;
    hw_signal signal = nullptr;
    void *context = nullptr;
    std::mutex lock;
    std::condition_variable changed;
    std::thread worker;
    bool stopping = false, failed = false, failureClosed = false, wasReady = false,
         remoteDescription = false;
    size_t outgoingBytes = 0, incomingBytes = 0;
    std::deque<std::vector<unsigned char>> outgoing, incoming;
    std::vector<std::pair<std::string, std::string>> candidates;
};
static void event(hw_rtc *r, const char *type, const char *value = "", const char *detail = "") {
    if (r && r->signal)
        r->signal(r->context, type, value, detail);
}
static void failure(hw_rtc *r, const char *reason) {
    if (!r)
        return;
    bool first;
    {
        std::lock_guard<std::mutex> guard(r->lock);
        if (r->stopping)
            return;
        first = !r->failed;
        r->failed = true;
        r->changed.notify_all();
    }
    hw_net_remove_peer(r->net, r->peer);
    if (first)
        event(r, "error", reason);
}
static void sync_state(hw_rtc *r) {
    int reliable, unreliable;
    bool failed, writable, report = false;
    {
        std::lock_guard<std::mutex> guard(r->lock);
        reliable = r->reliable;
        unreliable = r->unreliable;
        failed = r->failed || r->stopping;
        writable = r->outgoingBytes < QueueLimit - (HWF_HEADER + HWF_MAX_PAYLOAD);
    }
    bool a = !failed && reliable >= 0 && rtcIsOpen(reliable),
         b = !failed && unreliable >= 0 && rtcIsOpen(unreliable);
    hw_net_peer_state(r->net, r->peer, a, b, writable);
    {
        std::lock_guard<std::mutex> guard(r->lock);
        if (a && b && !r->wasReady) {
            r->wasReady = true;
            report = true;
        }
    }
    if (report)
        event(r, "ready");
}
static int emit(void *context, uint32_t peer, int reliable, const void *data, size_t size) {
    auto *r = static_cast<hw_rtc *>(context);
    if (peer != r->peer)
        return -1;
    int channel;
    {
        std::unique_lock<std::mutex> guard(r->lock);
        if (r->failed || r->stopping)
            return -1;
        channel = reliable ? r->reliable : r->unreliable;
        if (reliable) {
            hw_frame frame;
            if (hw_frame_decode(data, size, &frame))
                return -1;
            size_t limit = frame.type == HWF_DATA ? QueueLimit : QueueLimit + ControlReserve;
            if (r->outgoingBytes + size > limit) {
                if (frame.type == HWF_DATA)
                    return 0;
                guard.unlock();
                failure(r, "Reliable control queue overflow");
                return -1;
            }
            auto *bytes = static_cast<const unsigned char *>(data);
            r->outgoing.emplace_back(bytes, bytes + size);
            r->outgoingBytes += size;
            r->changed.notify_all();
            return 1;
        }
    }
    if (channel < 0 || !rtcIsOpen(channel) ||
        rtcGetBufferedAmount(channel) > static_cast<int>(UnreliableHighWater))
        return 0;
    return rtcSendMessage(channel, static_cast<const char *>(data), static_cast<int>(size)) >= 0
               ? 1
               : 0;
}
static void RTC_API description(int, const char *sdp, const char *type, void *pointer) {
    event(static_cast<hw_rtc *>(pointer), "description", sdp, type);
}
static void RTC_API candidate(int, const char *value, const char *mid, void *pointer) {
    event(static_cast<hw_rtc *>(pointer), "candidate", value, mid);
}
static void RTC_API state(int, rtcState state, void *pointer) {
    auto *r = static_cast<hw_rtc *>(pointer);
    if (!r)
        return;
    if (state == RTC_FAILED || state == RTC_DISCONNECTED || state == RTC_CLOSED) {
        failure(r, "WebRTC peer disconnected");
        event(r, "disconnected");
    }
}
static void RTC_API opened(int, void *pointer) {
    auto *r = static_cast<hw_rtc *>(pointer);
    if (r) {
        sync_state(r);
        r->changed.notify_all();
    }
}
static void RTC_API closed(int, void *pointer) {
    failure(static_cast<hw_rtc *>(pointer), "DataChannel closed");
}
static void RTC_API error(int, const char *reason, void *pointer) {
    failure(static_cast<hw_rtc *>(pointer), reason);
}
static void RTC_API low(int, void *pointer) {
    auto *r = static_cast<hw_rtc *>(pointer);
    if (r)
        r->changed.notify_all();
}
static void RTC_API message(int channel, const char *data, int size, void *pointer) {
    auto *r = static_cast<hw_rtc *>(pointer);
    if (!r)
        return;
    hw_frame frame;
    if (size < 0 || hw_frame_decode(data, static_cast<size_t>(size), &frame)) {
        failure(r, "Malformed binary Halo frame");
        return;
    }
    if (std::getenv("HALO_WEB_RTC_TRACE")) {
        auto value = std::to_string(frame.type) + ":" + std::to_string(frame.stream) + ":" +
                     std::to_string(frame.size);
        event(r, "frame-received", value.c_str());
    }
    int reliable;
    {
        std::lock_guard<std::mutex> guard(r->lock);
        reliable = r->reliable;
    }
    if (channel == reliable) {
        if (frame.type == HWF_DATAGRAM) {
            failure(r, "Datagram on reliable channel");
            return;
        }
        bool overflow = false;
        {
            std::lock_guard<std::mutex> guard(r->lock);
            if (r->incomingBytes + static_cast<size_t>(size) > QueueLimit)
                overflow = true;
            else {
                r->incoming.emplace_back(data, data + size);
                r->incomingBytes += static_cast<size_t>(size);
                r->changed.notify_all();
            }
        }
        if (overflow)
            failure(r, "Reliable receive queue overflow");
    } else if (frame.type != HWF_DATAGRAM ||
               hw_net_receive(r->net, r->peer, data, static_cast<size_t>(size)) < 0)
        failure(r, "Malformed datagram frame");
}
static void configure(hw_rtc *r, int channel, bool reliable) {
    rtcReliability policy = {};
    if (rtcGetDataChannelReliability(channel, &policy) < 0 ||
        (reliable ? (policy.unordered || policy.unreliable)
                  : (!policy.unordered || !policy.unreliable || policy.maxRetransmits ||
                     policy.maxPacketLifeTime))) {
        rtcDeleteDataChannel(channel);
        failure(r, "Incompatible DataChannel reliability");
        return;
    }
    bool duplicate = false, stopping = false;
    {
        std::lock_guard<std::mutex> guard(r->lock);
        stopping = r->stopping;
        int &slot = reliable ? r->reliable : r->unreliable;
        if (slot >= 0)
            duplicate = true;
        else if (!stopping)
            slot = channel;
    }
    if (stopping) {
        rtcDeleteDataChannel(channel);
        return;
    }
    if (duplicate) {
        rtcDeleteDataChannel(channel);
        failure(r, "Duplicate Halo DataChannel");
        return;
    }
    rtcSetUserPointer(channel, r);
    rtcSetOpenCallback(channel, opened);
    rtcSetClosedCallback(channel, closed);
    rtcSetErrorCallback(channel, error);
    rtcSetMessageCallback(channel, message);
    rtcSetBufferedAmountLowThreshold(channel, 32768);
    rtcSetBufferedAmountLowCallback(channel, low);
    sync_state(r);
}
static void RTC_API data_channel(int, int channel, void *pointer) {
    auto *r = static_cast<hw_rtc *>(pointer);
    if (!r) {
        rtcDeleteDataChannel(channel);
        return;
    }
    char label[64] = {};
    if (rtcGetDataChannelLabel(channel, label, sizeof(label)) < 0) {
        rtcDeleteDataChannel(channel);
        failure(r, "Invalid DataChannel label");
        return;
    }
    if (!strcmp(label, Reliable))
        configure(r, channel, true);
    else if (!strcmp(label, Unreliable))
        configure(r, channel, false);
    else {
        rtcDeleteDataChannel(channel);
        failure(r, "Unexpected DataChannel label");
    }
}
static void pump(hw_rtc *r) {
    while (true) {
        int channel;
        std::vector<unsigned char> outgoing, incoming;
        {
            std::unique_lock<std::mutex> guard(r->lock);
            r->changed.wait_for(guard, std::chrono::milliseconds(2));
            if (r->stopping)
                return;
            if (r->failed) {
                bool close = !r->failureClosed;
                r->failureClosed = true;
                guard.unlock();
                if (close)
                    rtcClosePeerConnection(r->pc);
                continue;
            }
            channel = r->reliable;
            if (!r->outgoing.empty())
                outgoing = r->outgoing.front();
            if (!r->incoming.empty())
                incoming = r->incoming.front();
        }
        if (!outgoing.empty() && channel >= 0 && rtcIsOpen(channel) &&
            rtcGetBufferedAmount(channel) < static_cast<int>(QueueLimit)) {
            if (rtcSendMessage(channel, reinterpret_cast<const char *>(outgoing.data()),
                               static_cast<int>(outgoing.size())) >= 0) {
                std::lock_guard<std::mutex> guard(r->lock);
                r->outgoingBytes -= outgoing.size();
                r->outgoing.pop_front();
            }
        }
        if (!incoming.empty()) {
            int result = hw_net_receive(r->net, r->peer, incoming.data(), incoming.size());
            if (std::getenv("HALO_WEB_RTC_TRACE")) {
                auto value = std::to_string(result);
                event(r, "frame-delivered", value.c_str());
            }
            if (result < 0)
                failure(r, "Malformed reliable frame");
            else if (result > 0) {
                std::lock_guard<std::mutex> guard(r->lock);
                r->incomingBytes -= incoming.size();
                r->incoming.pop_front();
            }
        }
        sync_state(r);
    }
}
extern "C" struct hw_rtc *hw_rtc_create(const unsigned char local[6], const unsigned char remote[6],
                                        int initiator, hw_signal signal, void *context) {
    auto *r = new hw_rtc;
    r->signal = signal;
    r->context = context;
    r->net = hw_net_create(local, emit, r);
    if (!r->net) {
        delete r;
        return nullptr;
    }
    r->peer = hw_net_add_peer(r->net, remote);
    if (!r->peer) {
        hw_net_destroy(r->net);
        delete r;
        return nullptr;
    }
    rtcConfiguration config = {};
    config.disableAutoNegotiation = true;
    config.maxMessageSize = HWF_HEADER + HWF_MAX_PAYLOAD;
    r->pc = rtcCreatePeerConnection(&config);
    if (r->pc < 0) {
        hw_net_destroy(r->net);
        delete r;
        return nullptr;
    }
    rtcSetUserPointer(r->pc, r);
    rtcSetLocalDescriptionCallback(r->pc, description);
    rtcSetLocalCandidateCallback(r->pc, candidate);
    rtcSetStateChangeCallback(r->pc, state);
    rtcSetDataChannelCallback(r->pc, data_channel);
    r->worker = std::thread(pump, r);
    if (initiator) {
        rtcDataChannelInit reliable = {}, unreliable = {};
        unreliable.reliability.unordered = true;
        unreliable.reliability.unreliable = true;
        int a = rtcCreateDataChannelEx(r->pc, Reliable, &reliable),
            b = rtcCreateDataChannelEx(r->pc, Unreliable, &unreliable);
        if (a < 0 || b < 0) {
            hw_rtc_destroy(r);
            return nullptr;
        }
        configure(r, a, true);
        configure(r, b, false);
        if (rtcSetLocalDescription(r->pc, "offer") < 0) {
            hw_rtc_destroy(r);
            return nullptr;
        }
    }
    return r;
}
extern "C" hw_net *hw_rtc_net(hw_rtc *r) { return r ? r->net : nullptr; }
extern "C" uint32_t hw_rtc_peer(hw_rtc *r) { return r ? r->peer : 0; }
extern "C" int hw_rtc_description(hw_rtc *r, const char *sdp, const char *type) {
    if (!r || !sdp || !type || (strcmp(type, "offer") && strcmp(type, "answer")))
        return -1;
    int result = rtcSetRemoteDescription(r->pc, sdp, type);
    if (result < 0)
        return result;
    std::vector<std::pair<std::string, std::string>> pending;
    {
        std::lock_guard<std::mutex> guard(r->lock);
        r->remoteDescription = true;
        pending.swap(r->candidates);
    }
    for (auto &value : pending)
        if (rtcAddRemoteCandidate(r->pc, value.first.c_str(), value.second.c_str()) < 0)
            return -1;
    return !strcmp(type, "offer") ? rtcSetLocalDescription(r->pc, "answer") : 0;
}
extern "C" int hw_rtc_candidate(hw_rtc *r, const char *value, const char *mid) {
    if (!r || !value || !mid)
        return -1;
    {
        std::lock_guard<std::mutex> guard(r->lock);
        if (!r->remoteDescription) {
            if (r->candidates.size() >= 128)
                return -1;
            r->candidates.emplace_back(value, mid);
            return 0;
        }
    }
    return rtcAddRemoteCandidate(r->pc, value, mid);
}
extern "C" void hw_rtc_destroy(hw_rtc *r) {
    if (!r)
        return;
    {
        std::lock_guard<std::mutex> guard(r->lock);
        r->stopping = true;
        r->changed.notify_all();
    }
    /* Clearing this synchronized callback waits for any active remote-channel
       setup before inspecting/deleting the channel slots. */
    rtcSetDataChannelCallback(r->pc, nullptr);
    if (r->worker.joinable())
        r->worker.join();
    rtcSetUserPointer(r->pc, nullptr);
    if (r->reliable >= 0) {
        rtcSetUserPointer(r->reliable, nullptr);
        rtcDeleteDataChannel(r->reliable);
    }
    if (r->unreliable >= 0) {
        rtcSetUserPointer(r->unreliable, nullptr);
        rtcDeleteDataChannel(r->unreliable);
    }
    rtcDeletePeerConnection(r->pc);
    hw_net_destroy(r->net);
    delete r;
}
