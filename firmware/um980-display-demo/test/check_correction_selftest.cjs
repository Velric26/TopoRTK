// Local transport algorithm only: no synthetic RF or receiver input is generated.
const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const out=path.resolve(__dirname,'../../../tests/2026-09-12-correction-transport');
(async()=>{fs.mkdirSync(out,{recursive:true});const browser=await chromium.launch({channel:'msedge',headless:true}),results=[];
try{for(const ip of ['192.168.100.20','192.168.100.19']){
 const context=await browser.newContext({viewport:{width:768,height:1024},acceptDownloads:true}),p=await context.newPage(),url='http://'+ip,errors=[];
 try{
  p.on('pageerror',e=>errors.push(e.message));await p.goto(url+'/diagnostics');await p.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Connected'));
  const before=await(await p.request.get(url+'/api/v1/diagnostic')).json(),survey=await(await p.request.get(url+'/api/v1/survey')).json();assert(!before.busy);
  const denied=await p.request.post(url+'/api/v1/diagnostic',{data:{op:'selftest',confirm:true}});assert.equal(denied.status(),401);
  await p.locator('#control').click();await p.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument');await p.locator('#selftest').click();
  await p.waitForFunction(old=>state?.self_test&&state.self_test.run!==old&&document.querySelector('#selfresult').textContent.startsWith('PASS'),before.self_test?.run||0);
  const after=await(await p.request.get(url+'/api/v1/diagnostic')).json();assert.deepEqual(after.last_report,before.last_report);assert(!after.busy);assert.equal(after.self_test.checks,22);assert.equal(after.self_test.failed_mask,0);assert.equal(after.self_test.saved,true);assert(after.self_test.workspace_bytes<=5120);assert(after.self_test.duration_us<1000000);
  const event=p.waitForEvent('download');await p.locator('#selfdownload').click();const download=await event;await download.saveAs(path.join(out,before.role.toLowerCase()+'-selftest.json'));assert.deepEqual(JSON.parse(fs.readFileSync(await download.path(),'utf8')),after.self_test);
  const now=await(await p.request.get(url+'/api/v1/survey')).json();for(const key of ['boot_id','role','jobs','active_job','records_used'])assert.deepEqual(now[key],survey[key]);assert.deepEqual(errors,[]);
  await p.locator('#selfresult').scrollIntoViewIfNeeded();await p.screenshot({path:path.join(out,before.role.toLowerCase()+'-selftest.png')});await p.locator('#release').click();
  results.push({ip,role:before.role,boot_id:now.boot_id,self_test:after.self_test});console.log(before.role+': '+after.self_test.checks+' checks passed in '+(after.self_test.duration_us/1000)+' ms; saved; '+after.self_test.workspace_bytes+' bytes');
 }finally{await p.evaluate(async()=>{const token=sessionStorage.getItem('diagnosticToken');if(token)await fetch('/api/v1/control/release',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:'{}'})}).catch(()=>{});await context.close()}
 }
 fs.writeFileSync(path.join(out,'hardware-results.json'),JSON.stringify({result:'PASS',checks:['authenticated browser action on both roles','22 fault checks on each ESP32','separate saved self-test download','previous RF report unchanged','survey records/role/boot unchanged','no page errors','controllers released'],results},null,2));
}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
