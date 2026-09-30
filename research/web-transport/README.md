# Web transport research fixture

This codec records the frame layout inferred from the public September 30, 2026
[WASM deployment](https://halo-web.otherness-bugs.workers.dev/halo.wasm).
It does not connect to signaling, implement WebRTC, or enable native/web crossplay.
No third-party code or game assets are included.

Run the portable golden fixtures with Node 22 or newer:

```sh
node --test research/web-transport/frames.test.mjs
```

An optional offline probe exercises `web_net_remote_receive` in a local copy of
the public WASM. The expected SHA-256 is pinned in the probe. It stubs every host
function, invokes constructors and network-ingress exports only, and performs
no network requests or game main loop. The module reserves 2.25 GiB of shared
virtual memory, so use a 64-bit desktop with sufficient address space.

```sh
node research/web-transport/probe-wasm.mjs /path/to/halo.wasm
```

The probe checks accepted and rejected datagram/open/data/close headers and
datagram limits. An accepted frame may be consumed and dropped when no socket
is listening. This is parser evidence, **not** proof that a game socket receives
the payload or that either engine stays synchronized. Stateful stream delivery,
backpressure, channel selection, and authentication need a cooperative test with
the web author before this can become a production adapter.

See [the interoperability notes](../web-crossplay.md) for the byte layout and
the remaining native/WebRTC integration work.
