// Takes/releases Rover control but never submits a valid survey command.
// No bearer, client ID or PIN is written to artifacts or logs.
const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const output=path.resolve(__dirname,'../../..',process.env.TOPORTK_TEST_RECORD||'tests/2026-09-11-rover-takeover');
(async()=>{const browser=await chromium.launch({channel:'msedge',headless:true});let latest='',first;
try{
 const contexts=[await browser.newContext({viewport:{width:390,height:844}}),await browser.newContext({viewport:{width:390,height:844}})];
 const pages=[await contexts[0].newPage(),await contexts[1].newPage()],errors=[];first=pages[0];for(const page of pages){page.on('pageerror',e=>errors.push(e.message));await page.goto('http://192.168.100.20/survey');await page.waitForFunction(()=>!document.querySelector('#takeControl').disabled);assert(await page.locator('#controlPinLabel').isHidden());}
 const read=async page=>{const r=await page.request.get('http://192.168.100.20/api/v1/survey');assert(r.ok());return r.json()};
 const before=await read(first);assert.equal(before.role,'ROVER');
 async function take(page){const previous=await page.evaluate(()=>sessionStorage.getItem('surveyToken'));await page.locator('#pairPanel').evaluate(x=>x.open=true);await page.locator('#takeControl').click();await page.waitForFunction(previous=>sessionStorage.getItem('surveyToken')!==previous&&document.querySelector('#controlState').textContent==='You control this instrument',previous);const token=await page.evaluate(()=>sessionStorage.getItem('surveyToken'));assert(token?.length===32);latest=token;return token}
 const a=await take(first),b=await take(pages[1]);assert(a!==b);
 await first.waitForFunction(()=>document.querySelector('#controlState').textContent==='View only · Take control');assert(await first.locator('#create button').isDisabled());
 const stale=await first.request.post('http://192.168.100.20/api/v1/command',{headers:{Authorization:'Bearer '+a},data:{id:'invalid'}});assert.equal(stale.status(),401);
 await first.request.post('http://192.168.100.20/api/v1/control/release',{headers:{Authorization:'Bearer '+a},data:{}});
 const owner=await pages[1].request.get('http://192.168.100.20/api/v1/survey',{headers:{Authorization:'Bearer '+b}});assert.equal(owner.headers()['x-controller'],'true');
 const c=await take(first);assert(c!==a&&c!==b);await pages[1].waitForFunction(()=>document.querySelector('#controlState').textContent==='View only · Take control');
 const d=await take(first);assert(d!==c);const previous=await first.request.get('http://192.168.100.20/api/v1/survey',{headers:{Authorization:'Bearer '+c}});assert.equal(previous.headers()['x-controller'],'false');
 // Existing same-origin checks still apply to unauthenticated takeovers.
 const cross=await first.request.post('http://192.168.100.20/api/v1/control',{headers:{Origin:'http://unrelated.test'},data:{client:'a'.repeat(32)}});assert.equal(cross.status(),403);
 const malformed=await first.request.post('http://192.168.100.20/api/v1/control',{data:{client:'bad'}});assert.equal(malformed.status(),400);
 await first.locator('#pairPanel').evaluate(x=>x.open=true);await first.screenshot({path:path.join(output,'hardware-rover-control.png'),fullPage:true});
 await first.locator('#release').click();latest='';await first.waitForFunction(()=>document.querySelector('#controlState').textContent==='View only · Take control');
 const base=await contexts[0].newPage();await base.goto('http://192.168.100.19/survey');await base.waitForFunction(()=>!document.querySelector('#takeControl').disabled);await base.locator('#pairPanel').evaluate(x=>x.open=true);assert(await base.locator('#controlPinLabel').isVisible());assert(await base.locator('#pair input').evaluate(x=>x.required&&!x.disabled));const denied=await base.request.post('http://192.168.100.19/api/v1/control',{data:{client:'a'.repeat(32)}});assert.equal(denied.status(),400);await base.screenshot({path:path.join(output,'hardware-base-pin.png'),fullPage:true});
 const after=await read(first);for(const key of ['boot_id','role','jobs','active_job','records_used'])assert.deepEqual(after[key],before[key]);assert.deepEqual(errors,[]);
 fs.writeFileSync(path.join(output,'takeover-results.json'),JSON.stringify({result:'PASS',checks:['Rover has no PIN entry','latest browser wins','previous bearer rejected with 401','previous release cannot revoke current owner','same client rotates bearer','foreign Origin rejected','malformed client rejected','Base still requires PIN','roles/jobs/journal/boot unchanged','no valid survey commands submitted','control released after test','no JS errors']},null,2));console.log('PASS: real Rover two-browser takeover and Base PIN retention; no survey data changed');
}finally{if(latest&&first)await first.request.post('http://192.168.100.20/api/v1/control/release',{headers:{Authorization:'Bearer '+latest},data:{}}).catch(()=>{});await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
