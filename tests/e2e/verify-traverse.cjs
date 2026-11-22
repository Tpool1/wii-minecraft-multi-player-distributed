'use strict';
const fs = require('node:fs');
const { readEvents } = require('./verify-game.cjs');

function verify(logA, logB) {
  const a = readEvents(logA), b = readEvents(logB);
  const views = a.filter(e => e.event === 'INTEREST').map(e =>
    Object.fromEntries(Object.entries(e.fields).map(([k,v]) => [k, Number(v)])));
  const full = v => v.a === 25 && v.b === 25;
  const together = views.find(v => full(v) && v.shared > 0);
  const apart = views.find(v => full(v) && v.shared === 0 && v.resident === 50 &&
    (Math.abs(v.ax - v.bx) >= 5 || Math.abs(v.az - v.bz) >= 5));
  const observer = (events, slot) => events.filter(e => e.event === 'REMOTE' && Number(e.fields.slot) === slot);
  const crossed = (events, slot) => {
    const rows = observer(events, slot);
    if (!rows.length) return false;
    return rows.some(e => Math.hypot(Number(e.fields.x) - Number(rows[0].fields.x),
                                       Number(e.fields.z) - Number(rows[0].fields.z)) >= 16);
  };
  const overflow = views.find(v => v.a > 25 || v.b > 25 || v.resident > 50);
  const hostSawTravel = crossed(a, 1), peerSawTravel = crossed(b, 0);
  return { passed: !!together && !!apart && !overflow && hostSawTravel && peerSawTravel,
    samples: views.length, together, apart, overflow, hostSawTravel, peerSawTravel };
}
if (require.main === module) {
  const report = verify(...process.argv.slice(2).map(f => fs.readFileSync(f, 'utf8')));
  console.log(JSON.stringify(report, null, 2));
  process.exitCode = report.passed ? 0 : 1;
}
module.exports = { verify };
