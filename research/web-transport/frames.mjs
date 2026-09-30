/* Research codec for the public September 30, 2026 deployment.
   This is not a WebRTC adapter or a supported protocol commitment. */
export const HEADER_BYTES = 12;
export const MAX_FRAME_BYTES = 16_396;
export const MAX_DATAGRAM_BYTES = 1_500;
export const TYPES = Object.freeze({ DATAGRAM: 1, STREAM_OPEN: 2, STREAM_DATA: 3, STREAM_CLOSE: 4 });

function bytes(value) {
  if (value instanceof Uint8Array) return value;
  if (value instanceof ArrayBuffer) return new Uint8Array(value);
  throw new TypeError("Expected Uint8Array or ArrayBuffer.");
}
function integer(value, maximum, label) {
  if (!Number.isInteger(value) || value < 0 || value > maximum) throw new RangeError(`Invalid ${label}.`);
}
function validate({ type, streamId, sourcePort, destinationPort, payload }) {
  integer(type, 4, "frame type");
  if (!type) throw new RangeError("Invalid frame type.");
  integer(streamId, 0xffffffff, "stream ID");
  integer(sourcePort, 0xffff, "source port");
  integer(destinationPort, 0xffff, "destination port");
  if (payload.byteLength > MAX_FRAME_BYTES - HEADER_BYTES) throw new RangeError("Frame too large.");
  if (type === TYPES.DATAGRAM) {
    if (streamId || !sourcePort || !destinationPort || payload.byteLength > MAX_DATAGRAM_BYTES) throw new RangeError("Invalid datagram.");
  } else if (type === TYPES.STREAM_OPEN) {
    if (!streamId || !sourcePort || !destinationPort || payload.byteLength) throw new RangeError("Invalid stream open.");
  } else {
    if (!streamId || sourcePort || destinationPort || (type === TYPES.STREAM_CLOSE && payload.byteLength)) throw new RangeError("Invalid stream data/close.");
  }
}
export function encodeFrame({ type, streamId = 0, sourcePort = 0, destinationPort = 0, payload = new Uint8Array() }) {
  payload = bytes(payload);
  validate({ type, streamId, sourcePort, destinationPort, payload });
  const frame = new Uint8Array(HEADER_BYTES + payload.byteLength), view = new DataView(frame.buffer);
  frame.set([0x48, 1, type, 0]);
  view.setUint32(4, streamId, false);
  view.setUint16(8, sourcePort, false);
  view.setUint16(10, destinationPort, false);
  frame.set(payload, HEADER_BYTES);
  return frame;
}
export function decodeFrame(input) {
  const frame = bytes(input);
  if (frame.byteLength < HEADER_BYTES || frame.byteLength > MAX_FRAME_BYTES) throw new RangeError("Invalid frame length.");
  if (frame[0] !== 0x48 || frame[1] !== 1 || frame[3] !== 0) throw new RangeError("Invalid frame header.");
  const view = new DataView(frame.buffer, frame.byteOffset, frame.byteLength);
  const result = { type: frame[2], streamId: view.getUint32(4, false), sourcePort: view.getUint16(8, false), destinationPort: view.getUint16(10, false), payload: frame.slice(HEADER_BYTES) };
  validate(result);
  return result;
}
