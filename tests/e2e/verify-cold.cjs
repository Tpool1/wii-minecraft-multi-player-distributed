'use strict';
const fs = require('node:fs');
const { readEvents, poses, transferredMotion } = require('./verify-game.cjs');

function snapshots(events, kind) {
  const result = new Map();
  for (const row of events.filter(r => r.event === kind)) {
    const { x, y, z, type } = row.fields;
    if ([x,y,z,type].every(v => v !== undefined && Number.isInteger(Number(v)))) {
      result.set([x,y,z].join(','), Number(type));
    }
  }
  return result;
}

function restoredPose(before, after, slot, source='LOCAL') {
  const select = (events,kind) => events.filter(r => r.event === kind && Number(r.fields.slot) === slot);
  const a = select(before,source).at(-1), b = select(after,'LOCAL')[0];
  if (!a || !b) return { passed:false };
  const distance = Math.hypot(...['x','y','z'].map(k => Number(a.fields[k])-Number(b.fields[k])));
  return { passed: distance <= 0.40, distance, previous:a.fields, cold:b.fields };
}

function verify(warmA, warmB, coldA, coldB) {
  const [a,b,ca,cb] = [warmA,warmB,coldA,coldB].map(readEvents);
  const initial = snapshots(a,'READ_AT');
  const final = snapshots(a,'BLOCK');
  const reloaded = [snapshots(ca,'READ_AT'),snapshots(cb,'READ_AT')];
  const markers = ['8,65,10','8,65,11','10,65,10','10,65,11'].map(key => {
    const old = initial.get(key);
    const expected = final.has(key) ? final.get(key) : old;
    const actual = reloaded.map(m=>m.get(key));
    return { key, initial:old, expected, actual,
      passed: expected !== undefined && actual.every(v=>v===expected) };
  });
  const changed = markers.some(m=>m.expected !== undefined && m.initial !== undefined && m.initial !== m.expected);
  const saves = a.filter(r => r.event==='HOST_SAVE' && r.fields.ok==='1');
  const checkpoint = saves.at(-1);
  const confirmed = checkpoint ? a.filter(r=>r.line<=checkpoint.line) : [];
  const hostPose = restoredPose(confirmed,ca,0);
  const peerPose = restoredPose(confirmed,cb,1,'REMOTE');
  const lastPeerDisplay = restoredPose(b,cb,1);
  return { passed: saves.length>0 && changed && markers.every(m=>m.passed) && hostPose.passed && peerPose.passed,
    saves, changedMarker:changed, markers, hostPose, peerPose, lastPeerDisplay };
}
if (require.main===module) {
  const args=process.argv.slice(2);
  const read=p=>fs.readFileSync(p,'utf8');
  let result;
  if ((args.length===3 || args.length===4) && args[0]==='--warm') {
    const after=readEvents(read(args[2]));
    result=restoredPose(readEvents(read(args[1])),after,1);
    result.passed=result.passed && after.some(r=>r.event==='READY' && r.fields.slot==='1');
    if (args[3]) {
      const host=readEvents(read(args[3]));
      const ready=after.find(r=>r.event==='READY' && r.fields.slot==='1');
      const age=r=>ready && ready.ms!==null && r.ms!==null ? (ready.ms-r.ms+3600000)%3600000 : Infinity;
      // Align by the replacement's actual READY, even when reviewing a sealed log
      // containing later reconnects or a host shutdown.
      const link=host.filter(r=>r.event==='LINK' && r.fields.connected==='1' && age(r)<=120000)
        .sort((a,b)=>age(a)-age(b))[0];
      const close=link && host.find(r=>r.line>link.line && r.event==='LINK' && r.fields.connected==='0');
      const seen=link ? host.filter(r=>r.line>link.line && (!close || r.line<close.line)) : [];
      const checkpoint=link && host.findLast(r=>r.line<link.line && r.event==='PEER_SAVE' && r.fields.ok==='1');
      const confirmed=checkpoint ? host.filter(r=>r.line<=checkpoint.line) : [];
      const lastPeerDisplay=result;
      result=restoredPose(confirmed,after,1,'REMOTE');
      result.lastPeerDisplay=lastPeerDisplay;
      result.newPeerReady=after.some(r=>r.event==='READY' && r.fields.slot==='1');
      result.passed=result.passed && result.newPeerReady;
      result.resumedMotion=transferredMotion(poses(after,'LOCAL',1),poses(seen,'REMOTE',1));
      result.passed=result.passed && Boolean(result.resumedMotion);
    }
  } else if (args.length===4) {
    result=verify(...args.map(read));
  } else throw new Error('Usage: node verify-cold.cjs WARM-A WARM-B COLD-A COLD-B | --warm OLD-B NEW-B');
  console.log(JSON.stringify(result,null,2));
  process.exitCode=result.passed ? 0 : 1;
}
module.exports={verify};
