// Control-plane acceptance on final hardware, with UM980s disconnected.
// No generated correction payload is ever sent to a physical UART.
const {chromium}=require('playwright'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const out=path.resolve(__dirname,'../../../tests/2026-09-13-correction-bridge');
const units=[{ip:'192.168.100.20',name:'base',port:'COM4'},{ip:'192.168.100.19',name:'rover',port:'COM10'}];
const save=(name,d)=>fs.writeFileSync(path.join(out,name+'.json'),JSON.stringify(d,null,2));
(async()=>{
 const browser=await chromium.launch({channel:'msedge',headless:true});const contexts=[];
 const get=async(u,endpoint)=>{const r=await u.context.request.get('http://'+u.ip+'/api/v1/'+endpoint);assert(r.ok());return r.json()};
 const post=async(u,body)=>u.page.evaluate(async body=>{const r=await fetch('/api/v1/diagnostic',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:JSON.stringify(body)});return {status:r.status,body:await r.json()}},body);
 // The nonblocking survey mutex can reject an otherwise idle request while
 // its snapshot is being published. Retry only that explicit busy rejection.
 const route=async(u,transport,session=0)=>{
   const before=(await get(u,'diagnostic')).corrections;
   for(let attempt=0;attempt<5;++attempt){
     const response=u.page.waitForResponse(r=>r.request().method()==='POST'&&r.url().endsWith('/api/v1/diagnostic'));
     if(transport==='sik'){if(u.name==='rover')await u.page.locator('#liveSession').fill(String(session));await u.page.locator('#liveStart').click();}
     else await u.page.locator('#liveWifi').click();
     assert.equal((await response).status(),202);
     const requestedAt=(await get(u,'diagnostic')).uptime_ms;
     await u.page.waitForFunction(({transport,session,old,requestedAt})=>{
       const c=state?.corrections;
       return state.uptime_ms>requestedAt+200&&(c?.error||c?.transport===transport&&(transport==='wifi'||(session?c.session===session:c.session!==old)));
     },{transport,session,old:before.session,requestedAt});
     const d=await get(u,'diagnostic');
     if(d.corrections.transport===transport&&(transport==='wifi'||(session?d.corrections.session===session:d.corrections.session!==before.session)))return;
     console.log('Routing rejection',u.name,d.corrections,await u.page.locator('#message').innerText());
     assert.match(d.corrections.error,/Finish the current test, survey or receiver operation first/);
     await u.page.waitForTimeout(300);
   }
   throw Error(u.name+' routing stayed busy');
 };
 try{
  for(const u of units){
   u.context=await browser.newContext({viewport:{width:768,height:1024}});contexts.push(u.context);u.page=await u.context.newPage();u.page.on('response',async r=>{if(r.request().method()==='POST'&&r.url().endsWith('/api/v1/diagnostic'))console.log(u.name,'POST',r.request().postData(),r.status())});
   u.before=await get(u,'survey');u.beforeDiag=await get(u,'diagnostic');
   assert(!u.before.gnss.profile_verified&&!u.before.gnss.position_valid&&!u.before.collection.active&&!u.beforeDiag.busy);
   const previous=JSON.parse(fs.readFileSync(path.join(out,'0.10.3-'+u.name+'-diagnostic.json'),'utf8'));
   assert.deepEqual(u.beforeDiag.last_report,previous.last_report);assert.deepEqual(u.beforeDiag.self_test,previous.self_test);
   await u.page.goto('http://'+u.ip+'/diagnostics');await u.page.waitForFunction(()=>!document.querySelector('#control').disabled);
   await u.page.locator('#control').click();await u.page.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument');
  }
  const [base,rover]=units;
  for(const session of [999999,4294967296,-1,'123456789'])assert.equal((await post(rover,{op:'corrections',transport:'sik',session,confirm:true})).status,409);
  await route(base,'sik');
  const session=(await get(base,'diagnostic')).corrections.session;assert(session>=1000000);
  await route(rover,'sik',session);
  assert.equal((await get(rover,'diagnostic')).corrections.session,session);
  assert.equal((await post(rover,{op:'corrections',transport:'sik',session,confirm:true})).status,202);
  for(const u of units){
   for(const id of ['arm','pairarm','probe','selftest'])assert(await u.page.locator('#'+id).isDisabled());
   assert.equal((await post(u,{op:'selftest',confirm:true})).status,202);
   await u.page.waitForFunction(()=>document.querySelector('#routeError').textContent.includes('before running diagnostics'));
   const d=await get(u,'diagnostic');assert(!d.busy&&d.corrections.transport==='sik');
   assert.equal(d.corrections.output.forwarded,0);assert.deepEqual(d.self_test,u.beforeDiag.self_test);save('live-'+u.name,d);
   await u.page.screenshot({path:path.join(out,'live-'+u.name+'.png'),fullPage:true});
  }
  const status=await get(rover,'status');assert.equal(status.ui_version,'0.10.4');assert.equal(status.link.transport,'SiK RADIO');
  assert.equal(status.link.rssi_dbm,null);assert.equal(status.link.peer_age_ms,null);assert.equal(status.link.correction_state,'waiting_observations');assert(!status.state.ready);save('live-rover-status',status);
  // Return explicitly to Wi-Fi, then select SiK again to verify reboot behavior.
  for(const u of units)await route(u,'wifi');
  await route(base,'sik');
  const next=(await get(base,'diagnostic')).corrections.session;assert.notEqual(next,session);
  await route(rover,'sik',next);
  for(const u of units){
   execFileSync('C:/Users/el_sp/.platformio/penv/Scripts/python.exe',['C:/Users/el_sp/.platformio/packages/tool-esptoolpy/esptool.py','--chip','esp32s3','--port',u.port,'--after','hard_reset','read_mac'],{stdio:'pipe'});
   await u.page.reload();await u.page.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Connected'),null,{timeout:30000});
   const d=await get(u,'diagnostic'),s=await get(u,'survey');
   assert.equal(d.corrections.transport,'wifi');assert.equal(d.corrections.session,0);assert(!d.busy&&!s.collection.active);
   assert.notEqual(s.boot_id,u.before.boot_id);assert.deepEqual(s.jobs,u.before.jobs);assert.equal(s.records_used,u.before.records_used);
   assert.deepEqual(d.last_report,u.beforeDiag.last_report);assert.deepEqual(d.self_test,u.beforeDiag.self_test);
   assert.equal(d.corrections.output.forwarded,0);save('after-restart-'+u.name+'-diagnostic',d);save('after-restart-'+u.name+'-survey',s);
  }
  save('hardware-results',{result:'PASS',firmware:'0.10.4',receivers:'disconnected',checks:['both real pages use takeover without PIN','invalid production sessions rejected','Base creates and Rover selects matching session','live SiK excludes diagnostics in page and server','receiver-unconfirmed readiness stays false; no COM2 correction output','explicit Wi-Fi return','new Base session differs','restart returns to Wi-Fi with no old session','saved jobs, radio reports and local self-tests unchanged'],limitation:'No live UM980 RTCM/RTK recovery or radio payload qualification in this check.'});
  console.log('PASS: both live routing pages, session selection, diagnostic exclusion, restart and saved-data preservation; no GNSS output');
 }finally{for(const u of units)if(u.page)try{await u.page.evaluate(async()=>{if(token)await fetch('/api/v1/control/release',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:'{}'})})}catch{};await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
