# Local native / browser transport experiment

This directory records the frame layout inferred from the fingerprinted
September 30, 2026 [public web build](https://halo-web.otherness-bugs.workers.dev/).
The native implementation is in [`port/web`](../../port/web). It creates real
WebRTC DTLS/SCTP connections and translates virtual TCP/UDP sockets into the
observed frames. No third-party web engine or Halo assets are included.

**This is packet-delivery compatibility, not verified Halo game crossplay.**
The browser fixture runs our codec and echo logic. It does not run the author's
engine, obtain room admission or connect to his service. Cloudflare is not used.

## Build and run locally

Requirements: Git, Python 3, CMake 3.24+, Ninja, a C++17 compiler, Node 22+, and a
browser with WebRTC. Apple builds require full Xcode on an Apple Silicon Mac.
The builder fetches pinned libdatachannel 0.24.6 and Mbed TLS 3.6.7 sources into
the ignored build cache and refuses to reset existing local edits.

```sh
python3 tools/web_transport_build.py --platform native
node research/web-transport/local-server.mjs
```

Open `http://127.0.0.1:9030/` in a browser. The page automatically checks both
channel policies, a 1,500-byte datagram, an ordered 250,000-byte stream larger
than the native socket queue, remote EOF, and a native-initiated stream reply.
The helper listens on loopback only. Stop it with Ctrl-C after the test.

Restart it with `--native-offer` to check the opposite negotiation role. Close
the previous browser test tab before starting a fresh run. The server's
`GET /signal` response includes the result and payload-free native events.
`HALO_WEB_TEST_PORT` selects another loopback port; `HALO_WEB_TEST_BINARY` selects
an alternate native probe binary. This fixture supports one pair per run.

## iPhone/iPad simulator probe

```sh
python3 tools/web_transport_build.py --platform simulator
HALO_WEB_TEST_PORT=9032 node research/web-transport/local-server.mjs --external-native
```

Install `build/web/simulator/Release-iphonesimulator/HaloWebProbe.app` on a
simulator and launch bundle `org.haloce.webtransportprobe`. The app defaults to
`http://127.0.0.1:9032`; `HALO_WEB_SIGNAL_URL` may select another literal loopback
HTTP port. Open `http://127.0.0.1:9032/` in the desktop browser. Add
`--native-offer` to the server for the opposite role. Use **Stop probe** to close
all sockets and channels; then stop the server and relaunch for another run.

The iOS probe calls the same `posix_socket_*` entry points Halo's guest uses,
including its Winsock error translation, 16-bit sockaddr family, port/address
byte order and select readiness. It does not execute Halo's guest game loop.
`--platform device` builds an unsigned ARM64 device probe for compile validation;
its loopback HTTP fixture requires a simulator and is not a physical-device test.

## Adapter integration and current limits

`python3 tools/ios_build.py --simulator --web-transport` compiles the optional
adapter into Halo. The flag does **not** activate a connection. Release builds
leave it off. A future admitted-session owner must create `hw_rtc`, exchange
SDP/candidates through a supported service, wait for both channels, install its
socket context with `halo_web_install_transport`, and drive the game host/join
lifecycle while excluding the existing UDP/KCP invite tunnel. Close all virtual
sockets and detach the context before destroying it. Detach with live sockets
fails with `EBUSY`; normal BSD sockets created before activation remain BSD.

The experiment has one WebRTC peer per context, no STUN/TURN configuration,
room admission, reconnect, host migration or public matchmaking integration.
Virtual addresses are process-local, so both engines must map the agreed
six-byte peer IDs to their own virtual addresses. Native stream IDs use opposite
parity and a high initial range; allocation/collision rules still need author
confirmation. Broadcast discovery is consumed locally and is not room admission.

Sockets are nonblocking. Stream reads support partial data and `MSG_PEEK`;
receive queues are bounded to 64 KiB per socket. Reliable DataChannel ingress and
egress have bounded 1 MiB queues; overflow terminates the peer rather than
silently dropping stream data. Malformed channel frames also terminate the
peer; this rejection/closure path was checked over a real browser connection.
Datagrams may drop, as with UDP. `shutdown` is a
full stream close because the observed protocol has no half-close. Common guest
socket options are accepted with fixed queue sizes. Unsupported operations fail.

## Regression and offline parser checks

```sh
node --test research/web-transport/frames.test.mjs
build/web/native/halo-web-socket-probe
build/web/native/halo-web-posix-probe  # macOS: the actual Darwin/Winsock bridge
```

The C probes use independent golden bytes, partial reads, backpressure/retry,
connected UDP filtering, listener cleanup, EOF/reset, local connections, real
file descriptors mixed with virtual select, and opt-out fallback. Assertions
stay enabled in Release builds. CI also runs address/undefined-behavior sanitizers.

An optional offline probe checks 19 accepted/rejected headers in a local copy of
the public WASM. It pins SHA-256, stubs host functions and invokes constructors
and network ingress only. It reserves 2.25 GiB of virtual memory on a 64-bit host.

```sh
node research/web-transport/probe-wasm.mjs /path/to/halo.wasm
```

An accepted frame may still be dropped when no game socket listens. Neither this
parser probe nor our echo fixture establishes the author's stateful socket
semantics, game-message serialization or simulation synchronization. See the
[interoperability notes](../web-crossplay.md) and
[dependency notices](../../port/web/THIRD_PARTY.md).
