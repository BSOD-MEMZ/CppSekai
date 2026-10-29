// Scan serialized Unity files for ASCII needles (prefab names) to find which
// .assets / .bundle file actually holds them. Returns file -> matched needles.
const fs = require('fs');
const path = require('path');

const root = process.argv[2];
const needles = process.argv.slice(3).map((s) => Buffer.from(s, 'ascii'));
const hits = [];

function walk(dir) {
  for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, ent.name);
    if (ent.isDirectory()) {
      walk(p);
      continue;
    }
    const st = fs.statSync(p);
    if (st.size < 4096 || st.size > 1500 * 1024 * 1024) continue;
    let buf;
    try {
      buf = fs.readFileSync(p);
    } catch (e) {
      continue;
    }
    const found = [];
    for (let i = 0; i < needles.length; i++) {
      if (buf.indexOf(needles[i]) >= 0) found.push(needles[i].toString('ascii'));
    }
    if (found.length) {
      hits.push({ file: p, sizeMB: +(st.size / 1048576).toFixed(1), found });
      console.log(p + '  ' + (st.size / 1048576).toFixed(1) + 'MB  -> ' + found.join(', '));
    }
  }
}

walk(root);
if (!hits.length) console.log('no hits');
