// Deliberate upload interruption against a live instrument: send a partial
// package body over a raw socket, then destroy the socket (RST) and verify the
// instrument reports a hard receive error, retains its running firmware, boot
// and records. Never retries automatically; private evidence stays under .pio.
// Usage: node run_ota_interrupt.cjs --interrupt <unit URL> <package.tpk> [partialBytes]
const net=require('node:net'),fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const [flag,base,packagePath,partialArg]=process.argv.slice(2);
if(flag!=='--interrupt'||!base||!packagePath)throw Error('Usage: --interrupt <unit URL> <package.tpk> [partialBytes]');
const partialBytes=Number(partialArg||65536);
const host=base.replace(/^https?:\/\//,'');
const root=path.resolve(__dirname,'..'),dir=path.join(root,'.pio/ota-interrupt-'+Date.now());fs.mkdirSync(dir,{recursive:true});
const bytes=fs.readFileSync(packagePath);assert.equal(bytes.subarray(0,4).toString(),'TPK1');
const client=crypto.randomBytes(16).toString('hex');
const evidence={started:new Date().toISOString(),partialBytes,polls:[]};
const subset=s=>Object.fromEntries(['unit','role','active_job','jobs','records_used','storage_ready'].map(k=>[k,s[k]??null]));
async function get(p){const r=await fetch(base+p,{signal:AbortSignal.timeout(5000)});return {status:r.status,d:await r.json()}}
async function postJSON(p,body,token){const r=await fetch(base+p,{method:'POST',headers:{'Content-Type':'application/json',...(token?{Authorization:'Bearer '+token}:{})},body:JSON.stringify(body),signal:AbortSignal.timeout(5000)});return {status:r.status,d:await r.json().catch(()=>null)}}
// Raw HTTP upload that sends only the first partialBytes of the body, then
// destroys the socket (RST) so the instrument sees a hard receive error.
function rawPartialUpload(pathname,token,body,sendCount){
 return new Promise(resolve=>{
  const [hostName,port]=host.split(':');
  const sock=net.connect(Number(port||80),hostName,()=>{
   const head=`POST ${pathname} HTTP/1.1\r\nHost: ${hostName}\r\nContent-Type: application/octet-stream\r\nContent-Length: ${body.length}\r\nAuthorization: Bearer ${token}\r\nConnection: close\r\n\r\n`;
   sock.write(head);
   sock.write(body.subarray(0,sendCount),()=>{setTimeout(()=>{if(sock.resetAndDestroy)sock.resetAndDestroy();else sock.destroy()},800)});
  });
  let reply='';sock.on('data',c=>{if(reply.length<2048)reply+=c.toString('latin1')});
  sock.on('error',e=>resolve({error:e.code||String(e),reply}));
  setTimeout(()=>{try{sock.destroy()}catch{};resolve({timeout:true,reply})},30000);
 });
}
(async()=>{
 const t0=Date.now();
 const note=(u)=>{const row={ms:Date.now()-t0,state:u.d.state,firmware:u.d.firmware,boot:u.d.boot,received:u.d.received};const last=evidence.polls[evidence.polls.length-1];if(!last||last.state!==row.state||last.received!==row.received)evidence.polls.push(row)};
 try{
  const before=await get('/api/v1/survey');assert.equal(before.d.collection.active,false);evidence.beforeSurvey=subset(before.d);
  const old=await get('/api/v1/update');assert.ok(['idle','failed'].includes(old.d.state),'update state '+old.d.state);evidence.oldBoot=old.d.boot_id;evidence.oldFirmware=old.d.firmware;evidence.oldBootText=old.d.boot;note(old);
  const claim=await postJSON('/api/v1/control',{client});assert.equal(claim.status,200);const token=claim.d.token;
  const prep=await postJSON('/api/v1/update',{op:'prepare',header:bytes.subarray(0,128).toString('hex')},token);assert.equal(prep.status,202);
  let ack=false;for(let i=0;i<30;++i){const u=await get('/api/v1/update');note(u);if(u.d.peer_acknowledged){ack=true;break}await new Promise(r=>setTimeout(r,500))}
  assert.equal(ack,true,'peer preparation acknowledgement missing; no override authorized');evidence.peerAcknowledged=true;
  const started=await postJSON('/api/v1/update',{op:'start',confirm:true},token);assert.equal(started.status,202);
  let u;for(let i=0;i<80;++i){u=await get('/api/v1/update');note(u);if(u.d.state==='ready')break;await new Promise(r=>setTimeout(r,250))}
  assert.equal(u.d.state,'ready');
  const raw=await rawPartialUpload('/api/v1/update/upload',token,bytes,partialBytes);
  evidence.raw=raw.reply?raw.reply.split('\r\n')[0]:raw;
  for(let i=0;i<60;++i){try{u=await get('/api/v1/update');note(u);if(u.d.state==='failed')break}catch{}await new Promise(r=>setTimeout(r,500))}
  assert.equal(u.d.state,'failed');assert.match(u.d.error,/connection closed|receive error|stalled/i);
  evidence.error=u.d.error;evidence.received=u.d.received;evidence.total=u.d.total;
  assert.ok(evidence.received>0&&evidence.received<evidence.total,'partial body not observed');
  const after=await get('/api/v1/survey');assert.equal(JSON.stringify(subset(after.d)),JSON.stringify(evidence.beforeSurvey));
  const fin=await get('/api/v1/update');assert.equal(fin.d.firmware,evidence.oldFirmware);assert.equal(fin.d.boot_id,evidence.oldBoot);assert.equal(fin.d.boot,evidence.oldBootText);note(fin);
  fs.writeFileSync(path.join(dir,'interrupt-evidence.json'),JSON.stringify(evidence,null,2));
  console.log('PASS: interrupted at '+evidence.received+'/'+evidence.total+' bytes ("'+evidence.error+'"); firmware '+evidence.oldFirmware+' retained, boot and records unchanged.');
 }catch(e){evidence.failure=String(e.message||e);fs.writeFileSync(path.join(dir,'interrupt-evidence.json'),JSON.stringify(evidence,null,2));throw e}
})().catch(e=>{console.error(e.message);process.exitCode=1});
