const crypto = require("crypto");
const http = require("http");
const net = require("net");

const PORT = 47832;
const MAGIC = 0x44533241;
const PIPE_PATH = "\\\\.\\pipe\\ds2_tab_audio_pcm";

let stats = {
  packets: 0,
  frames: 0,
  bytes: 0,
  forwarded: 0,
  lastSeq: null,
  drops: 0,
  startedAt: Date.now()
};
let pipeClient = null;

const server = http.createServer((req, res) => {
  res.writeHead(200, { "content-type": "text/plain" });
  res.end("DS2 tab audio PCM bridge\n");
});

server.on("upgrade", (req, socket) => {
  const key = req.headers["sec-websocket-key"];
  if (!key) {
    socket.destroy();
    return;
  }

  const accept = crypto
    .createHash("sha1")
    .update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
    .digest("base64");

  socket.write([
    "HTTP/1.1 101 Switching Protocols",
    "Upgrade: websocket",
    "Connection: Upgrade",
    `Sec-WebSocket-Accept: ${accept}`,
    "",
    ""
  ].join("\r\n"));

  console.log(`[bridge] client connected from ${socket.remoteAddress}`);
  const parser = createFrameParser((payload) => handlePacket(payload));
  socket.on("data", (data) => parser.push(data));
  socket.on("close", () => console.log("[bridge] client disconnected"));
  socket.on("error", (err) => console.log(`[bridge] socket error: ${err.message}`));
});

server.listen(PORT, "127.0.0.1", () => {
  console.log(`[bridge] listening on ws://127.0.0.1:${PORT}`);
});

const pipeServer = net.createServer((client) => {
  console.log("[bridge] game pipe client connected");
  if (pipeClient) {
    pipeClient.destroy();
  }
  pipeClient = client;
  client.on("close", () => {
    if (pipeClient === client) pipeClient = null;
    console.log("[bridge] game pipe client disconnected");
  });
  client.on("error", (err) => console.log(`[bridge] pipe error: ${err.message}`));
});

pipeServer.listen(PIPE_PATH, () => {
  console.log(`[bridge] pipe listening on ${PIPE_PATH}`);
});

setInterval(() => {
  const seconds = Math.max(1, (Date.now() - stats.startedAt) / 1000);
  const fps = Math.round(stats.frames / seconds);
  console.log(
    `[bridge] packets=${stats.packets} frames=${stats.frames} ` +
    `fps=${fps} bytes=${stats.bytes} forwarded=${stats.forwarded} drops=${stats.drops}`
  );
}, 5000);

function handlePacket(payload) {
  if (payload.length < 28) {
    return;
  }

  const magic = payload.readUInt32LE(0);
  if (magic !== MAGIC) {
    return;
  }

  const channels = payload.readUInt16LE(6);
  const sampleRate = payload.readUInt32LE(8);
  const frameCount = payload.readUInt32LE(12);
  const sequence = Number(payload.readBigUInt64LE(16));
  const pcmBytes = payload.readUInt32LE(24);
  if (payload.length - 28 !== pcmBytes) {
    return;
  }

  if (stats.lastSeq !== null && sequence !== stats.lastSeq + 1) {
    stats.drops += Math.max(0, sequence - stats.lastSeq - 1);
  }

  stats.lastSeq = sequence;
  stats.packets++;
  stats.frames += frameCount;
  stats.bytes += pcmBytes;
  forwardToPipe(payload);

  if (stats.packets === 1) {
    console.log(
      `[bridge] first packet: channels=${channels} ` +
      `sampleRate=${sampleRate} frames=${frameCount} pcmBytes=${pcmBytes}`
    );
  }
}

function forwardToPipe(payload) {
  if (!pipeClient || pipeClient.destroyed || !pipeClient.writable) {
    return;
  }

  const prefix = Buffer.allocUnsafe(4);
  prefix.writeUInt32LE(payload.length, 0);
  pipeClient.write(Buffer.concat([prefix, payload]));
  stats.forwarded++;
}

function createFrameParser(onBinary) {
  let buffer = Buffer.alloc(0);
  return {
    push(chunk) {
      buffer = Buffer.concat([buffer, chunk]);
      while (true) {
        const parsed = parseOneFrame(buffer);
        if (!parsed) return;
        buffer = buffer.subarray(parsed.consumed);
        if (parsed.opcode === 0x2) onBinary(parsed.payload);
      }
    }
  };
}

function parseOneFrame(buffer) {
  if (buffer.length < 2) return null;
  const byte0 = buffer[0];
  const byte1 = buffer[1];
  const opcode = byte0 & 0x0f;
  const masked = (byte1 & 0x80) !== 0;
  let length = byte1 & 0x7f;
  let offset = 2;

  if (length === 126) {
    if (buffer.length < offset + 2) return null;
    length = buffer.readUInt16BE(offset);
    offset += 2;
  } else if (length === 127) {
    if (buffer.length < offset + 8) return null;
    const bigLength = buffer.readBigUInt64BE(offset);
    if (bigLength > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error("frame too large");
    length = Number(bigLength);
    offset += 8;
  }

  if (!masked) throw new Error("client frame must be masked");
  if (buffer.length < offset + 4 + length) return null;

  const mask = buffer.subarray(offset, offset + 4);
  offset += 4;
  const payload = Buffer.from(buffer.subarray(offset, offset + length));
  for (let i = 0; i < payload.length; i++) {
    payload[i] ^= mask[i & 3];
  }
  return { opcode, payload, consumed: offset + length };
}
