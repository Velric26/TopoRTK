// Real two-instrument test: one instrument requests the paired RTCM test through
// the settings surface, both of them run it, and the browser is only the tablet
// controller, never the data path. No test code is typed anywhere.
const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const seconds=Number(process.env.TOPORTK_SECONDS||60),profile=process.env.TOPORTK_PROFILE||'clean';
assert(['clean','injected'].includes(profile));
const stamp=new Date().toISOString().replace(/[:.]/g,'-');
const out=path.resolve(__dirname,'../..',process.env.TOPORTK_TEST_RECORD||'tests/2026-09-12-correction-uart',process.env.TOPORTK_RUN||stamp);
(async()=>{fs.mkdirSync(out,{recursive:true});const browser=await chromium.launch({channel:'msedge',headless:true}),units=[],results=[];
try{
 for(const ip of ['192.168.100.20','192.168.100.19']){
  const context=await browser.newContext({viewport:{width:768,height:1024},acceptDownloads:true}),p=await context.newPage(),url='http://'+ip,errors=[];
  units.push({context,p,url,errors});p.on('pageerror',e=>errors.push(e.message));await p.goto(url+'/diagnostics');await p.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Connected'));
  const u=units.at(-1);u.before=await(await p.request.get(url+'/api/v1/diagnostic')).json();u.survey=await(await p.request.get(url+'/api/v1/survey')).json();assert(!u.before.busy);
  // A request without control, a bad profile and a combination the tested medium
  // does not offer are all refused before anything runs.
  const unauthorised=await p.request.post(url+'/api/v1/settings',{data:{id:'a'.repeat(32),revision:0,op:'link.test',transport:'sik',seconds,confirm:true}});assert.equal(unauthorised.status(),401);
  await p.locator('#control').click();await p.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument');
  const token=await p.evaluate(()=>sessionStorage.getItem('topoControlToken'));
  u.revision=(await(await p.request.get(url+'/api/v1/settings')).json()).revision;
  const post=(id,data)=>p.request.post(url+'/api/v1/settings',{headers:{Authorization:'Bearer '+token},data:{id,revision:u.revision,op:'link.test',transport:'sik',seconds,confirm:true,...data}});
  const badProfile=await post('b'.repeat(32),{profile:'unknown'});assert.equal(badProfile.status(),400);assert.equal((await badProfile.json()).error,'profile_required');
  const badCombination=await post('c'.repeat(32),{mode:1});assert.equal(badCombination.status(),400);assert.equal((await badCombination.json()).error,'request_refused');
  assert(!(await(await p.request.get(url+'/api/v1/diagnostic')).json()).busy);
 }
 // One instrument asks for the test; the other one finds out from the operation.
 const rover=units[1], roverUrl=rover.url;
 await rover.p.locator('#confirm').check();await rover.p.locator('#transport').selectOption('sik');await rover.p.locator('#seconds').selectOption(String(seconds));await rover.p.locator('#profile').selectOption(profile);
 await rover.p.locator('#start').click();
 await rover.p.waitForFunction(()=>settings?.operation?.kind==='test');
 const requested=(await(await rover.p.request.get(roverUrl+'/api/v1/settings')).json()).operation;
 assert.equal(requested.transport,'sik');assert.equal(requested.seconds,seconds);assert.equal(requested.profile,profile==='injected'?1:0);
 assert.equal(requested.rate,1000);assert.equal(requested.mode,0);assert.match(requested.id,/^[0-9a-f]{32}$/);
 const baseUrl=units[0].url;
 await units[0].p.waitForFunction(()=>settings?.operation?.kind==='test');
 assert.equal((await(await units[0].p.request.get(baseUrl+'/api/v1/settings')).json()).operation.transport,'sik');
 assert.equal((await(await units[0].p.request.get(baseUrl+'/api/v1/settings')).json()).operation.id,'');  // the id belongs to the instrument that asked
 console.log('Paired '+profile+' RTCM test requested from '+roverUrl+' over '+seconds+' s; both instruments adopted it');
 if(seconds>=60){
  for(const u of units)await u.context.setOffline(true);
  await new Promise(resolve=>setTimeout(resolve,20000));
  for(const u of units)await u.context.setOffline(false);
 }
 const reports=[];
 for(const u of units){const {p,url}=u;
  await p.waitForFunction(()=>state?.state==='done'&&state.peer_report_received&&state.persisted,null,{timeout:(seconds+90)*1000});
  const after=await(await p.request.get(url+'/api/v1/diagnostic')).json();fs.writeFileSync(path.join(out,u.before.role.toLowerCase()+'-snapshot.json'),JSON.stringify(after,null,2));
  const operation=(await(await p.request.get(url+'/api/v1/settings')).json()).operation;
  assert.equal(after.kind,'paired_rtcm_faults');assert.equal(after.profile,profile);assert(!after.busy);assert.equal(after.integrity_violations,0);assert.equal(after.tx_expired,0);assert.deepEqual(after.self_test,u.before.self_test);assert(after.uart.observed);for(const key of ['fifo_overflow','buffer_full','frame_errors','parity_errors','breaks','rx_bytes','tx_bytes','rx_backlog_peak','max_service_gap_ms','tx_wait_polls','short_writes'])assert(Number.isInteger(after.uart[key])&&after.uart[key]>=0);
  if(profile==='clean')for(const key of ['injected_drops','injected_corruptions','injected_duplicates'])assert.equal(after[key],0);
  // The operation settled from both peers' own verdicts, on the tested medium.
  assert(['succeeded','failed'].includes(operation.state));assert.equal(operation.transport,'sik');assert.equal(operation.seconds,seconds);
  const event=p.waitForEvent('download');await p.locator('#download').click();const download=await event;await download.saveAs(path.join(out,after.role.toLowerCase()+'.json'));
  const downloaded=JSON.parse(fs.readFileSync(await download.path(),'utf8'));assert.equal(downloaded.run,after.run);assert.equal(downloaded.pair_pass,after.pair_pass);
  const survey=await(await p.request.get(url+'/api/v1/survey')).json();for(const key of ['boot_id','role','jobs','active_job','records_used'])assert.deepEqual(survey[key],u.survey[key]);assert.deepEqual(u.errors,[]);
  await p.locator('#result').scrollIntoViewIfNeeded();await p.screenshot({path:path.join(out,after.role.toLowerCase()+'.png')});await p.locator('#release').click();
  results.push({ip:url,role:after.role,boot_id:survey.boot_id,run:after.run,operation,report:after});console.log(after.role+': '+profile+', run '+after.run+', received '+after.received+'/'+after.expected_rx+', invalid '+after.integrity_violations+', pair_pass '+after.pair_pass+', operation '+operation.state+'/'+operation.reason+', UART '+JSON.stringify(after.uart));
 }
 // One device-owned id: both instruments ran the same test without sharing a number.
 assert.equal(results[0].run,results[1].run);
 fs.writeFileSync(path.join(out,'hardware-results.json'),JSON.stringify({workflow:'PASS',profile,seconds,conditions:process.env.TOPORTK_CONDITIONS||'not recorded',run:results[0].run,pair_pass:results.every(x=>x.report.pair_pass),browser_offline_seconds:seconds>=60?20:0,results},null,2));
}finally{
 for(const {p,context} of units){await context.setOffline(false).catch(()=>{});await p.evaluate(async()=>{const token=sessionStorage.getItem('topoControlToken');if(token)await fetch('/api/v1/control/release',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:'{}'})}).catch(()=>{});await context.close()}
 await browser.close();
}})().catch(e=>{console.error(e);process.exitCode=1});
