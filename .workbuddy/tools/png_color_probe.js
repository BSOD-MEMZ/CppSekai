// Counts pixels matching a target colour range in a PNG, and reports their
// bounding box. Pure Node (zlib is built in), so it works without Pillow -
// the venv here cannot install anything (no pip index reachable).
//
// Usage: node png_color_probe.js <file.png> <r0-r1> <g0-g1> <b0-b1> [minGminusR] [minGminusB] [x0,y0,x1,y1]
//   e.g. node png_color_probe.js shot.png 20-115 150-215 130-195 60 15
// The optional last argument restricts the scan to a rectangle, which is how a
// widget's own colour is told apart from a background that happens to use it.
//
// minGminusR / minGminusB are **>=** tests, so leaving them at 0 is *not* "no
// filter" - it also demands g >= r and g >= b. A pale lavender / off-white
// target (g below b) therefore matches nothing at all and the probe reads as
// "no such colour" (2026-09-22, measuring a reference screenshot's light cards:
// #DDDEE9 came back as 19 px). They are positional, so to actually switch one
// off pass a large negative (`-255`); for a green/teal target the filter is
// what you want and 0 is harmless.
'use strict';
const fs = require('fs');
const zlib = require('zlib');

function decode(file) {
  const d = fs.readFileSync(file);
  let pos = 8, idat = [], w = 0, h = 0, ct = 0;
  while (pos < d.length) {
    const len = d.readUInt32BE(pos);
    const typ = d.toString('ascii', pos + 4, pos + 8);
    const data = d.subarray(pos + 8, pos + 8 + len);
    if (typ === 'IHDR') { w = data.readUInt32BE(0); h = data.readUInt32BE(4); ct = data[9]; }
    else if (typ === 'IDAT') idat.push(data);
    else if (typ === 'IEND') break;
    pos += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const bpp = ({ 0: 1, 2: 3, 4: 2, 6: 4 })[ct];
  const stride = w * bpp;
  const out = Buffer.alloc(w * h * bpp);
  let p = 0;
  for (let y = 0; y < h; y++) {
    const f = raw[p++];
    const line = Buffer.from(raw.subarray(p, p + stride));
    p += stride;
    const prev = y ? out.subarray((y - 1) * stride, y * stride) : Buffer.alloc(stride);
    const cur = out.subarray(y * stride, (y + 1) * stride);
    for (let i = 0; i < stride; i++) {
      const a = i >= bpp ? line[i - bpp] : 0;
      const b = prev[i];
      const c = i >= bpp ? prev[i - bpp] : 0;
      let add = 0;
      if (f === 1) add = a;
      else if (f === 2) add = b;
      else if (f === 3) add = (a + b) >> 1;
      else if (f === 4) {
        const pa = Math.abs(b - c), pb = Math.abs(a - c), pc = Math.abs(a + b - 2 * c);
        add = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
      }
      line[i] = (line[i] + add) & 255;
    }
    line.copy(cur);
  }
  return { w, h, bpp, out };
}

function range(text) {
  const parts = String(text).split('-');
  const lo = parseInt(parts[0], 10);
  const hi = parts.length > 1 ? parseInt(parts[1], 10) : lo;
  return [lo, hi];
}

const [file, rs, gs, bs, minGR, minGB, cropArg] = process.argv.slice(2);
const [r0, r1] = range(rs);
const [g0, g1] = range(gs);
const [b0, b1] = range(bs);
const needGR = minGR ? parseInt(minGR, 10) : 0;
const needGB = minGB ? parseInt(minGB, 10) : 0;

const { w, h, bpp, out } = decode(file);
let cx0 = 0, cy0 = 0, cx1 = w - 1, cy1 = h - 1;
if (cropArg) {
  const c = cropArg.split(',').map((v) => parseInt(v, 10));
  if (c.length === 4) { cx0 = c[0]; cy0 = c[1]; cx1 = c[2]; cy1 = c[3]; }
}
let hit = 0, x0 = 1e9, y0 = 1e9, x1 = -1, y1 = -1;
for (let y = Math.max(0, cy0); y <= Math.min(h - 1, cy1); y++) {
  for (let x = Math.max(0, cx0); x <= Math.min(w - 1, cx1); x++) {
    const o = (y * w + x) * bpp;
    const r = out[o], g = out[o + 1], b = out[o + 2];
    if (r >= r0 && r <= r1 && g >= g0 && g <= g1 && b >= b0 && b <= b1
      && (g - r) >= needGR && (g - b) >= needGB) {
      hit++;
      if (x < x0) x0 = x;
      if (y < y0) y0 = y;
      if (x > x1) x1 = x;
      if (y > y1) y1 = y;
    }
  }
}
const where = cropArg ? ` in ${cropArg}` : '';
console.log(`${file} ${w}x${h} ${bpp}ch${where} -> ${hit} px, bbox ${x0},${y0} - ${x1},${y1}`);
