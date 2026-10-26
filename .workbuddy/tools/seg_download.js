// Segmented, resumable downloader for large files behind a rate-limited GitHub proxy.
// Reason it exists: this machine's direct path to GitHub release CDN gets reset, and
// gh-proxy.com throttles a single connection to ~100-400 KB/s (543 MB = 1.5 h).
// Splitting into ranges and running 6-10 curl processes gets ~1-2.5 MB/s.
//
// Usage:
//   node .workbuddy/tools/seg_download.js <url> <outFile> <totalBytes> [concurrency] [partMB]
//
// Notes:
// - `--max-filesize <partLen+1>` is mandatory: the proxy sometimes ignores Range and
//   replies 200 with the whole file, which otherwise fills a 16 MB part with 391 MB
//   of garbage before we notice.
// - Parts are cached in <outFile>.parts, so re-running resumes. The parts directory is
//   removed only after the assembled size matches <totalBytes>.
// - No checksum here: pass the official one to a separate check when the release has it.
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const url = process.argv[2];
const out = process.argv[3];
const total = Number(process.argv[4]);
const concurrency = Number(process.argv[5] || 8);
const partMB = Number(process.argv[6] || 16);

if (!url || !out || !total) {
  console.error('usage: node seg_download.js <url> <outFile> <totalBytes> [concurrency] [partMB]');
  process.exit(2);
}

const partSize = partMB * 1024 * 1024;
const dir = out + '.parts';
fs.mkdirSync(dir, { recursive: true });

const parts = [];
for (let start = 0, i = 0; start < total; start += partSize, i++) {
  const end = Math.min(start + partSize, total) - 1;
  parts.push({ i, start, end, file: path.join(dir, `p${String(i).padStart(4, '0')}`) });
}

const len = (p) => p.end - p.start + 1;

function partDone(p) {
  try { return fs.statSync(p.file).size === len(p); } catch { return false; }
}

function fetchPart(p, attempt = 0) {
  return new Promise((resolve) => {
    if (partDone(p)) return resolve({ ok: true, cached: true });
    const tmp = p.file + '.tmp';
    const want = len(p);
    const args = ['-sSL', '--retry', '2', '--retry-all-errors', '-m', '900',
      '--connect-timeout', '20', '--max-filesize', String(want + 1),
      '-r', `${p.start}-${p.end}`, '-o', tmp, url];
    const child = spawn('curl.exe', args, { stdio: 'ignore' });
    child.on('close', (code) => {
      let size = -1;
      try { size = fs.statSync(tmp).size; } catch { }
      if (code === 0 && size === want) {
        fs.renameSync(tmp, p.file);
        return resolve({ ok: true });
      }
      try { fs.unlinkSync(tmp); } catch { }
      if (attempt < 14) {
        const delay = 300 + attempt * 500;
        return setTimeout(() => fetchPart(p, attempt + 1).then(resolve), delay);
      }
      resolve({ ok: false, code, size, want });
    });
  });
}

(async () => {
  let next = 0, done = 0, failed = 0;
  const started = Date.now();
  const ticker = setInterval(() => {
    let have = 0;
    for (const p of parts) { try { have += fs.statSync(p.file).size; } catch { } }
    const sec = (Date.now() - started) / 1000;
    process.stdout.write(`  ${(have / 1048576).toFixed(1)} / ${(total / 1048576).toFixed(0)} MB` +
      `  ${(have / 1048576 / sec).toFixed(1)} MB/s avg  parts ${done}/${parts.length}\n`);
  }, 5000);

  async function worker() {
    while (true) {
      const idx = next++;
      if (idx >= parts.length) return;
      const r = await fetchPart(parts[idx]);
      if (r.ok) done++; else { failed++; console.error('PART FAILED', parts[idx].i, JSON.stringify(r)); }
    }
  }
  await Promise.all(Array.from({ length: concurrency }, worker));
  clearInterval(ticker);

  if (failed) { console.error(`FAILED: ${failed} part(s) incomplete`); process.exit(1); }

  const ws = fs.createWriteStream(out);
  for (const p of parts) {
    await new Promise((res, rej) => {
      const rs = fs.createReadStream(p.file);
      rs.on('error', rej);
      rs.on('end', res);
      rs.pipe(ws, { end: false });
    });
  }
  await new Promise((res) => ws.end(res));
  const size = fs.statSync(out).size;
  console.log(`assembled ${out} ${size} bytes ${size === total ? 'OK' : 'SIZE MISMATCH'}`);
  if (size === total) fs.rmSync(dir, { recursive: true, force: true });
  process.exit(size === total ? 0 : 1);
})();
