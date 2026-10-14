// Real TLS HTTP/2 and HTTP/1.1. Shape bytes per physical socket, not per stream.
const http2 = require('node:http2'), fs = require('node:fs'), path = require('node:path');
const readline = require('node:readline'), https = require('node:https');
const [fixtures, sizeText, rateText, sharedText = '0', backgroundText = '0'] = process.argv.slice(2);
const size = Number(sizeText), rate = Number(rateText), block = Buffer.alloc(16384);
const sharedRate = Number(sharedText), backgroundCount = Number(backgroundText), backgroundClients = [];
for (let i = 0; i < block.length; ++i) block[i] = i % 256;
const sockets = new Map(), records = [], sessions = new Set(), accepted = new Set(); let nextId = 0, peakSockets = 0, peakStreams = 0, peakTotalSockets = 0;
const server = http2.createSecureServer({
  key: fs.readFileSync(path.join(fixtures, 'localhost-key.pem')),
  cert: fs.readFileSync(path.join(fixtures, 'localhost-cert.pem')), allowHTTP1: true
});
server.on('sessionError', () => {});
server.on('secureConnection', socket => {accepted.add(socket); socket.on('close', () => accepted.delete(socket));});
server.on('session', session => {sessions.add(session); session.on('close', () => sessions.delete(session));});
server.on('request', (req, res) => {
  const socket = req.httpVersionMajor === 2 ? req.stream.session.socket : req.socket;
  let state = sockets.get(socket);
  if (!state) {state = {id: ++nextId, streams: [], cursor: 0, credit: 0}; sockets.set(socket, state);}
  const background = req.url === '/background', total = background ? 1024 * 1024 * 1024 : size;
  const match = /^bytes=(\d+)-(\d+)$/.exec(req.headers.range || '');
  const first = match ? Number(match[1]) : 0, last = match ? Number(match[2]) : total - 1;
  if (first > last || last >= total) {res.writeHead(416); res.end(); return;}
  res.writeHead(match ? 206 : 200, {'content-type': 'application/octet-stream',
    'content-length': last - first + 1, etag: '"transport-pattern"',
    ...(match ? {'content-range': `bytes ${first}-${last}/${total}`} : {})});
  res.flushHeaders();
  records.push({socket: state.id, protocol: req.httpVersion, first, last, background});
  if (match && first === 0 && last === 0) {res.end(block.subarray(0, 1)); return;}
  const transfer = {res, offset: first, remaining: last - first + 1, blocked: false, background};
  state.streams.push(transfer);
  res.on('drain', () => {transfer.blocked = false;});
  res.on('close', () => {state.streams = state.streams.filter(item => item !== transfer);});
});
let previous = performance.now();
const timer = setInterval(() => {
  const now = performance.now(), elapsed = (now - previous) / 1000; previous = now;
  const active = [...sockets.values()].filter(s => s.streams.length);
  peakTotalSockets = Math.max(peakTotalSockets, active.length);
  peakSockets = Math.max(peakSockets, active.filter(s => s.streams.some(t => !t.background)).length);
  peakStreams = Math.max(peakStreams, active.reduce((sum, s) => sum + s.streams.filter(t => !t.background).length, 0));
  for (const state of active) {
    const socketRate = sharedRate ? Math.min(rate, sharedRate / active.length) : rate;
    state.credit = Math.min(32768, state.credit + elapsed * socketRate);
    let attempts = state.streams.length;
    while (state.credit >= 1024 && attempts-- > 0 && state.streams.length) {
      state.cursor %= state.streams.length; const item = state.streams[state.cursor++];
      if (item.blocked || item.res.destroyed) continue;
      const count = Math.min(16384, item.remaining, Math.floor(state.credit));
      // All assigned ranges start on 256-byte boundaries; handle arbitrary tails too.
      const data = Buffer.allocUnsafe(count);
      for (let i = 0; i < count; ++i) data[i] = (item.offset + i) % 256;
      item.blocked = !item.res.write(data); item.offset += count; item.remaining -= count; state.credit -= count;
      if (!item.remaining) {item.res.end(); state.streams = state.streams.filter(s => s !== item);}
    }
  }
}, 5);
server.listen(0, '127.0.0.1', () => {
  for (let i = 0; i < backgroundCount; ++i) {
    const client = https.get({hostname: 'localhost', family: 4, port: server.address().port,
      path: '/background', agent: false, ca: fs.readFileSync(path.join(fixtures, 'test-ca.pem'))}, res => res.resume());
    client.on('error', () => {}); backgroundClients.push(client);
  }
  const ready = () => {if (records.filter(r => r.background).length === backgroundCount)
      process.stdout.write(JSON.stringify({port: server.address().port}) + '\n');
    else setTimeout(ready, 5);}; ready();
});
readline.createInterface({input: process.stdin}).on('line', line => {
  if (line === 'stats') process.stdout.write(JSON.stringify({peakSockets, peakStreams, peakTotalSockets, records}) + '\n');
  if (line === 'exit') {clearInterval(timer); for (const client of backgroundClients) client.destroy(); for (const session of sessions) session.destroy(); for (const socket of accepted) socket.destroy(); server.close(); process.stdin.destroy();}
});
