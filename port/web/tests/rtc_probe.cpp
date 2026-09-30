/* Local test peer. JSON lines carry SDP/ICE only; game frames use DTLS/SCTP. */
#include "../rtc_transport.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <cstring>
#include <cerrno>
using json = nlohmann::json;
static std::mutex outputLock;
static std::atomic<bool> ready{false}, failed{false}, stopping{false};
static void output(const json &value) {
    std::lock_guard<std::mutex> guard(outputLock);
    std::cout << value.dump() << std::endl;
}
static void signal(void *, const char *event, const char *value, const char *detail) {
    output({{"event", event}, {"value", value}, {"detail", detail}});
    if (!strcmp(event, "ready"))
        ready = true;
    if (!strcmp(event, "error"))
        failed = true;
}
int main(int argc, char **argv) {
    bool initiator = argc > 1 && !strcmp(argv[1], "offer");
    unsigned char local[6] = {2, 0, 0, 0, 0, 1}, remote[6] = {2, 0, 0, 0, 0, 2};
    hw_rtc *rtc = hw_rtc_create(local, remote, initiator, signal, nullptr);
    if (!rtc)
        return 1;
    hw_net *net = hw_rtc_net(rtc);
    uint32_t peer = hw_rtc_peer(rtc);
    int server = hw_socket(net, SOCK_STREAM), udp = hw_socket(net, SOCK_DGRAM);
    if (server < 0 || udp < 0 || hw_bind(net, server, 0, 5150) || hw_listen(net, server, 8) ||
        hw_bind(net, udp, 0, 5151))
        return 2;
    std::thread game([&] {
        int stream = -1;
        while (!stopping && !failed) {
            if (ready) {
                if (stream < 0)
                    stream = hw_accept(net, server, nullptr, nullptr);
                char bytes[16384];
                uint32_t ip;
                uint16_t port;
                int n = hw_recvfrom(net, udp, bytes, sizeof(bytes), 0, &ip, &port);
                if (n >= 0) {
                    hw_sendto(net, udp, bytes, (size_t)n, 0, ip, port);
                    output({{"event", "datagram"}, {"bytes", n}});
                }
                if (stream >= 0) {
                    n = hw_recv(net, stream, bytes, sizeof(bytes), 0);
                    if (n > 0) {
                        int sent = hw_send(net, stream, bytes, (size_t)n, 0);
                        output({{"event", "stream"}, {"bytes", n}, {"sent", sent}});
                    } else if (n == 0) {
                        hw_close(net, stream);
                        stream = -1;
                        output({{"event", "eof"}});
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            auto command = json::parse(line);
            std::string type = command.at("type");
            int result = 0;
            if (type == "description")
                result =
                    hw_rtc_description(rtc, command.at("sdp").get<std::string>().c_str(),
                                       command.at("descriptionType").get<std::string>().c_str());
            else if (type == "candidate")
                result = hw_rtc_candidate(rtc, command.at("candidate").get<std::string>().c_str(),
                                          command.value("mid", std::string("0")).c_str());
            else if (type == "quit")
                break;
            else if (type == "native-send") {
                int fd = hw_socket(net, SOCK_STREAM);
                std::string payload = command.at("payload");
                if (hw_connect(net, fd, peer, 5150) ||
                    hw_send(net, fd, payload.data(), payload.size(), 0) < 0)
                    result = -1;
                else {
                    char response[4096];
                    int count = -1;
                    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                    while (count < 0 && std::chrono::steady_clock::now() < deadline) {
                        count = hw_recv(net, fd, response, sizeof(response), 0);
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    output({{"event", "native-reply"},
                            {"payload", count > 0 ? std::string(response, (size_t)count) : ""},
                            {"count", count},
                            {"error", count < 0 ? errno : 0}});
                    hw_close(net, fd);
                }
            } else
                result = -1;
            if (result < 0)
                output({{"event", "command-error"}, {"type", type}});
        } catch (const std::exception &e) {
            output({{"event", "command-error"}, {"message", e.what()}});
        }
    }
    stopping = true;
    game.join();
    hw_rtc_destroy(rtc);
    return failed ? 3 : 0;
}
