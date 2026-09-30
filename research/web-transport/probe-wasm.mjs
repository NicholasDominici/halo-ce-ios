/* Offline parser probe: no network, filesystem, rendering, or game main imports.
   Supply your local copy of the exact public artifact documented in README. */
import fs from "node:fs";
import crypto from "node:crypto";
import assert from "node:assert/strict";
import { encodeFrame, TYPES, MAX_FRAME_BYTES } from "./frames.mjs";

if (process.argv.length !== 3) throw new Error("Usage: node research/web-transport/probe-wasm.mjs /path/to/halo.wasm");
const binary = fs.readFileSync(process.argv[2]);
assert.equal(crypto.createHash("sha256").update(binary).digest("hex"), "929f542c820a5491d8cd29c21e3d10bfcd777e956c2e88115d25c87720652854", "Unexpected deployment: re-inspect its ABI before probing.");
const module = new WebAssembly.Module(binary);
// The inspected module requires 2.25 GiB of shared virtual address space.
const memory = new WebAssembly.Memory({ initial: 36864, maximum: 36864, shared: true });
const imports = {};
for (const item of WebAssembly.Module.imports(module)) {
  const group = imports[item.module] ??= {};
  if (item.kind === "memory") group[item.name] = memory;
  else if (item.kind === "function") group[item.name] = () => 0;
  else throw new Error(`Uninspected import: ${item.kind} ${item.name}`);
}
const api = new WebAssembly.Instance(module, imports).exports;
api.__wasm_call_ctors();
const ingress = api.web_net_remote_ingress_buffer();
assert.equal(api.web_net_remote_ingress_capacity(), MAX_FRAME_BYTES);
const heap = new Uint8Array(memory.buffer);
heap.set([2, 1, 2, 3, 4, 5], ingress);
const peer = api.web_net_remote_add_peer(ingress, 6);
assert.notEqual(peer, 0);
assert.equal(api.web_net_remote_set_peer_state(peer, 1, 1, 1), 1);
let checks = 0;
function check(name, frame, expected) {
  heap.set(frame, ingress);
  assert.equal(api.web_net_remote_receive(peer, frame.length), expected, name);
  checks++;
}
const datagram = encodeFrame({ type: TYPES.DATAGRAM, sourcePort: 5151, destinationPort: 5150, payload: Uint8Array.of(42) });
check("datagram consumed without a bound receiver", datagram, 1);
for (const [name, offset, value] of [["magic", 0, 0], ["version", 1, 2], ["type", 2, 0], ["reserved", 3, 1], ["datagram ID", 7, 1]]) {
  const frame = datagram.slice();frame[offset] = value;check(name, frame, -1);
}
check("short header", datagram.subarray(0, 11), -1);
check("maximum datagram", encodeFrame({ type: 1, sourcePort: 5151, destinationPort: 5150, payload: new Uint8Array(1500) }), 1);
const oversized = new Uint8Array(1513);oversized.set(datagram);
check("oversized datagram", oversized, -1);
const open = encodeFrame({ type: 2, streamId: 0x12345678, sourcePort: 5151, destinationPort: 5150 });
check("open consumed without a listener", open, 1);
check("open cannot carry a payload", new Uint8Array([...open, 42]), -1);
const zeroPort = open.slice();zeroPort[8] = zeroPort[9] = 0;check("open requires source port", zeroPort, -1);
for (const type of [TYPES.STREAM_DATA, TYPES.STREAM_CLOSE]) {
  const frame = encodeFrame({ type, streamId: 0x12345678 });
  check("unknown nonzero stream is consumed/dropped", frame, 1);
  const zeroId = frame.slice();zeroId.fill(0, 4, 8);check("stream requires ID", zeroId, -1);
  const port = frame.slice();port[9] = 1;check("data/close cannot carry ports", port, -1);
}
check("close cannot carry a payload", new Uint8Array([...encodeFrame({ type: 4, streamId: 1 }), 42]), -1);
api.web_net_remote_remove_peer(peer);
console.log(`PASS: ${checks} public WASM parser fixtures. Consumption does not prove delivery, WebRTC connectivity, or game compatibility.`);
