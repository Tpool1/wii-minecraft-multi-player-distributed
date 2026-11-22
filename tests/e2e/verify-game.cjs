'use strict';

const fs = require('node:fs');
const path = require('node:path');

function readEvents(log) {
  return log.split(/\r?\n/).flatMap((line, index) => {
    const match = line.match(/\[FCX\]\s+([A-Za-z_]+)(?:\s+(.*))?/);
    if (!match) return [];
    const fields = {};
    for (const token of (match[2] || '').matchAll(/(\w+)=([^\s,;]+)/g)) {
      fields[token[1]] = token[2];
    }
    const time = line.match(/^(\d+):(\d+):(\d+)\s/);
    const ms = time ? (Number(time[1])*60 + Number(time[2]))*1000 + Number(time[3]) : null;
    return [{ event: match[1].toUpperCase(), fields, line: index + 1, ms }];
  });
}

function poses(events, event, slot) {
  return events.filter(r => r.event === event && Number(r.fields.slot) === slot)
    .map(r => ({
      x: Number(r.fields.x), y: Number(r.fields.y), z: Number(r.fields.z),
      line: r.line,
    })).filter(p => [p.x, p.y, p.z].every(Number.isFinite));
}

function transferredMotion(local, remote) {
  if (!local.length || !remote.length) return null;
  for (const p of local.slice(1)) {
    if (Math.hypot(p.x - local[0].x, p.z - local[0].z) < 0.35) continue;
    const seen = remote.find(q => Math.hypot(p.x-q.x, p.y-q.y, p.z-q.z) <= 0.35 &&
      Math.hypot(q.x-local[0].x, q.z-local[0].z) >= 0.35);
    if (seen) return { start: local[0], local: p, remote: seen };
  }
  return null;
}

function blocks(events) {
  return events.filter(r => r.event === 'BLOCK').map(r => ({
    fields:r.fields, line:r.line, ms:r.ms,
    values:['x','y','z','type'].map(k => Number(r.fields[k])),
  })).filter(r => r.values.every(Number.isInteger));
}

function verify(logA, logB) {
  const a = readEvents(logA), b = readEvents(logB);
  const checks = [];
  const test = (name, condition, evidence) =>
    checks.push({ name, passed: Boolean(condition), evidence: evidence || null });
  const readyA = a.find(r => r.event === 'READY' && r.fields.slot === '0');
  const readyB = b.find(r => r.event === 'READY' && r.fields.slot === '1');
  test('both genuine clients entered distinct player slots', readyA && readyB,
    { A: readyA || null, B: readyB || null });
  const aMotion = transferredMotion(poses(a,'LOCAL',0), poses(b,'REMOTE',0));
  const bMotion = transferredMotion(poses(b,'LOCAL',1), poses(a,'REMOTE',1));
  test('A moved horizontally and B observed A at the moved position', aMotion, aMotion);
  test('B moved horizontally and A observed B at the moved position', bMotion, bMotion);
  const aBlocks = blocks(a), bBlocks = blocks(b);
  for (const actor of ['A', 'B']) {
    let confirmed = null;
    for (const edit of a.filter(r => r.event === 'EDIT' && r.fields.actor === actor && r.fields.accepted === '1')) {
      const xyz = ['x', 'y', 'z'].map(k => Number(edit.fields[k]));
      if (!xyz.every(Number.isInteger)) continue;
      const validType = type => edit.fields.op === 'dig' ? type === 0 : edit.fields.op === 'place' && type > 0;
      const sameBlock = row => xyz.every((v,i) => v === row.values[i]);
      const host = aBlocks.find(row => row.line > edit.line && sameBlock(row));
      if (!host || !validType(host.values[3]) || edit.ms === null) continue;
      // Same Windows clock in both Dolphin logs. Allow a minute/hour rollover.
      const elapsed = ms => (ms - edit.ms + 3600000) % 3600000;
      const peer = bBlocks.find(row => row.ms !== null && elapsed(row.ms) <= 3000 &&
        sameBlock(row) && row.values[3] === host.values[3]);
      if (peer) {
        confirmed = { edit, values: host.values, hostClientLine: host.line, peerClientLine: peer.line };
        break;
      }
    }
    test(actor + ' submitted an accepted edit and both clients applied its block and type', confirmed, confirmed);
  }
  return { passed: checks.every(c => c.passed), checks,
    eventCounts: { A:a.length, B:b.length }, tolerances:{minHorizontalBlocks:0.35,maxPoseErrorBlocks:0.35} };
}

function offline(log) {
  const events = readEvents(log);
  const local = poses(events, 'LOCAL', 0);
  const ready = events.find(e => e.event === 'READY' && e.fields.slot === '0');
  const moved = local.find(p => Math.hypot(local[0].x - p.x, local[0].z - p.z) >= 0.35);
  return { passed: Boolean(ready && moved), ready, start:local[0], moved };
}

if (require.main === module) {
  if (process.argv.length !== 4) {
    process.stderr.write('Usage: node verify-game.cjs HOST-LOG PEER-LOG | --offline HOST-LOG\n');
    process.exitCode = 2;
  } else {
    const args = process.argv.slice(2);
    const read = p => fs.readFileSync(path.resolve(p), 'utf8');
    const result = args[0] === '--offline' ? offline(read(args[1])) : verify(read(args[0]), read(args[1]));
    console.log(JSON.stringify(result, null, 2));
    process.exitCode = result.passed ? 0 : 1;
  }
}
module.exports = { verify, readEvents };
