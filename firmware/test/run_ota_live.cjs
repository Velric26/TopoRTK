// Explicit live firmware install: --install <unit A|B> <unit URL> <peer URL> <package.tpk>.
// Never retries automatically; private evidence stays under ignored .pio.
const {chromium}=require('playwright');
const fs=require('node:fs'), path=require('node:path'), assert=require('node:assert/strict');
const [flag,unit,base,peer,packagePath]=process.argv.slice(2);
if(flag!=='--install'||!['A','B'].includes(unit)||!base||!peer||!packagePath)throw Error('Usage: --install <A|B> <unit URL> <peer URL> <package.tpk>');
const root=path.resolve(__dirname,'..'),dir=path.join(root,'.pio/ota-live-'+Date.now());fs.mkdirSync(dir,{recursive:true});
const bytes=fs.readFileSync(packagePath);assert.equal(bytes.subarray(0,4).toString(),'TPK1');assert.equal(bytes[5],unit.charCodeAt(0)-64);assert.equal(bytes.length,128+bytes.readUInt32LE(8));assert.equal(require('node:crypto').createHash('sha256').update(bytes.subarray(128)).digest('hex'),bytes.subarray(16,48).toString('hex'));
const evidence={started:new Date().toISOString(),peer:[],stages:[]};
async function get(url){const r=await fetch(url,{signal:AbortSignal.timeout(3000)});if(!r.ok)throw Error('HTTP '+r.status);return r.json()}
const subset=s=>Object.fromEntries(['unit','role','active_job','jobs','records_used','config','point_count','storage_ready'].map(k=>[k,s[k]??null]));
(async()=>{
 const before=await get(base+'/api/v1/survey'); assert.equal(before.unit,unit);assert.equal(before.collection.active,false);
 const debug=await get(base+'/api/v1/debug');assert.equal(debug.enabled,true);
 const old=await get(base+'/api/v1/update');assert(['idle','failed'].includes(old.state));assert.equal(old.locked,false);evidence.oldBoot=old.boot_id;
 fs.writeFileSync(path.join(dir,'ota-survey-before.json'),JSON.stringify(subset(before),null,2));
 const browser=await chromium.launch({channel:'msedge',headless:true});let timer;
 try{
  const page=await browser.newPage({viewport:{width:1024,height:900}});const errors=[];page.on('pageerror',e=>errors.push(e.message));
  evidence.network=[];page.on('requestfailed',r=>{if(r.url().startsWith(base+'/api/'))evidence.network.push({at:new Date().toISOString(),path:new URL(r.url()).pathname,error:r.failure()?.errorText});});
  page.on('response',r=>{if(r.url()===base+'/api/v1/update/upload')evidence.network.push({at:new Date().toISOString(),path:'/api/v1/update/upload',status:r.status()});});
  await page.goto(base+'/debug');await page.locator('#claim').click();
  await page.locator('#firmwareFile').setInputFiles(path.resolve(packagePath));
  await page.waitForFunction(unit=>document.querySelector('#firmwareReview').textContent.includes('Unit '+unit),unit);
  evidence.warning=await page.locator('#updateWarning').innerText();console.log('Warning:',evidence.warning);
  assert(evidence.warning.length>40);
  let last='';timer=setInterval(async()=>{try{const u=await get(peer+'/api/v1/update');if(u.peer_status!==last){last=u.peer_status;evidence.peer.push({at:new Date().toISOString(),status:last});console.log('Peer:',last)}}catch{}},350);
  await page.locator('#reviewFirmware').click();
  try{await page.waitForFunction(()=>document.querySelector('#otaProgress').textContent.includes('Peer acknowledged preparation'),{},{timeout:20000})}catch(e){
   evidence.stages.push('Preparation not acknowledged; cancelled without flashing');
   if(await page.locator('#cancelFirmware').isEnabled())await page.locator('#cancelFirmware').click();
   throw Error('Peer preparation acknowledgement missing; no override authorized');
  }
  evidence.stages.push('Peer preparation acknowledged');console.log('Preparation acknowledged.');
  assert.equal(await page.locator('#confirmFirmware').isDisabled(),true);
  await page.screenshot({path:path.join(dir,'ota-review.png'),fullPage:true});
  await page.locator('#acceptInterruption').check();assert.equal(await page.locator('#allowUnconfirmed').isChecked(),false);
  await page.locator('#confirmFirmware').click();console.log('Confirmed; browser manages upload.');
  await page.waitForFunction(()=>document.querySelector('#otaState').textContent.startsWith('Update: failed')||/Update complete|Update rolled back|Upload rejected|Update stopped/.test(document.querySelector('#otaProgress').textContent),{},{timeout:360000});
  evidence.result=await page.locator('#otaProgress').innerText();console.log(evidence.result);evidence.terminal=await get(base+'/api/v1/update');
  assert.match(evidence.result,/Update complete/);
  evidence.after=await get(base+'/api/v1/update');assert.notEqual(evidence.after.boot_id,old.boot_id);assert.equal(evidence.after.boot,'New firmware verified');
  evidence.debug=await get(base+'/api/v1/debug');assert.equal(evidence.debug.enabled,true); // Debug is On by default at boot (0.11.5).
  evidence.savedStateUnchanged=JSON.stringify(subset(await get(base+'/api/v1/survey')))===JSON.stringify(subset(before));assert.equal(evidence.savedStateUnchanged,true);
  evidence.browserErrors=errors;assert.deepEqual(errors,[]);
  await page.screenshot({path:path.join(dir,'ota-complete.png'),fullPage:true});
  console.log('PASS: new boot verified, Debug On (default), saved survey state unchanged.');
 }finally{clearInterval(timer);await browser.close();fs.writeFileSync(path.join(dir,'ota-evidence.json'),JSON.stringify(evidence,null,2))}
})().catch(e=>{console.error(e.message);process.exitCode=1});
