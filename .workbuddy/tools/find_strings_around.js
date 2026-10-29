// Print every identifier-ish string containing a needle (surrounding printable run).
const fs = require('fs');

const file = process.argv[2];
const needle = Buffer.from(process.argv[3], 'latin1');
const buf = fs.readFileSync(file);

function printable(c) {
  return c >= 0x20 && c < 0x7f;
}

const seen = new Set();
let from = 0;
for (;;) {
  const i = buf.indexOf(needle, from);
  if (i < 0) break;
  from = i + 1;
  let a = i;
  let b = i + needle.length;
  while (a > 0 && printable(buf[a - 1])) a--;
  while (b < buf.length && printable(buf[b])) b++;
  seen.add(buf.toString('latin1', a, b));
}
for (const s of [...seen].sort()) console.log(s);
