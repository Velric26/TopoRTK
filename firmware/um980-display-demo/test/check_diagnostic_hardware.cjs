// Both instruments armed through their real browser pages. USB is not used.
// Explicit environment settings select a bounded synthetic test; no survey writes.
const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const run=Number(process.env.TOPORTK_RUN||913202),transport=process.env.TOPORTK_TRANSPORT||'wifi',seconds=Number(process.env.TOPORTK_SECONDS||30),rate=Number(process.env.TOPORTK_RATE||1000),mode=Number(process.env.TOPORTK_MODE||2);
assert(run>=100000&&run<=999999&&['wifi','sik'].includes(transport)&&[30,60,120,300].includes(seconds)&&[200,1000,3000].includes(rate)&&[0,1,2].includes(mode));
const out=path.resolve(__dirname,'../../../tests/2026-09-12-standalone-tablet',transport+'-'+run);fs.mkdirSync(out,{recursive:true});
(async()=>{const browser=await chromium.launch({channel:'msedge',headless:true}),contexts=[],pages=[],urls=['http://192.168.100.20','http://192.168.100.19'];
try{
 const errors=[],before=[];
 for(let i=0;i<2;i++){
  contexts[i]=await browser.newContext({viewport:{width:i?390:768,height:1024},acceptDownloads:true});const p=pages[i]=await contexts[i].newPage();p.on('pageerror',e=>errors.push(e.message));
  await p.goto(urls[i]+'/diagnostics');await p.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Connected'));assert(await p.locator('#pinLabel').count()===0||await p.locator('#pinLabel').isHidden());
  const old=await(await p.request.get(urls[i]+'/api/v1/diagnostic')).json();fs.writeFileSync(path.join(out,'before-'+i+'.json'),JSON.stringify(old,null,2));assert.equal(old.role,i?'ROVER':'BASE');assert(!old.busy);
  before[i]=await(await p.request.get(urls[i]+'/api/v1/survey')).json();
  await p.locator('#control').click();await p.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument');
  await p.locator('#run').fill(String(run));await p.locator('#transport').selectOption(transport);await p.locator('#mode').selectOption(String(mode));await p.locator('#seconds').selectOption(String(seconds));await p.locator('#rate').selectOption(String(rate));await p.locator('#confirm').check();
 }
 for(const p of pages)await p.locator('#arm').click();
 for(const p of pages)await p.waitForFunction(()=>document.querySelector('#result').textContent.startsWith('Running'),null,{timeout:15000});console.log('Both instruments armed by browser: '+transport+' '+seconds+' s, '+rate+' B/s, mode '+mode);
 for(const c of contexts)await c.setOffline(true);for(const p of pages){await p.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Disconnected'));assert(await p.locator('#cancel').isDisabled())}
 const offlineMs=seconds>=60?20000:5000;await pages[0].waitForTimeout(offlineMs);for(const c of contexts)await c.setOffline(false);
 for(const p of pages)await p.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Connected'));console.log('Both browser connections restored; test continues on instruments.');
 const reports=[];
 for(let i=0;i<2;i++){
  const p=pages[i];await p.waitForFunction(()=>document.querySelector('#result').textContent.startsWith('PASS')||document.querySelector('#result').textContent.startsWith('Not passed'),null,{timeout:(seconds+15)*1000});
  const promise=p.waitForEvent('download');await p.locator('#download').click();const download=await promise;await download.saveAs(path.join(out,i?'rover.json':'base.json'));
  const d=reports[i]=JSON.parse(fs.readFileSync(await download.path(),'utf8'));assert.equal(d.run,run);assert.equal(d.state,'done');assert.equal(d.persisted,true);
  const after=await(await p.request.get(urls[i]+'/api/v1/survey')).json();for(const key of ['boot_id','role','jobs','active_job','records_used'])assert.deepEqual(after[key],before[i][key]);
  await p.screenshot({path:path.join(out,i?'rover.png':'base.png'),fullPage:true});
  console.log(d.role+': '+d.received+'/'+d.expected_rx+' received, '+d.errors+' errors; pair_pass='+d.pair_pass);
 }
 assert.deepEqual(errors,[]);fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({workflow:'PASS',started:'Both actual browser pages, no USB',transport,seconds,rate,mode,offline_seconds:offlineMs/1000,pair_pass:reports.every(d=>d.pair_pass),checks:['both no-PIN takeover','browser arm','both browsers disconnected','controls locked offline','downloaded matching final reports','reports saved','no survey records, role or boot changes','no page errors']},null,2));
}finally{
 for(let i=0;i<contexts.length;i++){
  await contexts[i].setOffline(false).catch(()=>{});
  if(pages[i])await pages[i].evaluate(async()=>{const token=sessionStorage.getItem('diagnosticToken');if(token)await fetch('/api/v1/control/release',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:'{}'})}).catch(()=>{});
 }
 await browser.close();
}
})().catch(e=>{console.error(e);process.exitCode=1});
