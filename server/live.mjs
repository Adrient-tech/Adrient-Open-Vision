// Adrient OpenVision — realtime broadcast server (protocol adrient.openvision/live@1).
//
// Serves the 3D viewer statically and streams world-model frames to any number
// of WebSocket clients at /live.
//
// Sources (pick one):
//   --replay <scene.json>       loop a recorded scene file as a live feed
//   --tail <world_state.jsonl>  follow a world-state file as it is appended
//                               (start the engine with the same file to go live)
//
// Protocol v1:
//   server → client on connect : {type:"hello", protocol:"adrient.openvision/live@1", fps}
//   client → server (optional) : {type:"resume", after:<frame_id>}  replay missed frames
//   server → client            : {type:"frame", frame:{...scene v2 frame}}
//   server → client every 5 s  : {type:"ping", t}   client answers {type:"pong"}
//   a client that misses 2 pongs or buffers > 1 MiB is dropped (backpressure)
//
// Usage: node live.mjs [--port 8081] (--replay <scene.json> | --tail <world_state.jsonl> [fps])
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';

const PROTOCOL = 'adrient.openvision/live@1';
const args = process.argv.slice(2);
const port = +(args.includes('--port') ? args[args.indexOf('--port') + 1] : 8081);
const replayIdx = args.indexOf('--replay');
const tailIdx = args.indexOf('--tail');
const fps = tailIdx >= 0 && args[tailIdx + 2] ? +args[tailIdx + 2] : 10;
const viewerDir = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'viewer');

if (replayIdx < 0 && tailIdx < 0) {
  console.error('usage: node live.mjs [--port 8081] (--replay <scene.json> | --tail <world_state.jsonl> [fps])');
  process.exit(1);
}

// ── world_state.jsonl line → scene v2 frame (mirrors worldmodel/src/bin/adapter.rs)
const KIND = { 1: ['person', 0.6, 0.6, 1.7], 2: ['truck', 2.5, 8.0, 3.5] };
const kindFor = cls => KIND[cls] || ['vehicle', 2.0, 4.6, 1.6];
const r2 = x => Math.round(x * 100) / 100;
function worldFrameToScene(line) {
  const wf = JSON.parse(line);
  const xMax = 60, steps = 24;
  const path = [];
  for (let i = 0; i <= steps; i++) {
    const x = (xMax * i) / steps;
    path.push([r2(x), r2(wf.path.a * x * x + wf.path.b * x + wf.path.c)]);
  }
  return {
    t: wf.frame_id / fps,
    frame_id: wf.frame_id,
    ego: { x: r2(wf.ego?.x ?? 0), v: r2(wf.ego?.v ?? 0) },
    path,
    vehicles: wf.tracks.filter(tr => tr.state !== 'deleted').map(tr => {
      const [kind, w, l, h] = kindFor(tr.cls);
      return { id: tr.id, cls: tr.cls, kind, w, l, h,
               x: r2(tr.x), y: r2(tr.y), vx: r2(tr.vx), vy: r2(tr.vy), state: tr.state };
    }),
    cipo: { valid: !!wf.cipo?.valid, track_id: wf.cipo?.track_id ?? null,
            d: wf.cipo?.d ?? 0, v_rel: wf.cipo?.v_rel ?? 0, cut_in: !!wf.cipo?.cut_in },
  };
}

// ── Hub: ring buffer for resume + client set
const BUFFER_MAX = 600; // one minute at 10 fps
const buffer = [];
let latestFrameId = -1;
const MAX_CLIENTS = 50;

function broadcast(frame, { allowRewind = false } = {}) {
  // Idempotency: never regress — except a replay loop wrapping to its start.
  if (allowRewind && frame.frame_id <= latestFrameId) {
    buffer.length = 0;
    latestFrameId = -1;
  } else if (frame.frame_id <= latestFrameId) {
    return;
  }
  latestFrameId = frame.frame_id;
  buffer.push(frame);
  if (buffer.length > BUFFER_MAX) buffer.shift();
  for (const ws of wss.clients) {
    if (ws.readyState !== 1) continue;
    if (ws.bufferedAmount > 1 << 20) { ws.terminate(); continue; } // slow consumer
    ws.send(JSON.stringify({ type: 'frame', frame }));
  }
}

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json' };
const server = http.createServer((req, res) => {
  const p = req.url === '/' ? '/index.html' : req.url.split('?')[0];
  if (p === '/live') {
    // Plain GET /live (no WebSocket upgrade): endpoint info page, not a 404.
    res.writeHead(200, { 'content-type': 'text/html' });
    res.end(`<!DOCTYPE html><html><head><meta charset="utf-8"><title>OpenVision live endpoint</title>
<style>body{background:#0b0e14;color:#e8edf5;font:15px/1.5 system-ui,sans-serif;max-width:640px;margin:60px auto;padding:0 20px}
code{background:#161c29;padding:2px 6px;border-radius:4px}</style></head><body>
<h1>OpenVision live endpoint</h1>
<p>This URL is a WebSocket endpoint, protocol <code>${PROTOCOL}</code>.
The 3D viewer connects here automatically — open <a style="color:#4da3ff" href="/">the Live Instrument</a> and press <b>Go live</b>.</p>
<p>To connect directly: <code>ws://host:${port}/live</code>.
Messages: <code>hello</code>, <code>frame</code>, <code>ping</code> from the server;
<code>resume {after}</code> and <code>pong</code> from the client.
On reconnect send <code>{"type":"resume","after":&lt;last frame_id&gt;}</code> to replay what you missed.</p>
</body></html>`);
    return;
  }
  const file = path.normalize(path.join(viewerDir, p));
  if (!file.startsWith(viewerDir) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    res.writeHead(404).end('not found');
    return;
  }
  res.writeHead(200, { 'content-type': MIME[path.extname(file)] ?? 'application/octet-stream' });
  fs.createReadStream(file).pipe(res);
});

const wss = new WebSocketServer({ server, path: '/live' });
wss.on('connection', ws => {
  if (wss.clients.size > MAX_CLIENTS) { ws.close(1013, 'too many clients'); return; }
  ws.isAlive = true;
  ws.on('pong', () => { ws.isAlive = true; });
  ws.on('message', data => {
    let m;
    try { m = JSON.parse(data); } catch { return; }
    if (m.type === 'pong') { ws.isAlive = true; return; }
    if (m.type === 'resume' && Number.isFinite(m.after)) {
      for (const f of buffer) if (f.frame_id > m.after) ws.send(JSON.stringify({ type: 'frame', frame: f }));
    }
  });
  ws.send(JSON.stringify({ type: 'hello', protocol: PROTOCOL, fps }));
  if (buffer.length) ws.send(JSON.stringify({ type: 'frame', frame: buffer[buffer.length - 1] }));
});

// heartbeat: drop clients that missed 2 pongs
setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.isAlive) { ws.terminate(); continue; }
    ws.isAlive = false;
    if (ws.readyState === 1) ws.send(JSON.stringify({ type: 'ping', t: Date.now() }));
  }
}, 5000);

server.listen(port, () => console.log(`OpenVision live: http://localhost:${port} (protocol ${PROTOCOL})`));

// ── Sources
if (replayIdx >= 0) {
  const scene = JSON.parse(fs.readFileSync(args[replayIdx + 1], 'utf8'));
  const period = 1000 / scene.fps;
  let i = 0;
  let prevId = -1;
  setInterval(() => {
    const f = scene.frames[i];
    broadcast(f, { allowRewind: f.frame_id < prevId });
    prevId = f.frame_id;
    i = (i + 1) % scene.frames.length;
  }, period);
} else {
  const file = args[tailIdx + 1];
  let pos = fs.existsSync(file) ? fs.statSync(file).size : 0; // follow only new frames
  fs.watchFile(file, { interval: 100 }, () => {
    const size = fs.existsSync(file) ? fs.statSync(file).size : 0;
    if (size < pos) pos = 0; // truncated/rewritten
    if (size <= pos) return;
    const fd = fs.openSync(file, 'r');
    const chunk = Buffer.alloc(size - pos);
    fs.readSync(fd, chunk, 0, chunk.length, pos);
    fs.closeSync(fd);
    pos = size;
    for (const line of chunk.toString().split('\n')) {
      if (line.trim()) {
        try { broadcast(worldFrameToScene(line)); } catch (e) { console.error('bad frame:', e.message); }
      }
    }
  });
}
