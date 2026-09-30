import test from "node:test";
import assert from "node:assert/strict";
import { encodeFrame, decodeFrame, TYPES } from "./frames.mjs";

test("datagram fixture has the observed network-order ports and unchanged payload", () => {
  const fixture = Uint8Array.from([0x48, 1, 1, 0, 0, 0, 0, 0, 0x14, 0x1f, 0x14, 0x1e, 0xde, 0xad]);
  const data = { type: TYPES.DATAGRAM, streamId: 0, sourcePort: 5151, destinationPort: 5150, payload: Uint8Array.from([0xde, 0xad]) };
  assert.deepEqual(encodeFrame(data), fixture);
  // A buffer slice must not read from offset zero of its backing allocation.
  const padded = new Uint8Array(fixture.length + 7);padded.set(fixture, 7);
  assert.deepEqual(decodeFrame(padded.subarray(7)), data);
});
test("stream fixtures encode IDs in big endian and omit ports after opening", () => {
  assert.deepEqual(encodeFrame({ type: TYPES.STREAM_OPEN, streamId: 0x12345678, sourcePort: 5151, destinationPort: 5150 }), Uint8Array.from([0x48, 1, 2, 0, 0x12, 0x34, 0x56, 0x78, 0x14, 0x1f, 0x14, 0x1e]));
  assert.deepEqual(encodeFrame({ type: TYPES.STREAM_CLOSE, streamId: 0xffffffff }), Uint8Array.from([0x48, 1, 4, 0, 255, 255, 255, 255, 0, 0, 0, 0]));
});
test("payload limits distinguish datagrams and streams", () => {
  const datagram = { type: TYPES.DATAGRAM, sourcePort: 1, destinationPort: 2 };
  assert.equal(encodeFrame({ ...datagram, payload: new Uint8Array(1500) }).length, 1512);
  assert.throws(() => encodeFrame({ ...datagram, payload: new Uint8Array(1501) }));
  assert.equal(encodeFrame({ type: TYPES.STREAM_DATA, streamId: 1, payload: new Uint8Array(16384) }).length, 16396);
  assert.throws(() => encodeFrame({ type: TYPES.STREAM_DATA, streamId: 1, payload: new Uint8Array(16385) }));
});
test("malformed headers and type-specific fields are rejected", () => {
  const valid = encodeFrame({ type: TYPES.DATAGRAM, sourcePort: 1, destinationPort: 2 });
  for (const [offset, value] of [[0, 0], [1, 2], [2, 0], [2, 5], [3, 1], [7, 1], [9, 0], [11, 0]]) {
    const frame = valid.slice();frame[offset] = value;assert.throws(() => decodeFrame(frame));
  }
  assert.throws(() => decodeFrame(new Uint8Array(11)));
  assert.throws(() => encodeFrame({ type: 3, streamId: 0 }));
  assert.throws(() => encodeFrame({ type: 3, streamId: 1, sourcePort: 1 }));
  assert.throws(() => encodeFrame({ type: 4, streamId: 1, payload: new Uint8Array(1) }));
  assert.throws(() => encodeFrame({ type: 2, streamId: 1, sourcePort: 1, destinationPort: 2, payload: new Uint8Array(1) }));
  assert.throws(() => encodeFrame({ type: 1, sourcePort: 1.5, destinationPort: 2 }));
});
