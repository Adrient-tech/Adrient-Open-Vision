// Two-client test for protocol adrient.openvision/live@1:
//  1. two clients both receive frames within ~1 s
//  2. after reconnect with resume, the missed frames are replayed in order
//  3. the server survives a client vanishing mid-stream
//  4. server survives garbage messages
import WebSocket from 'ws';

const url = 'ws://localhost:8081/live';
let results = [];
const ok = (name, cond, detail = '') =>
  results.push(`${cond ? 'PASS' : 'FAIL'} ${name}${detail ? ' — ' + detail : ''}`);

function connect(after = null) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    const state = { ws, frames: [], pings: 0, opened: false };
    const timer = setTimeout(() => reject(new Error('connect/open timeout')), 5000);
    ws.on('open', () => { state.opened = true; clearTimeout(timer); resolve(state); });
    ws.on('error', e => { clearTimeout(timer); reject(e); });
    ws.on('message', raw => {
      const m = JSON.parse(raw);
      if (m.type === 'frame' && m.frame) state.frames.push(m.frame);
      if (m.type === 'ping') state.pings++;
    });
    if (after !== null) {
      ws.on('open', () => ws.send(JSON.stringify({ type: 'resume', after })));
    }
  });
}
const sleep = ms => new Promise(r => setTimeout(r, ms));

// 1 + 2: two simultaneous clients get frames quickly
const t0 = Date.now();
const [a, b] = await Promise.all([connect(), connect()]);
await sleep(1200);
ok('client A got frames < 1.2s', a.frames.length > 0, `${a.frames.length} frames in ${Date.now() - t0}ms`);
ok('client B got frames < 1.2s', b.frames.length > 0, `${b.frames.length} frames`);
ok('frames have scene v2 shape', ['frame_id', 'ego', 'path', 'vehicles', 'cipo']
  .every(k => a.frames[0]?.[k] !== undefined));

// 3: resume after reconnect — client A vanishes without close (hard drop)
const lastId = a.frames[a.frames.length - 1]?.frame_id;
a.ws.terminate(); // vanish mid-stream
const a2 = await connect(lastId);
await sleep(800);
const ids = a2.frames.map(f => f.frame_id);
ok('reconnect replays missed frames in order',
  ids.length >= 2 && ids.every((v, i) => i === 0 || v === ids[i - 1] + 1),
  `${ids.length} frames, first=${ids[0]} last=${ids[ids.length - 1]}, resumed after ${lastId}`);
a2.ws.terminate();

// 4: garbage message must not kill the server
const c = await connect();
c.ws.send('not json at all');
c.ws.send(Buffer.from([0x00, 0xff, 0x01]));
await sleep(400);
const stillAlive = await connect().then(() => true).catch(() => false);
ok('server survives garbage + vanished client', stillAlive);
console.log(results.join('\n'));
b.ws.close();
process.exit(results.some(r => r.startsWith('FAIL')) ? 1 : 0);
