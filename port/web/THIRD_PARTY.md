# Experimental native WebRTC dependencies

The default Halo iOS release does not compile this module. Enabling
`HALO_ENABLE_WEBRTC` links the portable socket/frame code and its native
WebRTC backend. `tools/web_transport_build.py` fetches unmodified upstream
source into the ignored `build/third_party` cache, pinned to:

| Component | Revision / source | License |
| --- | --- | --- |
| libdatachannel 0.24.6 | [6b1e2e6](https://github.com/paullouisageneau/libdatachannel/tree/6b1e2e620f1e37f0eafeee702eaea0043cb305fd) | MPL-2.0 |
| libjuice | libdatachannel's pinned submodule | MPL-2.0 |
| usrsctp | libdatachannel's pinned submodule | BSD-3-Clause |
| plog | libdatachannel's pinned submodule | MIT |
| Mbed TLS 3.6.7 | [068ff08](https://github.com/Mbed-TLS/mbedtls/tree/068ff080b369adfac81509f9b57b2afabaf82dc5) | Apache-2.0 selected from the upstream dual license |
| nlohmann/json | libdatachannel's pinned submodule; desktop probe only | MIT |

Mbed TLS retains its upstream component notices. Media and WebSockets are
disabled; libsrtp is not linked. The TLS configuration enables DTLS-SRTP
(required by libdatachannel's DTLS implementation) and pthread locking. Peer
fingerprint verification stays enabled.

Experimental Halo builds bundle the upstream license texts under
`Resources/Licenses`. Distributors must retain those notices and make the MPL
components' source available under their license; the links and pinned commits
above identify the unmodified source. The iOS probe is an unsigned test app;
no third-party game assets, downloaded web engine, room tickets or credentials
are included in this repository.
