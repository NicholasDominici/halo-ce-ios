/* Run against wrangler dev, or an explicitly supplied demo endpoint. */
import assert from "node:assert/strict";
const base = process.env.MATCHMAKING_URL ?? "http://127.0.0.1:8787";
const tickets = [];
async function request(method, path, body, token, expected = 200) {
  const response = await fetch(base + path, { method, headers: { "Content-Type": "application/json", ...(token ? { Authorization: `Bearer ${token}` } : {}) }, body: body === undefined ? undefined : JSON.stringify(body) });
  const result = await response.json();assert.equal(response.status, expected, JSON.stringify(result));return result;
}
try {
  assert.equal((await request("GET", "/health")).version, 1);
  const compatibility = "http-test:" + crypto.randomUUID();
  const input = { playlist: "bloodgulch-slayer-8", compatibility, name: "TestSpartan" };
  const host = await request("POST", "/v1/tickets", input, null, 201);tickets.push(host);
  const joins = await Promise.all(Array.from({ length: 7 }, () => request("POST", "/v1/tickets", input, null, 201)));
  tickets.push(...joins);
  assert.equal(host.role, "host");
  for (const join of joins) { assert.equal(join.role, "join");assert.equal(join.room, host.room); }
  const invite = "halo://join/" + "a".repeat(44);
  await request("PATCH", `/v1/tickets/${joins[0].id}`, { invite }, joins[0].token, 403);
  await request("PATCH", `/v1/tickets/${host.id}`, { invite }, host.token);
  const polled = await request("GET", `/v1/tickets/${joins[0].id}`, undefined, joins[0].token);
  assert.equal(polled.invite, invite);assert.equal(polled.humans, 8);assert.equal(polled.botSeats, 0);assert.equal(polled.token, undefined);
  await request("GET", `/v1/tickets/${host.id}`, undefined, "wrong-token", 404);
  await request("POST", "/v1/tickets", { ...input, name: "x".repeat(4096) }, null, 413);
  await request("DELETE", `/v1/tickets/${host.id}`, undefined, host.token);
  assert.equal((await request("GET", `/v1/tickets/${joins[0].id}`, undefined, joins[0].token)).state, "expired");
  console.log("HTTP integration passed: concurrent allocation, membership, invite authorization, body limit, cancellation.");
  if(process.env.MATCHMAKING_TEST_EXPIRY === "1") {
    const abandoned = await request("POST", "/v1/tickets", { ...input, compatibility: "expiry:" + crypto.randomUUID() }, null, 201);
    tickets.push(abandoned);
    await new Promise(resolve => setTimeout(resolve, 65_000));
    await request("GET", `/v1/tickets/${abandoned.id}`, undefined, abandoned.token, 404);
    console.log("HTTP lease expiry passed after the Durable Object alarm window.");
  }
} finally {
  for (const ticket of tickets) await request("DELETE", `/v1/tickets/${ticket.id}`, undefined, ticket.token).catch(() => {});
}
