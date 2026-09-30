/* Pure matchmaking model. A Durable Object transaction owns every mutation. */
export const CAPACITY = 8;
export const WAIT_MS = 20_000;
export const LEASE_MS = 60_000;
export const MATCH_MS = 30 * 60_000;
export class ApiError extends Error {
  constructor(status, message) { super(message); this.status = status; }
}
export function initialState() { return { tickets: {}, rooms: {}, rates: {} }; }
export function sweep(state, now) {
  for (const room of Object.values(state.rooms)) {
    const host = state.tickets[room.host];
    if (!host || host.expiresAt <= now || room.expiresAt <= now) {
      for (const id of room.members) if (state.tickets[id]) state.tickets[id].state = "expired";
      delete state.rooms[room.id];
    }
  }
  for (const [id, ticket] of Object.entries(state.tickets)) {
    if (ticket.expiresAt <= now) {
      const room = state.rooms[ticket.room];
      if (room) room.members = room.members.filter(member => member !== id);
      delete state.tickets[id];
    }
  }
  for (const [key, rate] of Object.entries(state.rates)) if (now >= rate.until) delete state.rates[key];
}
export function createTicket(state, input, identity, now, rateKey = "local") {
  sweep(state, now);
  if (input.playlist !== "bloodgulch-slayer-8") throw new ApiError(400, "Unsupported playlist.");
  if (typeof input.compatibility !== "string" || !/^[A-Za-z0-9._:-]{1,160}$/.test(input.compatibility)) throw new ApiError(400, "Invalid compatibility identifier.");
  if (typeof input.name !== "string" || !/^[^\x00-\x1f\x7f]{1,11}$/u.test(input.name)) throw new ApiError(400, "Use a player name of 1–11 characters.");
  const rate = state.rates[rateKey] ??= { count: 0, until: now + 60_000 };
  if (++rate.count > 10) throw new ApiError(429, "Too many searches. Wait a minute and retry.");
  if (Object.keys(state.tickets).length >= 2048) throw new ApiError(503, "The demo is at capacity. Try later.");
  let room = Object.values(state.rooms).find(room => room.playlist === input.playlist && room.compatibility === input.compatibility && room.startsAt > now && room.members.length < CAPACITY);
  if (!room) {
    room = { id: identity.room, host: identity.id, playlist: input.playlist, compatibility: input.compatibility, startsAt: now + WAIT_MS, expiresAt: now + MATCH_MS, members: [], invite: null };
    state.rooms[room.id] = room;
  }
  const ticket = { id: identity.id, token: identity.token, room: room.id, role: room.host === identity.id ? "host" : "join", name: input.name, state: "waiting", expiresAt: now + LEASE_MS };
  state.tickets[ticket.id] = ticket;room.members.push(ticket.id);
  return { ...ticketView(state, ticket), token: ticket.token };
}
function ticketView(state, ticket) {
  const room = state.rooms[ticket.room];
  return { id: ticket.id, room: ticket.room, role: ticket.role, state: room ? ticket.state : "expired", startsAt: room?.startsAt, capacity: CAPACITY, humans: room?.members.length ?? 0, botSeats: CAPACITY - (room?.members.length ?? 0), invite: ticket.role === "join" ? room?.invite ?? null : null };
}
export function authenticatedTicket(state, id, token, now) {
  sweep(state, now);
  const ticket = state.tickets[id];
  if (!ticket || !token || ticket.token !== token) throw new ApiError(404, "Search not found or expired.");
  return ticket;
}
export function pollTicket(state, id, token, now) {
  const ticket = authenticatedTicket(state, id, token, now);
  ticket.expiresAt = now + LEASE_MS;
  return ticketView(state, ticket);
}
export function publishInvite(state, id, token, invite, now) {
  const ticket = authenticatedTicket(state, id, token, now);
  if (ticket.role !== "host") throw new ApiError(403, "Only the host can publish an invite.");
  if (typeof invite !== "string" || !/^halo:\/\/join\/[0-9a-fA-F]{44}$/.test(invite)) throw new ApiError(400, "Invalid native Halo invite.");
  const room = state.rooms[ticket.room];
  if (!room) throw new ApiError(409, "This match has expired.");
  if (room.invite && room.invite !== invite) throw new ApiError(409, "This match already has a different invite.");
  room.invite = invite; ticket.expiresAt = now + LEASE_MS;
  return ticketView(state, ticket);
}
export function cancelTicket(state, id, token, now) {
  const ticket = authenticatedTicket(state, id, token, now), room = state.rooms[ticket.room];
  if (room && ticket.role === "host") {
    for (const member of room.members) if (state.tickets[member]) state.tickets[member].state = "cancelled";
    delete state.rooms[room.id];
  } else if (room) room.members = room.members.filter(member => member !== id);
  delete state.tickets[id];return { state: "cancelled" };
}
