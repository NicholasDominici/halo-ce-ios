# Native matchmaking demo

A Cloudflare Worker and SQLite-backed Durable Object place compatible native clients into eight-player Blood Gulch Slayer rooms. The first client hosts. After a 20-second queue window, the host fills unoccupied simulation seats with practice bots. Game packets use the existing native P2P invite transport; this service only coordinates the queue.

This is a demo, without rankings, skill matching, host migration, moderation, or a relay for restrictive NAT. A queue ticket is not proof that the associated client reached the game host. The host counts actual joined game players before adding bots.

## Run locally

```sh
cd services/matchmaking
npm ci
npm test
npm run dev
```

With Wrangler running, execute `node test/http.integration.js`. It checks eight concurrent allocations, invitation authorization, request limits, and cancellation against the real Worker/DO. `MATCHMAKING_URL` can select an explicitly authorized test deployment instead.

The iOS simulator accepts `http://127.0.0.1:8787` in the Multiplayer demo's service field. Physical devices require a reachable HTTPS deployment. The game files remain on the device and are never uploaded to the coordinator.

## Deploy your own service

```sh
npx wrangler login
npx wrangler deploy --dry-run
npm run deploy
```

Check the selected account before deployment. Enter the returned HTTPS URL in the app. All clients must select the same endpoint and import identical Blood Gulch map data. The app sends only a compatibility hash, player display name, random ticket credentials, and the native host invite. Room credentials expire when the host stops renewing its lease.

## API v1

| Request | Body / authorization | Result |
| --- | --- | --- |
| `GET /health` | none | service/version |
| `POST /v1/tickets` | `playlist`, `compatibility`, `name` | ticket, role, deadline, bearer token |
| `GET /v1/tickets/{id}` | bearer token | current room, host invite for joining members |
| `PATCH /v1/tickets/{id}` | host bearer token; `invite` | publish the native invite |
| `DELETE /v1/tickets/{id}` | bearer token | cancel; a host cancellation invalidates the room |

The service caps searches at ten per source IP per minute, active tickets at 2,048, bodies at 2,048 bytes, rooms at eight members, leases at 60 seconds, and match records at 30 minutes. Each state change runs in a Durable Object storage transaction. Expiry alarms remove abandoned records. The IP-based limiter stores its key for at most a minute; do not enable additional request/body logging with credentials.

Tickets and room tokens are bearer credentials. Use HTTPS, do not publish them in logs, and do not expose this coordinator as the web port's signaling server. See [the crossplay research](../../research/web-crossplay.md) for the separate interoperability work.
