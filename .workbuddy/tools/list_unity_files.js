// Enumerate Unity serialized files ("UnityFS" bundles and "UnityWeb/CAB" assets)
// under a folder: counts, total size, and the largest ones.
const fs = require('fs');
const path = require('path');

const root = process.argv[2];
const rows = [];
let total = 0;
let files = 0;

function walk(dir) {
  for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, ent.name);
    if (ent.isDirectory()) {
      walk(p);
      continue;
    }
    files++;
    const st = fs.statSync(p);
    if (st.size < 64) continue;
    let head;
    const fd = fs.openSync(p, 'r');
    try {
      head = Buffer.alloc(8);
      fs.readSync(fd, head, 0, 8, 0);
    } finally {
      fs.closeSync(fd);
    }
    const magic = head.toString('ascii', 0, 7);
    if (magic.startsWith('UnityFS') || magic.startsWith('UnityWeb') || magic.startsWith('UnityRaw')) {
      rows.push({ p, sizeMB: +(st.size / 1048576).toFixed(2), magic: magic.replace(/\0/g, '') });
      total += st.size;
    }
  }
}

walk(root);
rows.sort((a, b) => b.sizeMB - a.sizeMB);
console.log('files walked: ' + files);
console.log('serialized files: ' + rows.length + ', total ' + (total / 1048576).toFixed(1) + ' MB');
for (const r of rows.slice(0, 20)) console.log(r.sizeMB + ' MB  ' + r.magic + '  ' + r.p);
if (process.argv[3] === '--all') for (const r of rows) console.log(r.sizeMB + ' MB  ' + r.p);
