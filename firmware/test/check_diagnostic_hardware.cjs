// Both instruments run one paired synthetic test requested from one of their real
// browser pages; USB is not used and no test code is shared between the pages.
// Explicit environment settings select a bounded synthetic test; no survey writes.
const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const transport=process.env.TOPORTK_TRANSPORT||'wifi',seconds=Number(process.env.TOPORTK_SECONDS||30),rate=Number(process.env.TOPORTK_RATE||1000),mode=Number(process.env.TOPORTK_MODE||2),profile=process.env.TOPORTK_PROFILE||'clean';
assert(['wifi','sik'].includes(transport)&&[30,60,120,300].includes(seconds)&&[200,1000,3000].includes(rate)&&[0,1,2].includes(mode)&&['clean','injected'].includes(profile));
const stamp=new Date().toISOString().replace(/[:.]/g,'-');
const out=path.resolve(__dirname,'../../tests/2026-09-12-standalone-tablet',transport+'-'+stamp);fs.mkdirSync(out,{recursive:true});
(async()=>{const browser=await chromium.launch({channel:'msedge',headless:true}),contexts=[],pages=[],urls=['http://192.168.100.20','http://192.168.100.19'];
try{
 const errors=[],before=[];
 for(let i=0;i<2;i++){
  contexts[i]=await browser.newContext({viewport:{width:i?390:768,height:1024},acceptDownloads:true});const p=pages[i]=await contexts[i].newPage();p.on('pageerror',e=>errors.push(e.message));
  await p.goto(urls[i]+'/diagnostics');await p.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Connected'));assert(await p.locator('#pinLabel').count()===0||await p.locator('#pinLabel').isHidden());
  const old=await(await p.request.get(urls[i]+'/api/v1/diagnostic')).json();fs.writeFileSync(path.join(out,'before-'+i+'.json'),JSON.stringify(old,null,2));assert.equal(old.role,i?'ROVER':'BASE');assert(!old.busy);
  before[i]={diagnostic:old,survey:await(await p.request.get(urls[i]+'/api/v1/survey')).json()};
  await p.locator('#control').click();await p.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument');
 }
 // One instrument asks for the test; the other one adopts it from the operation.
 const rover=pages[1];
 await rover.locator('#confirm').check();await rover.locator('#transport').selectOption(transport);await rover.locator('#seconds').selectOption(String(seconds));
 if(transport==='wifi'){await rover.locator('#rate').selectOption(String(rate));await rover.locator('#mode').selectOption(String(mode));}else{await rover.locator('#profile').selectOption(profile);}
 await rover.locator('#start').click();
 await pages[1].waitForFunction(()=>settings?.operation?.kind==='test');await pages[0].waitForFunction(()=>settings?.operation?.kind==='test');
 const operation=(await(await rover.request.get(urls[1]+'/api/v1/settings')).json()).operation;
 assert.equal(operation.transport,transport);assert.equal(operation.seconds,seconds);assert.match(operation.id,/^[0-9a-f]{32}$/);
 assert.equal(operation.rate,transport==='wifi'?rate:1000);assert.equal(operation.mode,transport==='wifi'?mode:0);
 assert.equal(operation.profile,transport==='sik'&&profile==='injected'?1:0);
 console.log('One page requested the paired '+transport+' test: '+seconds+' s, '+(transport==='wifi'?rate+' B/s, mode '+mode:'profile '+profile)+'; both instruments adopted it as operation '+operation.id);
 for(const p of pages)await p.waitForFunction(()=>document.querySelector('#testPhase').textContent.startsWith('Preparing')||document.querySelector('#result').textContent.startsWith('Running'));
 for(const c of contexts)await c.setOffline(true);for(const p of pages){await p.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Disconnected'));assert(await p.locator('#cancel').isDisabled());assert(await p.locator('#start').isDisabled())}
 const offlineMs=seconds>=60?20000:5000;await pages[0].waitForTimeout(offlineMs);for(const c of contexts)await c.setOffline(false);
 for(const p of pages)await p.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Connected'));console.log('Both browser connections restored; the test continued on the instruments.');
 const reports=[];
 for(let i=0;i<2;i++){
  const p=pages[i];await p.waitForFunction(()=>document.querySelector('#testPhase').textContent.startsWith('Test finished')||document.querySelector('#result').textContent.startsWith('Not passed'),null,{timeout:(seconds+90)*1000});
  const snapshot=await(await p.request.get(urls[i]+'/api/v1/diagnostic')).json();const settled=(await(await p.request.get(urls[i]+'/api/v1/settings')).json()).operation;
  assert(['succeeded','failed'].includes(settled.state));assert.equal(settled.transport,transport);assert.equal(settled.seconds,seconds);
  const promise=p.waitForEvent('download');await p.locator('#download').click();const download=await promise;await download.saveAs(path.join(out,i?'rover.json':'base.json'));
  const d=reports[i]=JSON.parse(fs.readFileSync(await download.path(),'utf8'));assert.equal(d.run,snapshot.run);assert.equal(d.state,'done');assert.equal(d.persisted,true);
  const after=await(await p.request.get(urls[i]+'/api/v1/survey')).json();for(const key of ['boot_id','role','jobs','active_job','records_used'])assert.deepEqual(after[key],before[i].survey[key]);
  await p.screenshot({path:path.join(out,i?'rover.png':'base.png'),fullPage:true});
  console.log(d.role+': run '+d.run+', '+d.received+'/'+d.expected_rx+' received, '+d.errors+' errors; pair_pass='+d.pair_pass+', operation '+settled.state+'/'+settled.reason);
 }
 assert.equal(reports[0].run,reports[1].run);   // one device-owned id, no number typed into two pages
 assert.deepEqual(errors,[]);
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({workflow:'PASS',started:'One real browser page requested the test; both instruments ran it, no USB',transport,seconds,rate,mode,profile,run:reports[0].run,operation_id:operation.id,offline_seconds:offlineMs/1000,pair_pass:reports.every(d=>d.pair_pass),checks:['both no-PIN takeover','one page requested one link.test','both instruments adopted the same operation','browser arm','both browsers disconnected','controls locked offline','the test finished on the instruments','downloaded matching final reports','both reports share the device-owned run id','reports saved','no survey records, role or boot changes','no page errors']},null,2));
}finally{
 for(let i=0;i<contexts.length;i++){
  await contexts[i].setOffline(false).catch(()=>{});
  if(pages[i])await pages[i].evaluate(async()=>{const token=sessionStorage.getItem('topoControlToken');if(token)await fetch('/api/v1/control/release',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:'{}'})}).catch(()=>{});
 }
 await browser.close();
}
})().catch(e=>{console.error(e);process.exitCode=1});
