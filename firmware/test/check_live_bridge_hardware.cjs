// Control-plane acceptance on hardware, with UM980s disconnected.
// No generated correction payload is ever sent to a physical UART.
const {chromium}=require('playwright'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const out=path.resolve(__dirname,'../..',process.env.TOPORTK_TEST_RECORD||'firmware/.pio/r5-live-bridge');
const units=[{ip:'192.168.100.20',name:'base',port:'COM4'},{ip:'192.168.100.19',name:'rover',port:'COM10'}];
const save=(name,d)=>fs.writeFileSync(path.join(out,name+'.json'),JSON.stringify(d,null,2));
(async()=>{
 fs.mkdirSync(out,{recursive:true});
 const browser=await chromium.launch({channel:'msedge',headless:true});
 const get=async(u,endpoint)=>{const r=await u.context.request.get('http://'+u.ip+'/api/v1/'+endpoint);assert(r.ok());return r.json()};
 const post=async(u,body)=>u.page.evaluate(async body=>{const r=await fetch('/api/v1/diagnostic',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:JSON.stringify(body)});return {status:r.status,body:await r.json()}},body);
 const route=async(u,transport)=>{
   const requestedAt=(await get(u,'diagnostic')).uptime_ms;
   const response=u.page.waitForResponse(r=>r.request().method()==='POST'&&r.url().endsWith('/api/v1/diagnostic'));
   await u.page.locator(transport==='sik'?'#liveRadio':'#liveWifi').click();
   assert.equal((await response).status(),202);
   await u.page.waitForFunction(({transport,requestedAt})=>state?.uptime_ms>requestedAt+200&&(state.corrections?.error||state.corrections?.transport===transport),{transport,requestedAt});
   const d=await get(u,'diagnostic');
   assert(!d.corrections.error,JSON.stringify(d.corrections));
   assert.equal(d.corrections.transport,transport);
   // Local selection is not proof of connectivity: select both before awaiting the pair.
 };
 const paired=async(transport,previous=0)=>{
   const deadline=Date.now()+20000;let current;
   while(Date.now()<deadline){
     current=await Promise.all(units.map(u=>get(u,'diagnostic')));
     const [b,r]=current.map(d=>d.corrections);
     if([b,r].every(c=>c.transport===transport&&c.peer_connected===true)&&b.session>=1000000&&b.session===r.session&&b.session!==previous)return current;
     await new Promise(resolve=>setTimeout(resolve,250));
   }
   throw Error('Automatic '+transport+' pair not established: '+JSON.stringify(current));
 };
 try{
  for(const u of units){
   u.context=await browser.newContext({viewport:{width:768,height:1024}});u.page=await u.context.newPage();
   u.before=await get(u,'survey');u.beforeDiag=await get(u,'diagnostic');
   assert(!u.before.gnss.profile_verified&&!u.before.gnss.position_valid&&!u.before.collection.active&&!u.beforeDiag.busy);
   await u.page.goto('http://'+u.ip+'/diagnostics');await u.page.waitForFunction(()=>!document.querySelector('#control').disabled);
   await u.page.locator('#control').click();await u.page.waitForFunction(()=>owner&&online);
  }
  const [,rover]=units;
  // The obsolete manual session key is rejected even for a formerly valid value.
  assert.equal((await post(rover,{op:'corrections',transport:'sik',session:123456789,confirm:true})).status,409);
  for(const u of units)await route(u,'sik');
  const initial=await paired('sik');
  let session=initial[0].corrections.session;
  for(const u of units){
   for(const id of ['arm','pairarm','probe','selftest'])assert(await u.page.locator('#'+id).isDisabled());
   assert.equal((await post(u,{op:'selftest',confirm:true})).status,202);
   await u.page.waitForFunction(()=>!!state?.corrections?.error);
   const d=await get(u,'diagnostic');assert(!d.busy&&d.corrections.transport==='sik');
   assert.equal(d.corrections.output.forwarded,0);assert.deepEqual(d.self_test,u.beforeDiag.self_test);save('live-'+u.name,d);
   await u.page.screenshot({path:path.join(out,'live-'+u.name+'.png'),fullPage:true});
  }
  const status=await get(rover,'status');assert.equal(status.link.transport,'SiK RADIO');
  assert.equal(status.link.rssi_dbm,null);assert(Number.isFinite(status.link.peer_age_ms));assert.equal(status.link.correction_state,'waiting_observations');assert(!status.state.ready);save('live-rover-status',status);
  for(const u of units)await route(u,'wifi');
  await paired('wifi');
  for(const u of units)await route(u,'sik');
  session=(await paired('sik',session))[0].corrections.session;
  // Each individual restart must preserve Radio selection and negotiate a fresh pair.
  for(const u of units){
   execFileSync('C:/Users/el_sp/.platformio/penv/Scripts/python.exe',['C:/Users/el_sp/.platformio/packages/tool-esptoolpy/esptool.py','--chip','esp32s3','--port',u.port,'--after','hard_reset','read_mac'],{stdio:'pipe'});
   await u.page.reload();await u.page.waitForFunction(()=>online,null,{timeout:30000});
   const current=await paired('sik',session);session=current[0].corrections.session;
   const d=current[units.indexOf(u)],s=await get(u,'survey');
   assert.equal(d.corrections.transport,'sik');assert(!d.busy&&!s.collection.active);
   assert.notEqual(s.boot_id,u.before.boot_id);assert.deepEqual(s.jobs,u.before.jobs);assert.equal(s.records_used,u.before.records_used);
   assert.deepEqual(d.last_report,u.beforeDiag.last_report);assert.deepEqual(d.self_test,u.beforeDiag.self_test);
   assert.equal(d.corrections.output.forwarded,0);save('after-restart-'+u.name+'-diagnostic',d);save('after-restart-'+u.name+'-survey',s);
  }
  save('hardware-results',{result:'PASS',receivers:'disconnected',checks:['both real pages use takeover without PIN','manual session key rejected','both local selections establish an automatic current pair','live Radio excludes diagnostics in page and server','receiver-unconfirmed readiness stays false; no COM2 correction output','automatic Wi-Fi pair','Radio preference survives each individual restart with fresh pairing','saved jobs, radio reports and local self-tests unchanged'],limitation:'No live UM980 RTCM/RTK recovery or radio-only startup qualification in this check.'});
  console.log('PASS: automatic pairing on both links, diagnostic exclusion, restart and saved-data preservation; no GNSS output');
 }finally{for(const u of units)if(u.page)try{await u.page.evaluate(async()=>{if(token)await fetch('/api/v1/control/release',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:'{}'})})}catch{};await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
