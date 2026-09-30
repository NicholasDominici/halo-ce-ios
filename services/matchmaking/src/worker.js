import { DurableObject } from "cloudflare:workers";
import { ApiError, initialState, sweep, createTicket, pollTicket, publishInvite, cancelTicket } from "./model.js";

function json(value, status = 200) {
  return Response.json(value, { status, headers: { "Cache-Control": "no-store", "X-Content-Type-Options": "nosniff" } });
}
export class Matchmaker extends DurableObject {
  async fetch(request) {
    try {
      const url = new URL(request.url), now = Date.now();
      const match = url.pathname.match(/^\/v1\/tickets\/([a-f0-9-]{36})$/);
      const token = request.headers.get("Authorization")?.replace(/^Bearer /, "");
      let body;
      if (request.method === "POST" || request.method === "PATCH") {
        const reader = request.body?.getReader();
        let size = 0, chunks = [];
        if (reader) while (true) {
          const { done, value } = await reader.read();
          if (done) break;
          size += value.byteLength;
          if (size > 2048) { await reader.cancel(); throw new ApiError(413, "Request too large."); }
          chunks.push(value);
        }
        const bytes = new Uint8Array(size);let offset = 0;
        for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.byteLength; }
        const raw = new TextDecoder().decode(bytes);
        try { body = JSON.parse(raw); } catch { throw new ApiError(400, "Invalid JSON."); }
        if (!body || typeof body !== "object" || Array.isArray(body)) throw new ApiError(400, "Expected an object.");
      }
      const identity = { id: crypto.randomUUID(), room: crypto.randomUUID(), token: crypto.randomUUID() + crypto.randomUUID() };
      const result = await this.ctx.storage.transaction(async storage => {
        const state = await storage.get("state") ?? initialState();
        let result;
        if (url.pathname === "/v1/tickets" && request.method === "POST") result = createTicket(state, body, identity, now, request.headers.get("CF-Connecting-IP") ?? "local");
        else if (match && request.method === "GET") result = pollTicket(state, match[1], token, now);
        else if (match && request.method === "PATCH") result = publishInvite(state, match[1], token, body.invite, now);
        else if (match && request.method === "DELETE") result = cancelTicket(state, match[1], token, now);
        else throw new ApiError(404, "Unknown endpoint.");
        await storage.put("state", state);
        await storage.setAlarm(now + 60_000);
        return result;
      });
      return json(result, request.method === "POST" ? 201 : 200);
    } catch (error) { return json({ error: error instanceof ApiError ? error.message : "The service could not complete this request." }, error instanceof ApiError ? error.status : 500); }
  }
  async alarm() {
    await this.ctx.storage.transaction(async storage => {
      const state = await storage.get("state");
      if (!state) return;
      sweep(state, Date.now());
      if (Object.keys(state.tickets).length) { await storage.put("state", state); await storage.setAlarm(Date.now() + 60_000); }
      else await storage.delete("state");
    });
  }
}
export default {
  async fetch(request, env) {
    const path = new URL(request.url).pathname;
    if (path === "/health" && request.method === "GET") return json({ service: "halo-ce-matchmaking-demo", version: 1 });
    if (!path.startsWith("/v1/tickets")) return json({ error: "Not found." }, 404);
    return env.MATCHMAKER.getByName("demo-v1").fetch(request);
  }
};
