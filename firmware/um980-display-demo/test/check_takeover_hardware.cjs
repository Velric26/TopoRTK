// Real browser takeover checks for both roles. No valid survey command is submitted.
// No bearer or client ID is written to artifacts or logs.
const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const output=path.resolve(__dirname,'../../..',process.env.TOPORTK_TEST_RECORD||'tests/2026-09-12-standalone-tablet');
(async()=>{fs.mkdirSync(output,{recursive:true});const browser=await chromium.launch({channel:'msedge',headless:true}),results=[];
try{for(const ip of ['192.168.100.20','192.168.100.19']){
 const url='http://'+ip,contexts=[await browser.newContext({viewport:{width:390,height:844}}),await browser.newContext({viewport:{width:768,height:1024}})],pages=[await contexts[0].newPage(),await contexts[1].newPage()],errors=[];let latest='';
 try{
  for(const page of pages){page.on('pageerror',e=>errors.push(e.message));await page.goto(url+'/survey');await page.waitForFunction(()=>!document.querySelector('#takeControl').disabled);assert.equal(await page.locator('#controlPinLabel').count(),0)}
  const read=async()=>{const r=await pages[0].request.get(url+'/api/v1/survey');assert(r.ok());return r.json()},before=await read();
  async function take(page){const old=await page.evaluate(()=>sessionStorage.getItem('surveyToken'));await page.locator('#pairPanel').evaluate(x=>x.open=true);await page.locator('#takeControl').click();await page.waitForFunction(old=>sessionStorage.getItem('surveyToken')!==old&&document.querySelector('#controlState').textContent==='You control this instrument',old);latest=await page.evaluate(()=>sessionStorage.getItem('surveyToken'));assert.equal(latest.length,32);return latest}
  const a=await take(pages[0]),b=await take(pages[1]);assert.notEqual(a,b);await pages[0].waitForFunction(()=>document.querySelector('#controlState').textContent==='View only · Take control');
  const stale=await pages[0].request.post(url+'/api/v1/command',{headers:{Authorization:'Bearer '+a},data:{id:'invalid'}});assert.equal(stale.status(),401);
  await pages[0].request.post(url+'/api/v1/control/release',{headers:{Authorization:'Bearer '+a},data:{}});
  const owner=await pages[1].request.get(url+'/api/v1/diagnostic',{headers:{Authorization:'Bearer '+b}});assert.equal(owner.headers()['x-controller'],'true');
  const c=await take(pages[0]),d=await take(pages[0]);assert.notEqual(c,d);const previous=await pages[0].request.get(url+'/api/v1/diagnostic',{headers:{Authorization:'Bearer '+c}});assert.equal(previous.headers()['x-controller'],'false');
  const cross=await pages[0].request.post(url+'/api/v1/control',{headers:{Origin:'http://unrelated.test'},data:{client:'a'.repeat(32)}});assert.equal(cross.status(),403);
  const malformed=await pages[0].request.post(url+'/api/v1/control',{data:{client:'bad'}});assert.equal(malformed.status(),400);
  await pages[0].locator('#pairPanel').evaluate(x=>x.open=true);await pages[0].screenshot({path:path.join(output,'takeover-'+before.role.toLowerCase()+'.png'),fullPage:true});
  const after=await read();for(const key of ['boot_id','role','jobs','active_job','records_used'])assert.deepEqual(after[key],before[key]);assert.deepEqual(errors,[]);results.push({role:before.role,result:'PASS'});console.log('PASS: '+before.role+' latest-request takeover in two browsers');
 }finally{if(latest)await pages[0].request.post(url+'/api/v1/control/release',{headers:{Authorization:'Bearer '+latest},data:{}}).catch(()=>{});for(const c of contexts)await c.close()}
 }
 fs.writeFileSync(path.join(output,'takeover-results.json'),JSON.stringify({results,checks:['no PIN on either role','latest browser wins','same client rotates bearer','stale bearer rejected','stale release cannot revoke current owner','diagnostic owner header matches survey owner','foreign Origin / malformed client rejected','no jobs/role/boot changes','control released','no JavaScript errors']},null,2));
}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
