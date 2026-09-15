// Deliberate rollback acceptance against a live instrument: flash a test image
// that cannot pass boot acceptance and verify the bootloader falls back to the
// previous firmware without operator intervention. Never retries automatically;
// private evidence stays under .pio.
// Usage: node run_ota_rollback.cjs --rollback <unit URL> <peer URL> <package.tpk> <stockVersion>
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const [flag,base,peer,packagePath,stockVersion]=process.argv.slice(2);
if(flag!=='--rollback'||!base||!peer||!packagePath||!stockVersion)throw Error('Usage: --rollback <unit URL> <peer URL> <package.tpk> <stockVersion>');
const root=path.resolve(__dirname,'..'),dir=path.join(root,'.pio/ota-rollback-'+Date.now());fs.mkdirSync(dir,{recursive:true});
const bytes=fs.readFileSync(packagePath);assert.equal(bytes.subarray(0,4).toString(),'TPK1');
const testVersion=bytes.subarray(48,80).toString('utf8').replace(/\0+$/,'');
const client=crypto.randomBytes(16).toString('hex');
const evidence={started:new Date().toISOString(),testVersion,stockVersion,peer:[],phases:[]};
const subset=s=>Object.fromEntries(['unit','role','active_job','jobs','records_used','storage_ready'].map(k=>[k,s[k]??null]));
async function get(p){const r=await fetch(p,{signal:AbortSignal.timeout(5000)});return {status:r.status,d:await r.json()}}
async function postJSON(p,body,token){const r=await fetch(base+p,{method:'POST',headers:{'Content-Type':'application/json',...(token?{Authorization:'Bearer '+token}:{})},body:JSON.stringify(body),signal:AbortSignal.timeout(5000)});return {status:r.status,d:await r.json().catch(()=>null)}}
async function postRaw(p,body,token){const r=await fetch(base+p,{method:'POST',headers:{'Content-Type':'application/octet-stream',Authorization:'Bearer '+token},body});return {status:r.status,d:await r.text()}}
(async()=>{
 const t0=Date.now();
 try{
  const before=await get(base+'/api/v1/survey');assert.equal(before.d.collection.active,false);evidence.beforeSurvey=subset(before.d);
  const old=await get(base+'/api/v1/update');assert.ok(['idle','failed'].includes(old.d.state),'update state '+old.d.state);evidence.oldBoot=old.d.boot_id;evidence.oldFirmware=old.d.firmware;
  const claim=await postJSON('/api/v1/control',{client});assert.equal(claim.status,200);const token=claim.d.token;
  const prep=await postJSON('/api/v1/update',{op:'prepare',header:bytes.subarray(0,128).toString('hex')},token);assert.equal(prep.status,202);
  let ack=false;for(let i=0;i<30;++i){const u=await get(base+'/api/v1/update');if(u.d.peer_acknowledged){ack=true;break}await new Promise(r=>setTimeout(r,500))}
  assert.equal(ack,true,'peer preparation acknowledgement missing; no override authorized');evidence.peerAcknowledged=true;
  const started=await postJSON('/api/v1/update',{op:'start',confirm:true},token);assert.equal(started.status,202);
  let u;for(let i=0;i<80;++i){u=await get(base+'/api/v1/update');if(u.d.state==='ready')break;await new Promise(r=>setTimeout(r,250))}
  assert.equal(u.d.state,'ready');
  const up=await postRaw('/api/v1/update/upload',bytes,token);assert.equal(up.status,200);
  evidence.uploadAcceptedAt=new Date().toISOString();
  // Poll through the pending-verify window and the rollback reboot. Connection
  // errors are expected while the unit is down (fully so in the hang build).
  for(let i=0;i<160;++i){
   try{
    const s=await get(base+'/api/v1/update');
    const last=evidence.phases[evidence.phases.length-1];
    if(!last||last.boot_id!==s.d.boot_id||last.boot!==s.d.boot)evidence.phases.push({at:Math.round(Date.now()-t0),boot_id:s.d.boot_id,firmware:s.d.firmware,boot:s.d.boot,state:s.d.state});
    if(s.d.boot_id!==evidence.oldBoot&&/rollback/i.test(s.d.boot))break;
   }catch{}
   await new Promise(r=>setTimeout(r,2000));
  }
  const fin=await get(base+'/api/v1/update');
  assert.equal(fin.d.firmware,stockVersion,'running firmware is not the stock image after rollback');
  assert.match(fin.d.boot,/Previous firmware restored \(rollback\)/);
  assert.equal(fin.d.state,'idle');assert.equal(fin.d.locked,false);
  evidence.final={firmware:fin.d.firmware,boot:fin.d.boot,state:fin.d.state};
  const debug=await get(base+'/api/v1/debug');evidence.debugAfter=debug.d.enabled;
  const after=await get(base+'/api/v1/survey');assert.equal(JSON.stringify(subset(after.d)),JSON.stringify(evidence.beforeSurvey));
  try{const p=await get(peer+'/api/v1/update');evidence.peerStatus=p.d.peer_status||''}catch{evidence.peerStatus='unreachable'}
  fs.writeFileSync(path.join(dir,'rollback-evidence.json'),JSON.stringify(evidence,null,2));
  console.log('PASS: pending test boot '+testVersion+' failed acceptance; bootloader restored '+stockVersion+'; Debug '+(evidence.debugAfter?'On':'Off')+'; records unchanged. Phases: '+evidence.phases.map(p=>p.firmware+'/'+p.boot).join(' -> '));
 }catch(e){evidence.failure=String(e.message||e);fs.writeFileSync(path.join(dir,'rollback-evidence.json'),JSON.stringify(evidence,null,2));throw e}
})().catch(e=>{console.error(e.message);process.exitCode=1});
