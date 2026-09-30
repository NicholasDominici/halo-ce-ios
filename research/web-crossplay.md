# Public web port interoperability notes

Inspected September 30, 2026: [Halo web](https://halo-web.otherness-bugs.workers.dev/), its public `halo.js`, `halo.wasm`, and the HTML it serves. The page credits [Mitchell Hynes](https://mitchellhynes.com/). This inspection did not create rooms, join third-party sessions, bypass Turnstile, or contact the author.

## What the public client establishes

The game executes locally through Emscripten/WebAssembly. Its multiplayer transport uses two WebRTC DataChannels, not a video stream:

| Channel | Settings |
| --- | --- |
| `halo-reliable-v1` | ordered, unlimited retransmissions |
| `halo-unreliable-v1` | unordered, `maxRetransmits: 0` |

Messages are binary frames between 12 and 16,396 bytes. The JavaScript passes them unchanged to `web_net_remote_receive(address, length)` using a WASM ingress buffer. A six-byte peer identifier maps to a virtual IPv4 address. The header layout below was inferred from the WASM parser, not from a supported API specification.

The public client advertises signaling protocol 1 and build identifier `web-multiplayer-v1`. It creates `/v1/rooms` and joins `/v1/rooms/{id}/sessions` at `https://halo-web-signaling.otherness-bugs.workers.dev`, submitting a Turnstile token. The returned session describes the WebSocket endpoint and ICE configuration; the socket carries offers, answers, and ICE candidates. Room tickets control admission.

Public-artifact fingerprints (SHA-256):

- HTML: `8711c737e3e5c9f3bf7a5a8731eeacab2a6d20ff9677a9f2e0f00b1ec36175ae`
- halo.js: `6926ecdea4d30f423dd6ab5b08f72f19ba3fae2cdba52d968db0226bde86d2f9`
- halo.wasm: `929f542c820a5491d8cd29c21e3d10bfcd777e956c2e88115d25c87720652854`

These are observations of one deployment, not a promise of a stable API.

## Inferred frame layout and offline probe

`web_net_remote_receive` at code offset `0x18ff19` checks this header:

| Offset | Size | Observed meaning |
| --- | --- | --- |
| 0 | 1 | magic `0x48` (H) |
| 1 | 1 | frame version 1 |
| 2 | 1 | type 1–4 |
| 3 | 1 | reserved, must be zero |
| 4 | 4 | stream ID, big-endian unsigned integer |
| 8 | 2 | source port, network byte order |
| 10 | 2 | destination port, network byte order |
| 12 | variable | unchanged payload |

The parser's four branches behave as datagram (1), stream open (2), stream data
(3), and stream close (4). Datagram frames require ID zero, nonzero ports, and
at most 1,500 payload bytes. Open frames require a nonzero ID, nonzero ports,
and no payload. Data/close require a nonzero ID and zero ports; close has no
payload. The global payload limit is 16,384 bytes.

The [portable codec and golden fixtures](web-transport/README.md) encode these
rules. An offline Node probe instantiated the fingerprinted WASM with stubbed
host functions and checked 19 acceptance/rejection cases against its exported
parser. It did not run the game or create a WebRTC connection. A return value
of 1 can mean consumption followed by a drop when no listener or stream exists;
it does not establish socket delivery. Our separate local fixture below now
checks these behaviors in our native adapter and browser peer. An owner-supported
fixture still needs to establish the web engine's stateful socket behavior.

## Native transport implementation and local evidence

The optional [`port/web`](../port/web) module implements a native libdatachannel
DTLS/SCTP backend, bounded socket queues, TCP open/data/close translation,
UDP datagrams and mixed native/virtual `select`. Its iOS integration overrides
the existing `posix_socket_*` guest boundary only after explicit activation;
normal builds retain BSD sockets. A separate UIKit probe uses that exact bridge.

Locally verified on September 30, 2026:

| Check | Evidence / boundary |
| --- | --- |
| C codec and socket semantics | Address/undefined-behavior sanitizer probes, independent golden bytes, bounded queues, retry, EOF/reset, listener cleanup and UDP filtering |
| iOS Winsock boundary | Actual exported bridge: sockaddr/endian conversion, Winsock errors, readiness, guarded detach and BSD fallback |
| Native ↔ browser transport | Real DTLS/SCTP in Chrome, both offer roles; 1,500-byte UDP echo, ordered 250,000-byte stream, close/EOF, native-initiated reply |
| iPad simulator ↔ browser | Same five checks through the iOS Winsock bridge, both offer roles |
| Device target | Unsigned ARM64 probe compiles; no physical crossplay claimed |
| Malformed WebRTC frame | Native rejection followed by peer closure; tested after a successful byte-delivery run |
| Public WASM ingress | 19 offline parser acceptance/rejection cases, with stubbed host functions |

These tests move bytes through **our** browser fixture. They do not run the web
Halo engine or prove game session compatibility. No third-party room was joined,
no public queue was deployed and no Cloudflare account was required. Reproduction
commands and current resource/lifecycle limitations are in the
[fixture README](web-transport/README.md).

Remaining work includes author-confirmed socket/peer-ID allocation, supported
native admission and ICE configuration, game-message/build/map compatibility,
and both-engine synchronization under host/client, loss and reconnect scenarios.

## The useful collaboration boundary

Our app already has the game state it needs to generate its own player's normal network actions. We do not need to scrape the browser's internal state. Genuine crossplay would carry the **game's network protocol** between the two engines.

Our released native transport supplies BSD sockets and the upstream UDP/KCP invite tunnel. The optional experiment supplies framed WebRTC sockets but is not wired to public room admission. That tunnel cannot be sent directly into the web port's DataChannels: the framing and connection semantics differ. Sharing a repository ancestor does not prove wire or simulation compatibility.

A cooperative bridge should:

1. Agree on game-message protocol version, capacity constants, serialization, map-file hashes, and deterministic simulation behavior.
2. Confirm the inferred frame contract with the author, especially stateful connection/open/close, port mapping, stream segmentation, channel selection, ordering, and backpressure.
3. Validate the implemented native WebRTC transport against the author's stateful engine socket semantics and supported peer/stream allocation. Keep game action packets intact whenever the two engines agree on them.
4. Have the owner admit a native client through supported room authentication and build negotiation. Turnstile is not something a native adapter should circumvent.
5. Test host/client roles in both directions, loss/reordering, reconnects, map loading, several players, and random-seed/out-of-sync reporting.

We should exchange a tiny transport fixture and two-client test before attempting a public crossplay queue. Native bots can then run on the host and replicate through ordinary player actions, provided both clients share the same simulation contract.

## Input-proxy alternative

A browser client controlled by an iOS shell would participate as that browser client. It could be a useful demonstration of touch controls against his build, but it would execute his web engine and would not demonstrate native-engine crossplay. Embedding also depends on iOS browser support for the site's isolation, WASM threading, and rendering requirements. No such wrapper or working crossplay connection is claimed here.

## Current native demo boundary

The matchmaking coordinator in `services/matchmaking` allocates native clients and shares their existing authenticated invite. It does not connect to the web signaling service. Native compatibility keys include the Blood Gulch map's SHA-256 and an explicit demo protocol identifier; they intentionally separate incompatible map data.
