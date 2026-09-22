const path = require('path');
const express = require('express');
const http = require('http');
const { WebSocketServer } = require('ws');

const PORT = process.env.PORT || 8080;

const app = express();
app.use(express.static(path.join(__dirname, 'public')));

const server = http.createServer(app);
const wss = new WebSocketServer({ server, path: '/ws' });

// Every connected socket (browser dashboard OR the ESP32 / simulator) sits
// in the same set. Any "sample" message received from one is broadcast to
// all the others. This keeps the protocol symmetric and trivial to test:
// the simulator just pretends to be the ESP32.
const clients = new Set();

function parseIncoming(raw) {
  // Accepts either JSON: {"v":2048,"t":123456}
  // or a lightweight CSV form the ESP32 can emit without any JSON library: "2048,123456"
  const text = raw.toString().trim();
  if (!text) return null;

  if (text[0] === '{') {
    try {
      const obj = JSON.parse(text);
      if (typeof obj.v === 'number') {
        return { v: obj.v, t: typeof obj.t === 'number' ? obj.t : Date.now() };
      }
    } catch (err) {
      return null;
    }
    return null;
  }

  const parts = text.split(',');
  if (parts.length >= 1) {
    const v = Number(parts[0]);
    const t = parts.length > 1 ? Number(parts[1]) : Date.now();
    if (Number.isFinite(v) && Number.isFinite(t)) return { v, t };
  }
  return null;
}

wss.on('connection', (ws, req) => {
  clients.add(ws);
  console.log(`[ws] client connected (${req.socket.remoteAddress}) — total: ${clients.size}`);

  ws.on('message', (raw) => {
    const sample = parseIncoming(raw);
    if (!sample) return;

    const payload = JSON.stringify({ type: 'sample', v: sample.v, t: sample.t });
    for (const client of clients) {
      if (client.readyState === client.OPEN) {
        client.send(payload);
      }
    }
  });

  ws.on('close', () => {
    clients.delete(ws);
    console.log(`[ws] client disconnected — total: ${clients.size}`);
  });

  ws.on('error', (err) => {
    console.error('[ws] error:', err.message);
  });
});

server.listen(PORT, () => {
  console.log(`ECG monitor server running at http://localhost:${PORT}`);
  console.log(`WebSocket endpoint: ws://localhost:${PORT}/ws`);
  console.log('Run "npm run simulate" in another terminal to test without hardware.');
});
