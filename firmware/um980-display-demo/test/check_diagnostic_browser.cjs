// Exercises the actual embedded page with controlled protocol states; no physical writes.
const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../..'),out=path.resolve(root,process.env.TOPORTK_TEST_RECORD||'tests/2026-09-12-standalone-tablet');
const html=fs.readFileSync(path.join(__dirname,'../src/link_diagnostic_ui.h'),'utf8').split('R"HTML(')[1].split(')HTML"')[0];
const saved={run:912203,state:'done',reason:'complete',role:'ROVER',transport:'sik',seconds:30,rate:1000,mode:2,expected_tx:117,sent:117,expected_rx:117,received:117,errors:0,duplicates:0,reordered:0,max_gap_ms:694,local_pass:true,peer_report_received:true,pair_pass:true};
(async()=>{fs.mkdirSync(out,{recursive:true});const browser=await chromium.launch({channel:'msedge',headless:true});try{
 const context=await browser.newContext({viewport:{width:768,height:1024},acceptDownloads:true}),page=await context.newPage(),errors=[];
 let state={run:0,state:'idle',role:'ROVER',reason:'idle',radio_probe:'not checked',busy:false,remaining_seconds:0,last_report:saved},token='',posts=[],freeze=false,offline=false,uptime=100;
 page.on('pageerror',e=>errors.push(e.message));
 await page.route('**/*',async route=>{const r=route.request(),url=new URL(r.url());
  if(url.pathname==='/api/v1/diagnostic'&&r.method()==='GET'){if(offline)return route.abort('internetdisconnected');if(!freeze)uptime+=1000;return route.fulfill({json:{...state,uptime_ms:uptime},headers:{'X-Controller':token&&r.headers().authorization==='Bearer '+token?'true':'false'}})}
  if(r.method()==='POST'){const d=r.postDataJSON();posts.push({path:url.pathname,data:d});
   if(url.pathname==='/api/v1/control'){token='a'.repeat(32);return route.fulfill({json:{token,lease_seconds:120}})}
   if(url.pathname==='/api/v1/control/release'){token='';return route.fulfill({json:{state:'released'}})}
   if(r.headers().authorization!=='Bearer '+token)return route.fulfill({status:401,json:{error:'claim_control_first'}});
   if(d.op==='arm')state={...state,...d,state:'armed',reason:'waiting_for_other_instrument',busy:true,sent:0,received:0,expected_tx:117,expected_rx:117,pair_pass:false};
   if(d.op==='cancel')state={...state,state:'failed',reason:'cancelled',busy:false,persisted:true};
   if(d.op==='probe')state={...state,state:'probing',busy:true};
   if(d.op==='selftest')state={...state,self_test:{kind:'local_transport_fault_suite',suite_version:1,run:77,state:'passed',passed:true,checks:22,failed_mask:0,workspace_bytes:4760,duration_us:4200,saved:true}};
   return route.fulfill({status:202,json:{state:'queued'}});
  }
  return route.fulfill({body:html,contentType:'text/html'});
 });
 await page.goto('http://diagnostic.test/diagnostics');await page.waitForFunction(()=>document.querySelector('#connection').textContent==='Connected · Rover');
 assert.equal(await page.locator('#pinLabel').count(),0);assert(await page.locator('#arm').isDisabled());assert.match(await page.locator('#result').innerText(),/PASS/);assert.equal(await page.locator('#reportTitle').innerText(),'3. Saved report');
 async function download(){const promise=page.waitForEvent('download');await page.locator('#download').click();const d=await promise;return JSON.parse(fs.readFileSync(await d.path(),'utf8'))}
 assert.deepEqual(await download(),saved);
 await page.locator('#control').click();await page.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument');await page.reload();await page.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument');assert.equal(posts.filter(p=>p.path==='/api/v1/control').length,1);
 await page.locator('#arm').click();assert.match(await page.locator('#message').innerText(),/Confirm preparation/);await page.locator('#confirm').check();await page.locator('#run').fill('913101');await page.locator('#mode').selectOption('2');await page.locator('#seconds').selectOption('30');await page.locator('#arm').click();await page.waitForFunction(()=>document.querySelector('#result').textContent.startsWith('Waiting for the other'));
 assert(await page.locator('#run').isDisabled());assert(await page.locator('#cancel').isEnabled());
 state={...state,state:'running',remaining_seconds:20,sent:39,received:38};await page.waitForFunction(()=>document.querySelector('#result').textContent.startsWith('Running'));
 offline=true;await page.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Disconnected'));assert(await page.locator('#cancel').isDisabled());
 state={...state,state:'done',busy:false,sent:117,received:116,errors:1,local_pass:false,peer_report_received:true,pair_pass:false,persisted:true};offline=false;await page.waitForFunction(()=>document.querySelector('#result').textContent.startsWith('Not passed'));
 const failed=await download();assert.equal(failed.run,913101);assert.equal(failed.received,116);assert.equal(failed.pair_pass,false);assert.equal(failed.last_report.run,912203); // current run, never substitute old PASS
 freeze=true;await page.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Disconnected'),null,{timeout:9000});assert(await page.locator('#arm').isDisabled());freeze=false;await page.waitForFunction(()=>document.querySelector('#connection').textContent==='Connected · Rover');
 state={...state,...saved,run:913101,last_report:{...saved,run:913101}};await page.waitForFunction(()=>document.querySelector('#result').textContent.startsWith('PASS'));
 for(const width of [320,390,768,1280]){await page.setViewportSize({width,height:900});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));await page.screenshot({path:path.join(out,'diagnostic-'+width+'.png'),fullPage:true})}
 await page.setViewportSize({width:390,height:844});await page.addStyleTag({content:':root{font-size:32px}'});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 await page.locator('#release').click();await page.waitForFunction(()=>document.querySelector('#controlState').textContent.startsWith('View only'));state={run:0,state:'idle',role:'BASE',reason:'idle',radio_probe:'not checked',busy:false,last_report:null};await page.reload();await page.waitForFunction(()=>document.querySelector('#connection').textContent==='Connected · Base');assert.equal(await page.locator('#pinLabel').count(),0);
 await page.locator('#control').click();await page.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument');assert.equal(posts.filter(p=>p.path==='/api/v1/control').at(-1).data.pin,undefined);
 await page.locator('#selftest').click();await page.waitForFunction(()=>document.querySelector('#selfresult').textContent.startsWith('PASS'));const selfPromise=page.waitForEvent('download');await page.locator('#selfdownload').click();const selfDownload=await selfPromise;const selfReport=JSON.parse(fs.readFileSync(await selfDownload.path(),'utf8'));assert.equal(selfReport.kind,'local_transport_fault_suite');assert.equal(selfReport.run,77);assert.equal(selfReport.checks,22);
 await page.locator('#confirm').check();await page.locator('#probe').click();await page.waitForFunction(()=>document.querySelector('#result').textContent.startsWith('Checking the local'));assert(await page.locator('#cancel').isDisabled());assert.deepEqual(errors,[]);
 fs.writeFileSync(path.join(out,'browser-results.json'),JSON.stringify({result:'PASS',checks:['live connection and role','Both roles use takeover without a PIN','explicit claim/release and reload retention','confirm/arm/state lock','offline and stale controls disabled','current failed report never replaced with old pass','report download','320/390/768/1280 widths and 200% text','separate local fault self-test and downloaded report','probe has no ineffective cancel button','no page errors']},null,2));console.log('PASS: diagnostic browser lifecycle, reports, offline state and responsive layout');
 }finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
