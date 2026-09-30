# Matchmaking demo validation

September 30, 2026 · 0.2.0 (build 7) · development branch

## Implemented behavior

The UIKit Multiplayer panel coordinates eight-player Blood Gulch Slayer through
a deployable Worker/Durable Object service. The first compatible client hosts.
The host waits 20 seconds, counts players that joined the game, fills remaining
seats with bots, and starts the ordinary Halo network simulation. Solo practice
starts directly without a service. BOT names are explicit in the game roster.
The host submits bot actions through the authoritative input queue; joining
clients receive ordinary replicated Halo actions. Bots move, aim, fire, reload,
and jump. They do not plan complete routes or use vehicles.

Map compatibility uses the SHA-256 of the imported Blood Gulch map plus a native
protocol identifier. Match size is limited in both the coordinator and the game.
Cancel revokes the queue ticket, tears down the native session, clears bots, and
returns to the main menu. Fresh sessions rotate their invites and drop previous
peers. Practice does not advertise an internet host after an online match.

## Local evidence

- `python3 tools/ios_test.py`: native ILP32, concurrent memory tracking, SDL PCM
  handoff, display presets/native dimensions, the Darwin socket regression, and
  all 15 XISO import/extraction cases passed.
- The socket probe passed inside the iOS 26.0 ARM64 simulator as well as on macOS.
  It checks UDP requests/replies, Winsock sockaddr conversion, select, and a
  connected socket's requested destination. This fixes Darwin's `EISCONN` failure
  on Halo's connected UDP action socket without redirecting a different peer.
- Nine service model tests passed. Real Wrangler/DO HTTP tests passed concurrent
  eight-ticket allocation, bearer membership, host-only invite publication,
  cancellation, body limits, and expiry after a 65-second alarm window.
- `wrangler deploy --dry-run` passed. No public service has been deployed.
- Two app instances, iOS 26.0 and 26.5 iPad simulators, reached one match through
  the real coordinator and upstream native P2P transport: two humans, six bots.
  Their simulation ticks and kill/death totals tracked each other. A separate
  rendered-host run continued for over 15 minutes (27,000 ticks), with both
  clients reporting ten kills/deaths. Bot deaths and respawns were observed.
  In the lifecycle run the client then cancelled,
  returned to idle, and started another match with seven bots successfully.
- The simulator harness invoked the same UIKit coordinator methods as the
  buttons. A separate run presented the actual panel, reached a two-human /
  six-bot match, dismissed it, and rendered Blood Gulch with fighting bots.
  The main transport run disabled drawing; the visible run used 640 × 480 to
  limit the simulator's software renderer. Device defaults remain native pixels.
- Device and simulator builds completed. The signed app passed strict codesign
  verification. Build 7 installed and launched on the iPhone 17 Pro Max; all 42
  save-directory files matched their pre-update SHA-256 hashes. The iPad install
  is pending an unlocked device. Physical matchmaking gameplay is pending user
  confirmation; install/launch evidence does not establish gameplay or sound.

The [web codec](../../research/web-transport/README.md) passed four portable
golden-fixture tests and 19 parser checks against the fingerprinted public
WASM in an offline harness. No web room connection or crossplay was tested.

The opt-in simulator integration harness is excluded from device builds. Set
`HALO_MATCHMAKING_AUTOSTART=queue` or `practice` when launching a simulator.
`HALO_MATCHMAKING_TEST_SERVICE` selects the local coordinator;
`HALO_MATCHMAKING_TEST_PRESENT=1` presents the real panel;
`HALO_MATCHMAKING_CANCEL_AFTER=120` exercises cancellation and a new practice
match. Without those variables, the app runs normally.

## Limits

This is a demo without ranking, host migration, reconnect recovery, a NAT relay,
full bot navigation, or mid-round bot replacement for a departed human. A queue
ticket is not proof of a game connection. Restrictive NAT can prevent joins;
cancel/retry or use solo practice. Keep the host app active during the round.
A match ends when its host leaves. Leave the game before finding another match.
Round-end status is implemented but a complete 50-kill round is not yet tested.

There is no default public matchmaking endpoint. Deploy the included service
and enter the same HTTPS URL on each physical device. The public web port uses
a separate WebRTC transport; crossplay is not implemented. See
[the public-client research](../../research/web-crossplay.md) and the
[unsent collaboration draft](../../research/collaboration-brief.md).

Runtime captures and credential-free simulation summaries are saved outside
the source repository in the task's `outputs/matchmaking-validation` directory.
Game files, device saves, room credentials, and signing material are excluded
from Git.
