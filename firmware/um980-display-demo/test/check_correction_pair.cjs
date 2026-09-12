// Real two-instrument test: browser is only the tablet controller, never the data path.
const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const run=Number(process.env.TOPORTK_RUN||913225),seconds=Number(process.env.TOPORTK_SECONDS||60);
const out=path.resolve(__dirname,'../../../tests/2026-09-12-correction-pair',String(run));
(async()=>{fs.mkdirSync(out,{recursive:true});const browser=await chromium.launch({channel:'msedge',headless:true}),units=[],results=[];
try{
 for(const ip of ['192.168.100.20','192.168.100.19']){
  const context=await browser.newContext({viewport:{width:768,height:1024},acceptDownloads:true}),p=await context.newPage(),url='http://'+ip,errors=[];
  units.push({context,p,url,errors});p.on('pageerror',e=>errors.push(e.message));await p.goto(url+'/diagnostics');await p.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Connected'));
  const u=units.at(-1);u.before=await(await p.request.get(url+'/api/v1/diagnostic')).json();u.survey=await(await p.request.get(url+'/api/v1/survey')).json();assert(!u.before.busy);
  const denied=await p.request.post(url+'/api/v1/diagnostic',{data:{op:'pairtest',run,seconds,confirm:true}});assert.equal(denied.status(),401);
  await p.locator('#control').click();await p.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument');
  await p.locator('#run').fill(String(run));await p.locator('#seconds').selectOption(String(seconds));await p.locator('#confirm').check();await p.locator('#pairarm').click();
  await p.waitForFunction(id=>state?.run===id&&['armed','running'].includes(state.state),run);
 }
 for(const {p} of units)await p.waitForFunction(()=>state?.state==='running');
 if(seconds>=60){
  for(const u of units)await u.context.setOffline(true);
  await new Promise(resolve=>setTimeout(resolve,20000));
  for(const u of units)await u.context.setOffline(false);
 }
 for(const u of units){const {p,url}=u;
  await p.waitForFunction(()=>state?.state==='done'&&state.peer_report_received&&state.persisted,null,{timeout:(seconds+20)*1000});
  const after=await(await p.request.get(url+'/api/v1/diagnostic')).json();assert.equal(after.kind,'paired_rtcm_faults');assert(!after.busy);assert.equal(after.integrity_violations,0);assert.equal(after.tx_expired,0);assert.deepEqual(after.self_test,u.before.self_test);
  const event=p.waitForEvent('download');await p.locator('#download').click();const download=await event;await download.saveAs(path.join(out,after.role.toLowerCase()+'.json'));
  const downloaded=JSON.parse(fs.readFileSync(await download.path(),'utf8'));assert.equal(downloaded.run,run);assert.equal(downloaded.pair_pass,after.pair_pass);
  const survey=await(await p.request.get(url+'/api/v1/survey')).json();for(const key of ['boot_id','role','jobs','active_job','records_used'])assert.deepEqual(survey[key],u.survey[key]);assert.deepEqual(u.errors,[]);
  await p.locator('#result').scrollIntoViewIfNeeded();await p.screenshot({path:path.join(out,after.role.toLowerCase()+'.png')});await p.locator('#release').click();
  results.push({ip:url,role:after.role,boot_id:survey.boot_id,report:after});console.log(after.role+': received '+after.received+'/'+after.expected_rx+', invalid '+after.integrity_violations+', pair_pass '+after.pair_pass);
 }
 fs.writeFileSync(path.join(out,'hardware-results.json'),JSON.stringify({workflow:'PASS',pair_pass:results.every(x=>x.report.pair_pass),browser_offline_seconds:seconds>=60?20:0,results},null,2));
}finally{
 for(const {p,context} of units){await context.setOffline(false).catch(()=>{});await p.evaluate(async()=>{const token=sessionStorage.getItem('diagnosticToken');if(token)await fetch('/api/v1/control/release',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:'{}'})}).catch(()=>{});await context.close()}
 await browser.close();
}})().catch(e=>{console.error(e);process.exitCode=1});
