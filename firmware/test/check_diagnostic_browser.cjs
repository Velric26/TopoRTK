// Exercises the actual embedded page with controlled protocol states; no physical writes.
// The paired test is requested through the settings surface: the page owns no
// test code and never arms a companion, so the coverage asserts the request's
// shape and the device-owned phase/verdict, not a button returning.
const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),out=path.resolve(root,process.env.TOPORTK_TEST_RECORD||'tests/2026-09-12-standalone-tablet');
// The Link-test page is the canonical source the build embeds (see web/assets.json).
const html=fs.readFileSync(path.join(__dirname,'../web/diagnostics.html'),'utf8');
const saved={run:912203,state:'done',reason:'complete',role:'ROVER',transport:'sik',seconds:30,rate:1000,mode:2,expected_tx:117,sent:117,expected_rx:117,received:117,errors:0,duplicates:0,reordered:0,max_gap_ms:694,local_pass:true,peer_report_received:true,pair_pass:true};
(async()=>{fs.mkdirSync(out,{recursive:true});const browser=await chromium.launch({channel:'msedge',headless:true});try{
 const context=await browser.newContext({viewport:{width:768,height:1024},acceptDownloads:true}),page=await context.newPage(),errors=[];
 let state={run:0,state:'idle',role:'ROVER',reason:'idle',radio_probe:'not checked',busy:false,remaining_seconds:0,last_report:saved};
 let record={version:1,revision:7,selected_transport:'wifi',peer_connected:false,operation:null};
 let token='',posts=[],freeze=false,offline=false,uptime=100;
 // The instrument's own operation record: the page only renders what this holds.
 // The published operation record carries no separate 'active' flag; the page
 // derives the phases that still own the tested medium from its state.
 const publish=(operation)=>{record={...record,operation}};
 page.on('pageerror',e=>errors.push(e.message));
 await page.route('**/*',async route=>{const r=route.request(),url=new URL(r.url());
  if(url.pathname==='/api/v1/diagnostic'&&r.method()==='GET'){if(offline)return route.abort('internetdisconnected');if(!freeze)uptime+=1000;return route.fulfill({json:{...state,uptime_ms:uptime},headers:{'X-Controller':token&&r.headers().authorization==='Bearer '+token?'true':'false'}})}
  if(url.pathname==='/api/v1/settings'&&r.method()==='GET'){if(offline)return route.abort('internetdisconnected');return route.fulfill({json:record,headers:{'X-Controller':token&&r.headers().authorization==='Bearer '+token?'true':'false'}})}
  if(r.method()==='POST'){const d=r.postDataJSON();posts.push({path:url.pathname,data:d});
   if(url.pathname==='/api/v1/control'){token='a'.repeat(32);return route.fulfill({json:{token,lease_seconds:120}})}
   if(url.pathname==='/api/v1/control/release'){token='';return route.fulfill({json:{state:'released'}})}
   if(r.headers().authorization!=='Bearer '+token)return route.fulfill({status:401,json:{error:'claim_control_first'}});
   if(url.pathname==='/api/v1/diagnostic'){
    if(d.op==='corrections')state={...state,corrections:{transport:d.transport,session:0,peer_connected:false,pair_state:'negotiating',station:-1,queue_expired:0,wire_errors:0,output:{forwarded:0}}};
    if(d.op==='probe')state={...state,state:'probing',busy:true};
    if(d.op==='selftest')state={...state,self_test:{kind:'local_transport_fault_suite',suite_version:1,run:77,state:'passed',passed:true,checks:22,failed_mask:0,workspace_bytes:4760,duration_us:4200,saved:true}};
    return route.fulfill({status:202,json:{state:'queued'}});
   }
   if(url.pathname==='/api/v1/settings'){
    if(d.op==='link.test')publish({id:d.id,tag:0x1234,kind:'test',transport:d.transport,previous_transport:'wifi',state:'negotiating',committed:false,coordinator:true,phase_remaining_ms:20000,reason:'',profile:d.profile==='injected'?1:0,seconds:d.seconds,rate:d.rate??1000,mode:d.mode??0});
    if(d.op==='link.cancel')publish({...record.operation,state:'cancelled',reason:'cancelled',phase_remaining_ms:0});
    return route.fulfill({status:202,json:{state:'queued'}});
   }
   return route.fulfill({status:400,json:{error:'operation_not_available'}});
  }
  if(url.pathname==='/api.js')return route.fulfill({body:fs.readFileSync(path.join(__dirname,'../web/api.js'),'utf8'),contentType:'application/javascript'});
  return route.fulfill({body:html,contentType:'text/html'});
 });
 await page.goto('http://diagnostic.test/diagnostics');await page.waitForFunction(()=>document.querySelector('#connection').textContent==='Connected · Rover');
 assert.equal(await page.locator('#pinLabel').count(),0);
 // A passive view can read and download, but starts nothing.
 assert(await page.locator('#start').isDisabled());assert(await page.locator('#probe').isDisabled());assert(await page.locator('#selftest').isDisabled());
 assert.match(await page.locator('#result').innerText(),/PASS/);assert.equal(await page.locator('#reportTitle').innerText(),'3. Saved report of this instrument');
 assert.deepEqual(posts.filter(p=>p.path!=='/api/v1/control'),[]);
 async function download(){const promise=page.waitForEvent('download');await page.locator('#download').click();const d=await promise;return JSON.parse(fs.readFileSync(await d.path(),'utf8'))}
 assert.deepEqual(await download(),saved);
 await page.locator('#control').click();await page.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument');await page.reload();await page.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument');assert.equal(posts.filter(p=>p.path==='/api/v1/control').length,1);
 // Confirming is required, and cancelling before it sends no device request at all.
 const before=posts.length;
 await page.locator('#start').click();assert.match(await page.locator('#message').innerText(),/Confirm preparation/);assert.equal(posts.length,before);
 // One instrument requests the test; the tested medium and the shape it offers
 // are the only fields that travel.
 await page.locator('#confirm').check();await page.locator('#seconds').selectOption('120');await page.locator('#profile').selectOption('injected');await page.locator('#start').click();
 await page.waitForFunction(()=>document.querySelector('#testPhase').textContent.startsWith('Preparing the tested link'));
 const started=posts.at(-1);assert.equal(started.path,'/api/v1/settings');assert.equal(started.data.op,'link.test');
 assert.equal(started.data.transport,'sik');assert.equal(started.data.seconds,120);assert.equal(started.data.profile,'injected');
 assert.equal(started.data.confirm,true);assert.match(started.data.id,/^[0-9a-f]{32}$/);assert.equal(started.data.rate,undefined);assert.equal(started.data.mode,undefined);
 assert.equal(posts.filter(p=>p.data.op==='link.test').length,1);
 assert.match(await page.locator('#result').innerText(),/No verdict yet/);
 // The instrument owns the phase and the verdict: the page never decides a pass.
 assert(await page.locator('#start').isDisabled());for(const id of ['arm','pairarm','run'])assert.equal(await page.locator('#'+id).count(),0);   // no test code, no per-instrument arming
 publish({...record.operation,state:'running',phase_remaining_ms:45000});state={...state,busy:true,state:'running',remaining_seconds:45};
 await page.waitForFunction(()=>document.querySelector('#result').textContent.startsWith('Running'));
 assert.equal(await page.locator('#progress').isVisible(),true);
 assert(await page.locator('#profile').isDisabled());assert(await page.locator('#seconds').isDisabled());assert(await page.locator('#mode').isDisabled());
 // The run finished without passing: the device record, not the page, decides.
 publish({...record.operation,state:'failed',reason:'test_failed',phase_remaining_ms:0});
 state={...state,busy:false,state:'done',reason:'complete',run:913307,sent:117,received:116,errors:1,local_pass:false,peer_report_received:true,pair_pass:false,persisted:true,last_report:saved};
 await page.waitForFunction(()=>document.querySelector('#result').textContent.startsWith('Not passed'));
 const failed=await download();assert.equal(failed.run,913307);assert.equal(failed.received,116);assert.equal(failed.pair_pass,false);assert.equal(failed.run!==saved.run,true);
 assert.match(await page.locator('#testPhase').innerText(),/Test failed/);
 // A successful request of the other medium sends only its own fields.
 await page.locator('#transport').selectOption('wifi');await page.locator('#seconds').selectOption('60');await page.locator('#rate').selectOption('200');await page.locator('#mode').selectOption('2');await page.locator('#start').click();
 await page.waitForFunction(()=>settings?.operation?.transport==='wifi');
 const wifi=posts.at(-1);assert.equal(wifi.path,'/api/v1/settings');
 assert.deepEqual(wifi.data,{id:wifi.data.id,revision:7,op:'link.test',transport:'wifi',seconds:60,rate:200,mode:2,confirm:true});
 assert.equal(await page.locator('#profile').isDisabled(),true);
 // Only the instrument that issued the request may cancel it, and cancelling is
 // the settings surface, never a second matching field.
 assert(await page.locator('#cancel').isEnabled());await page.locator('#cancel').click();
 await page.waitForFunction(()=>document.querySelector('#testPhase').textContent.startsWith('Test cancelled'));
 assert.deepEqual(posts.at(-1).data,{id:wifi.data.id,op:'link.cancel',confirm:true});
 assert.match(await page.locator('#result').innerText(),/Cancelled/);
 // A report the instrument still holds stays downloadable while live reads fail.
 freeze=true;await page.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Disconnected'),null,{timeout:9000});assert(await page.locator('#start').isDisabled());assert(await page.locator('#cancel').isDisabled());assert.match(await page.locator('#testPhase').innerText(),/keeps running on the instrument/);
 freeze=false;await page.waitForFunction(()=>document.querySelector('#connection').textContent==='Connected · Rover');
 // Another instrument's operation is not this page's to cancel: the instrument
 // publishes an id only to the instrument that issued the request.
 publish({...record.operation,id:'',state:'running',phase_remaining_ms:30000});
 await page.waitForTimeout(1400);assert.equal(record.operation.id,'');assert(await page.locator('#cancel').isDisabled());
 publish({...record.operation,id:wifi.data.id,state:'succeeded',reason:'applied'});
 await page.waitForFunction(()=>document.querySelector('#testPhase').textContent.startsWith('Test finished'));
 const passed=await download();assert.equal(passed.run,913307);assert.equal(passed.pair_pass,false);
 for(const width of [320,390,768,1280]){await page.setViewportSize({width,height:900});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));await page.screenshot({path:path.join(out,'diagnostic-'+width+'.png'),fullPage:true})}
 await page.setViewportSize({width:390,height:844});await page.addStyleTag({content:':root{font-size:32px}'});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 await page.locator('#release').click();await page.waitForFunction(()=>document.querySelector('#controlState').textContent.startsWith('View only'));
 assert(await page.locator('#start').isDisabled());
 state={run:0,state:'idle',role:'BASE',reason:'idle',radio_probe:'not checked',busy:false,last_report:null};publish(null);await page.reload();await page.waitForFunction(()=>document.querySelector('#connection').textContent==='Connected · Base');assert.equal(await page.locator('#pinLabel').count(),0);
 await page.locator('#control').click();await page.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument');assert.equal(posts.filter(p=>p.path==='/api/v1/control').at(-1).data.pin,undefined);
 // The local actions stay local: the wiring probe and the fault self-test.
 await page.locator('#selftest').click();await page.waitForFunction(()=>document.querySelector('#selfresult').textContent.startsWith('PASS'));const selfPromise=page.waitForEvent('download');await page.locator('#selfdownload').click();const selfDownload=await selfPromise;const selfReport=JSON.parse(fs.readFileSync(await selfDownload.path(),'utf8'));assert.equal(selfReport.kind,'local_transport_fault_suite');assert.equal(selfReport.run,77);assert.equal(selfReport.checks,22);
 // The probe takes UART2 away from the correction link, so it needs the same
 // preparation confirmation the test does.
 const beforeProbe=posts.length;await page.locator('#probe').click();assert.match(await page.locator('#message').innerText(),/Confirm preparation/);assert.equal(posts.length,beforeProbe);
 await page.locator('#confirm').check();
 await page.locator('#probe').click();await page.waitForFunction(()=>document.querySelector('#result').textContent.startsWith('Checking the local'));
 state={...state,state:'idle',busy:false,run:0};await page.waitForFunction(()=>!document.querySelector('#liveRadio').disabled);
 await page.locator('#liveRadio').click();await page.waitForFunction(()=>document.querySelector('#routeState').textContent.includes('Radio'));
 assert.deepEqual(posts.at(-1).data,{op:'corrections',transport:'sik',confirm:true});
 assert.match(await page.locator('#routePeer').innerText(),/not connected/i);
 // Radio carries the correction link, so the wiring probe (which reads UART2)
 // and the local fault checks stay unavailable, in the page and on the server.
 assert(await page.locator('#probe').isDisabled());assert(await page.locator('#selftest').isDisabled());
 state={...state,corrections:{...state.corrections,session:123456789,peer_connected:true,pair_state:'connected'}};
 await page.waitForFunction(()=>document.querySelector('#routePeer').textContent.startsWith('Peer connected'));
 offline=true;await page.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Disconnected'));assert(await page.locator('#liveWifi').isDisabled());assert.match(await page.locator('#routePeer').innerText(),/stale/i);offline=false;
 await page.waitForFunction(()=>!document.querySelector('#liveWifi').disabled);await page.locator('#liveWifi').click();await page.waitForFunction(()=>document.querySelector('#routeState').textContent.startsWith('Wi-Fi'));
 assert.deepEqual(posts.at(-1).data,{op:'corrections',transport:'wifi',confirm:true});assert.match(await page.locator('#routePeer').innerText(),/not connected/i);
 state={...state,role:'ROVER'};await page.waitForFunction(()=>state?.role==='ROVER');
 await page.locator('#liveRadio').click();await page.waitForFunction(()=>document.querySelector('#routeState').textContent.includes('Radio'));
 assert.deepEqual(posts.at(-1).data,{op:'corrections',transport:'sik',confirm:true});
 state={...state,corrections:{...state.corrections,error:'cannot_save_link_preference'}};
 await page.waitForFunction(()=>!!document.querySelector('#routeError').textContent);assert.match(await page.locator('#routePeer').innerText(),/not connected/i);assert.deepEqual(errors,[]);
 await page.setViewportSize({width:390,height:844});await page.screenshot({path:path.join(out,'live-routing-390.png'),fullPage:true});
 fs.writeFileSync(path.join(out,'browser-results.json'),JSON.stringify({result:'PASS',checks:['live connection and role','both roles use takeover without a PIN','explicit claim/release and reload retention','a passive view starts nothing','confirm gate sends nothing','one link.test with a fresh 32-hex id, the tested medium and only its fields','device-owned preparation, running and verdict phases','current failed report never replaced with the old pass','the other medium sends its own rate and mode','cancel is the issuer\'s link.cancel','offline and stale freeze the controls and the phase text','another controller\'s operation cannot be cancelled','report download','320/390/768/1280 widths and 200% text','local fault self-test and downloaded report','wiring probe stays local','Radio selection locks the UART2 probe and the local fault checks','local selection is not pair coordination and stays visible','no page errors']},null,2));console.log('PASS: diagnostic page requests one device-owned paired test through the settings surface; '+posts.length+' device requests');
 }finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
