# Native / web Halo collaboration brief

Draft for the project owner to send; no message has been sent.

Hi Mitchell — I have Halo: CE running natively on iPhone and iPad with touch
controls, audio, native display resolution, and an on-device XISO importer.
Your browser port is impressive. I would like to test native/web crossplay
with you.

I inspected the public client and found the `halo-reliable-v1` and
`halo-unreliable-v1` DataChannels, signaling protocol 1, and the WASM network
ingress. I decoded its 12-byte frame header and wrote a small codec with an
offline parser probe against the public binary. I propose carrying the game’s
existing action/connection packets through a compatible native transport.

Could we exchange:

- Confirmation of the inferred frame layout, channel routing, and stateful
  stream/datagram connection semantics in our linked research fixture.
- A protocol/build fingerprint and the relevant capacity/serialization changes.
- A supported native room-admission flow alongside your current Turnstile flow.
- A small captured transport fixture with no room credentials or game assets.

A first test could be one browser and one iOS client on identical Blood Gulch
map data, checking both host roles and Halo’s random-seed synchronization.
The native demo now runs eight-player matchmaking with host-owned practice
bots. Two native simulator clients have played the same match with six bots;
native/web synchronization remains to be tested with you.

Our source: https://github.com/NicholasDominici/halo-ce-ios

Technical observations and open questions: [web-crossplay.md](web-crossplay.md).
