const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../src/debug_ui.h'),'utf8'),html=source.split('R"HTML(')[1].split(')HTML"')[0];
const js=fs.readFileSync(path.join(__dirname,'../src/ota_ui.h'),'utf8').split('R"JS(')[1].split(')JS"')[0];
const out=path.resolve(__dirname,'../../../tests/2026-09-14-ota');fs.mkdirSync(out,{recursive:true});
(async()=>{const browser=await chromium.launch({channel:'msedge',headless:true});try{
 const page=await browser.newPage({viewport:{width:390,height:844}}),errors=[],posts=[];
 let owner=false,enabled=true,uptime=0,ack=false,boot=7,version='0.11.0',state='idle',pending=0;
 let uploaded=null;
 const file=Buffer.alloc(640);file.write('TPK1');file[4]=1;file[5]=2;file[6]=1;file.writeUInt32LE(512,8);file.write('0.11.1',48);
 page.on('pageerror',e=>errors.push(e.message));
 await page.route('**/*',async route=>{const r=route.request(),url=new URL(r.url());
  if(url.pathname==='/api/v1/update/upload'){
   assert.equal(state,'ready');assert.equal(r.headers().authorization,'Bearer '+'a'.repeat(32));
   uploaded=r.postDataBuffer();state='restarting';pending=2;
   return route.fulfill({json:{state:'restarting'}});
  }
  if(r.method()==='POST'){
   const d=r.postDataJSON();posts.push({path:url.pathname,...d});
   if(url.pathname==='/api/v1/control'){owner=true;return route.fulfill({json:{token:'a'.repeat(32)}});}
   if(!owner)return route.fulfill({status:401,json:{error:'owner required'}});
   if(url.pathname==='/api/v1/update'){
    if(d.op==='prepare'){assert.equal(d.header,file.subarray(0,128).toString('hex'));state='review';}
    if(d.op==='start'){assert.equal(d.confirm,true);assert(d.allow_unconfirmed||ack);state='pausing';pending=2;}
    if(d.op==='cancel')state='failed';
   }
   return route.fulfill({json:{state:'queued'}});
  }
  if(url.pathname==='/api/v1/debug')return route.fulfill({json:{version:1,boot_id:boot,uptime_ms:++uptime*1000,firmware:version,enabled,remaining_ms:900000},headers:{'X-Controller':String(owner)}});
  if(url.pathname==='/api/v1/debug/log')return route.fulfill({json:{entries:[],throttled:0,overwritten:0,truncated:0}});
  if(url.pathname==='/api/v1/survey')return route.fulfill({json:{unit:'B',role:'ROVER',gnss:{fixed:false,profile_verified:true}}});
  if(url.pathname==='/api/v1/diagnostic')return route.fulfill({json:{corrections:{transport:'sik'}}});
  if(url.pathname==='/api/v1/update'){
   if(pending&&!--pending){if(state==='pausing')state='ready';else{state='idle';boot=8;version='0.11.1';enabled=owner=false;}}
   return route.fulfill({json:{version:1,state,unit:2,firmware:version,available:true,locked:!['idle','failed'].includes(state),boot_id:boot,boot:boot===8?'New firmware verified':'USB / normal boot',peer_acknowledged:ack,peer_status:'Base updating - corrections paused'}});
  }
  if(url.pathname==='/update-ui.js')return route.fulfill({body:js,contentType:'application/javascript'});
  return route.fulfill({body:html,contentType:'text/html'});
 });
 await page.goto('http://ota.test/debug');await page.waitForFunction(()=>!document.querySelector('#claim').disabled);
 assert(await page.locator('#firmwareFile').isDisabled());await page.locator('#claim').click();await page.waitForFunction(()=>!document.querySelector('#firmwareFile').disabled);
 const wrong=Buffer.from(file);wrong[5]=1;await page.locator('#firmwareFile').setInputFiles({name:'wrong.tpk',mimeType:'application/octet-stream',buffer:wrong});
 await page.waitForFunction(()=>document.querySelector('#firmwareReview').textContent.includes('other instrument'));assert(await page.locator('#reviewFirmware').isDisabled());
 await page.locator('#firmwareFile').setInputFiles({name:'rover.tpk',mimeType:'application/octet-stream',buffer:file});await page.waitForFunction(()=>!document.querySelector('#reviewFirmware').disabled);
 assert(!posts.some(p=>p.path==='/api/v1/update'));assert(await page.locator('#confirmFirmware').isDisabled());
 await page.locator('#reviewFirmware').click();await page.waitForFunction(()=>document.querySelector('#otaProgress').textContent.includes('unconfirmed'));
 await page.locator('#acceptInterruption').check();assert(await page.locator('#confirmFirmware').isDisabled());
 await page.locator('#allowUnconfirmed').check();assert(!(await page.locator('#confirmFirmware').isDisabled()));
 for(const width of [320,390,768,1280]){await page.setViewportSize({width,height:900});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));await page.screenshot({path:path.join(out,'ota-review-'+width+'.png'),fullPage:true});}
 await page.locator('#confirmFirmware').click();await page.waitForFunction(()=>document.querySelector('#otaProgress').textContent.startsWith('Update complete'),null,{timeout:15000});
 assert.deepEqual(uploaded,file);assert.equal(posts.filter(p=>p.op==='start').length,1);assert(await page.locator('#firmwareFile').isDisabled());
 assert.deepEqual(errors,[]);fs.writeFileSync(path.join(out,'ota-browser.json'),JSON.stringify({result:'PASS',checks:['file selection has no mutation','wrong unit rejected','current-controller gate','explicit interruption confirmation','unconfirmed peer override','one binary upload after ready','new boot verification before success','Debug off after restart','responsive 320/390/768/1280']},null,2));
 console.log('PASS: OTA browser target/review/confirmation, unconfirmed-peer override, upload and new-boot verification');
 }finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
