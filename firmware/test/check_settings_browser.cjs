// Exercises the production Settings page against a controlled pair-settings fixture
// (Playwright route interception; no instrument, no network, no pair writes).
//
//   node test/check_settings_browser.cjs      (NODE_PATH must resolve Playwright)
const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),out=path.join(root,process.env.TOPORTK_TEST_RECORD||'tests/2026-09-15-settings-browser');
// The Settings page is the canonical source the build embeds (see web/assets.json).
const html=fs.readFileSync(path.join(__dirname,'../web/settings.html'),'utf8');
const api=fs.readFileSync(path.join(__dirname,'../web/api.js'),'utf8');
const ID=/^[0-9a-f]{32}$/,WAIT={timeout:8000},checks=[];

// The page recognises its own operation through an FNV-1a hash of the request id; the
// fixture recomputes that hash from the request instead of reading page internals.
function tag(id){let hash=2166136261,length=0;for(const c of id){const d=c>='0'&&c<='9'?c.charCodeAt(0)-48:c>='a'&&c<='f'?c.charCodeAt(0)-87:-1;if(d<0||++length>32)return 0;hash=Math.imul(hash^d,16777619)>>>0}return length===32?hash:0}
const fixture=overrides=>Object.assign({revision:4,selected:'wifi',candidate:null,peer:true,fresh:true,operation:null,
  token:null,claims:0,uptime:1000,gets:0,failGet:false,refuse:null,accept:null,role:null},overrides);
const settingsBody=f=>{const body={version:1,boot_id:'4d2c1b0a',uptime_ms:f.uptime+=1000,revision:f.revision,selected_transport:f.selected,
  candidate_transport:f.candidate,peer_connected:f.peer,corrections_fresh:f.fresh,operation:f.operation,last_tests:{wifi:null,sik:null}};
  if(f.role)body.role=f.role;return body};
const startOperation=(f,data)=>{f.operation={id:data.id,tag:tag(data.id),kind:'select',transport:data.transport,previous_transport:f.selected,
  state:'negotiating',committed:false,coordinator:false,phase_remaining_ms:9000,reason:''};f.candidate=data.transport};
function answer(f,data,route){
  if(f.refuse){const error=f.refuse;f.refuse=null;if(error==='stale_revision')f.revision=6;return route.fulfill({status:409,json:{error}})}
  if(f.accept)f.accept(data);
  if(data.op==='link.select')startOperation(f,data);
  return route.fulfill({status:202,json:{state:'queued'}});
}
function handler(f,label,log){return async route=>{
  const request=route.request(),url=new URL(request.url());
  if(url.pathname==='/api.js')return route.fulfill({body:api,contentType:'application/javascript'});
  if(url.pathname==='/api/v1/settings'&&request.method()==='GET'){f.gets++;
    if(f.failGet)return route.abort('internetdisconnected');
    return route.fulfill({json:settingsBody(f),headers:{'X-Controller':request.headers().authorization==='Bearer '+f.token?'true':'false'}})}
  if(url.pathname==='/api/v1/control'||url.pathname==='/api/v1/control/release'){
    log.push({label,path:url.pathname,data:request.postDataJSON()});
    if(url.pathname==='/api/v1/control/release'){f.token=null;return route.fulfill({json:{state:'released'}})}
    f.claims++;f.token=(f.claims===1?'a':'b').repeat(32);return route.fulfill({json:{token:f.token,lease_seconds:120}})}
  if(url.pathname==='/api/v1/settings'&&request.method()==='POST'){
    const data=request.postDataJSON();log.push({label,path:url.pathname,data,auth:request.headers().authorization});
    if(request.headers().authorization!=='Bearer '+f.token)return route.fulfill({status:401,json:{error:'claim_control_first'}});
    return answer(f,data,route)}
  return route.fulfill({body:html,contentType:'text/html'});}}

(async()=>{fs.mkdirSync(out,{recursive:true});
const browser=await chromium.launch({channel:'msedge',headless:true});
try{
const errors=[];
const txt=(page,id)=>page.locator('#'+id).textContent();
const live=page=>page.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Live from instrument'),null,WAIT);
const writes=page=>page.locator('[data-write]').evaluateAll(nodes=>nodes.map(n=>({id:n.id,disabled:n.disabled,hidden:n.offsetParent===null})));
// Success wording about the pair: the page must never show it while the route is only queued.
const claimsPair=async page=>/switch complete|both instruments selected/i.test(await page.locator('main').innerText());
async function loadAndStates(page,revision){
  await live(page);
  assert.equal(await txt(page,'wifiState'),'Selected',`wi-fi card at revision ${revision}`);
  assert.equal(await txt(page,'radioState'),'Not selected');
  assert.equal(await page.locator('#wifiCard').getAttribute('data-selected'),'true');
  assert.equal(await page.locator('#radioCard').getAttribute('data-selected'),'false');
  assert.equal(await txt(page,'selectedRoute'),'Wi-Fi');
  assert.equal(await txt(page,'peerState'),'Connected');
  assert.equal(await txt(page,'correctionsState'),'Fresh');
  assert(await page.locator('#operationSection').isHidden());
  assert.match(await txt(page,'identity'),new RegExp('revision '+revision));
}
async function tabWalk(page,steps){const seen=[];
  for(let i=0;i<steps;i++){await page.keyboard.press('Tab');
    seen.push(await page.evaluate(()=>{const a=document.activeElement;return{id:(a&&a.id)||(a?a.tagName:''),focusVisible:!!(a&&a.matches&&a.matches(':focus-visible')),inView:!!(a&&a.getBoundingClientRect().width>0&&a.getBoundingClientRect().bottom>0&&a.getBoundingClientRect().top<innerHeight)}}))}
  return seen}
const reached=(walk,id)=>{const hits=walk.filter(s=>s.id===id);assert(hits.length>=1,id+' is not reachable with Tab');assert(hits.every(s=>s.focusVisible),id+' does not show keyboard focus');return hits};

// ---- Main page: load, switch, refusals, HTTP loss, layout ----
const context=await browser.newContext({viewport:{width:1200,height:1000}});
const page=await context.newPage();page.on('pageerror',e=>errors.push('main: '+e.message));
const F=fixture(),log=[];
await page.route('**/*',handler(F,'main',log));
const selectPosts=()=>log.filter(p=>p.path==='/api/v1/settings');

// 1. Load and states: the selected medium, the pair and the operation section.
await page.goto('http://settings.test/settings');
await loadAndStates(page,4);
assert(await page.locator('#confirmSection').isHidden());
assert.equal(await txt(page,'controlState'),'View only · Take control');
assert(!await page.locator('#takeControl').isDisabled());
const locked=await writes(page);
assert.deepEqual(locked.map(w=>w.id),['selectRadio','selectWifi','confirmSwitch','cancelOperation']);
assert(locked.filter(w=>w.id==='selectRadio'||w.id==='selectWifi').every(w=>w.disabled),'route buttons are live without control');
checks.push('load: Wi-Fi card selected, connected/fresh pair, no operation section');

// 2. Queued is not connected: explicit confirmation, one request, no premature claim.
await page.locator('#takeControl').click();
await page.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument',null,WAIT);
assert.deepEqual(log.filter(p=>p.path==='/api/v1/control').map(p=>Object.keys(p.data)),[['client']]);
assert(ID.test(log.find(p=>p.path==='/api/v1/control').data.client));
assert(!await page.locator('#selectRadio').isDisabled());
await page.locator('#selectRadio').click();
assert(!await page.locator('#confirmSection').isHidden());
assert.equal(await txt(page,'confirmHeading'),'Confirm switch to Radio (SiK)');
assert.match(await txt(page,'confirmWarning'),/Radio/);assert.match(await txt(page,'confirmWarning'),/Wi-Fi/);
assert.equal(await page.evaluate(()=>document.activeElement.id),'confirmSwitch');
assert.equal(selectPosts().length,0);
checks.push('an explicit select click opens the confirm step, which warns about both media');

await page.locator('#confirmSwitch').click();
await page.waitForFunction(()=>document.querySelector('#message').textContent.includes('queued'),null,WAIT);
assert.equal(selectPosts().length,1,'more than one switch request was sent');
const sent=selectPosts()[0].data;
assert.deepEqual(Object.keys(sent).sort(),['confirm','id','op','revision','transport']);
assert(ID.test(sent.id));assert.equal(sent.revision,4);assert.equal(sent.op,'link.select');assert.equal(sent.transport,'sik');assert.equal(sent.confirm,true);
assert.equal(selectPosts()[0].auth,'Bearer '+'a'.repeat(32));
checks.push('exactly one link.select carrying the fixture revision, transport sik, confirm:true and a 32-hex id');
await page.waitForFunction(()=>document.querySelector('#operationState').textContent.startsWith('Contacting the other instrument on Radio'),null,WAIT);
assert(!await page.locator('#operationSection').isHidden());
assert(await page.locator('#confirmSection').isHidden());
assert(!await page.locator('#cancelOperation').isHidden());
assert.match(await txt(page,'operationOwner'),/came from this browser/);
assert.equal(await txt(page,'selectedRoute'),'Wi-Fi');
assert.equal(await txt(page,'radioState'),'Trying now');
assert.notEqual(await txt(page,'radioState'),'Selected');
assert.equal(await txt(page,'wifiState'),'Selected');
assert(!await claimsPair(page),'the pair is reported connected while the switch is only negotiating');
F.operation={...F.operation,state:'running'};
await page.waitForFunction(()=>document.querySelector('#operationState').textContent==='Applying Radio (SiK). Corrections are paused.',null,WAIT);
assert.equal(await txt(page,'selectedRoute'),'Wi-Fi');
assert.notEqual(await txt(page,'radioState'),'Selected');
assert(!await claimsPair(page));
assert(await page.locator('#confirmSection').isHidden());
checks.push('negotiating and running never report the new medium as connected');

F.operation={...F.operation,state:'succeeded',committed:true,phase_remaining_ms:0};F.selected='sik';F.candidate=null;
await page.waitForFunction(()=>document.querySelector('#operationState').textContent==='Switch complete. Both instruments selected Radio (SiK).',null,WAIT);
assert.equal(await txt(page,'selectedRoute'),'Radio (SiK)');
assert.equal(await txt(page,'radioState'),'Selected');
assert.equal(await txt(page,'wifiState'),'Not selected');
assert.equal(await page.locator('#radioCard').getAttribute('data-selected'),'true');
assert.equal(selectPosts().length,1);
checks.push('committed success advances negotiating/restoring-free to the completed switch and Radio selected');
// A finished operation stays reported by the instrument, and must not lock the page:
// the medium that is not selected stays switchable while the outcome is on screen.
assert(!await page.locator('#selectWifi').isDisabled(),'a reported outcome must not disable switching back');
assert(await page.locator('#selectRadio').isDisabled(),'the already-selected medium stays disabled');
checks.push('a reported outcome does not lock the route selector');

// 3. Unreachable target: the pair keeps the previous route.
F.operation=null;F.selected='wifi';F.candidate=null;
await page.waitForFunction(()=>document.querySelector('#operationSection').hidden,null,WAIT);
await page.locator('#selectRadio').click();await page.locator('#confirmSwitch').click();
await page.waitForFunction(()=>document.querySelector('#operationState').textContent.startsWith('Contacting the other instrument on Radio'),null,WAIT);
assert.equal(selectPosts().length,2);
F.operation={...F.operation,state:'restoring'};
await page.waitForFunction(()=>document.querySelector('#operationState').textContent.includes('could not be confirmed; restoring Wi-Fi'),null,WAIT);
assert(!await claimsPair(page));assert.equal(await txt(page,'selectedRoute'),'Wi-Fi');
F.operation={...F.operation,state:'failed',committed:false,phase_remaining_ms:0,reason:'peer_unreachable'};F.candidate=null;
await page.waitForFunction(()=>document.querySelector('#operationState').textContent==='Switch failed (peer_unreachable). Wi-Fi remains in use.',null,WAIT);
assert(!await claimsPair(page),'success wording accompanied an unreachable target');
assert.match(await txt(page,'operationDetail'),/State failed · reason peer_unreachable/);
assert.equal(await txt(page,'selectedRoute'),'Wi-Fi');
assert.equal(await txt(page,'wifiState'),'Selected');
assert.equal(await txt(page,'radioState'),'Not selected');
assert(!await page.locator('#operationSection').isHidden());
assert(await page.locator('#recoveryHint').isHidden());
assert.equal(selectPosts().length,2);
checks.push('peer_unreachable restores nothing and reports the failed switch with Wi-Fi still selected');

// 4. Refusals: stale revision then busy; neither starts an operation.
F.operation=null;F.selected='wifi';F.refuse='stale_revision';
await page.waitForFunction(()=>document.querySelector('#operationSection').hidden,null,WAIT);
await page.locator('#selectRadio').click();await page.locator('#confirmSwitch').click();
await page.waitForFunction(()=>document.querySelector('#message').textContent.includes('stored selection changed'),null,WAIT);
assert.match(await txt(page,'message'),/refresh/i);assert.match(await txt(page,'message'),/review/i);
assert.equal(F.revision,6);
assert(await page.locator('#operationSection').isHidden(),'a refused request started an operation');
assert(await page.locator('#recoveryHint').isHidden());
assert(await page.locator('#confirmSection').isHidden());
assert.equal(selectPosts().length,3);
assert.equal(selectPosts()[2].data.revision,4);
checks.push('stale_revision asks for a review of the refreshed state and starts nothing');
await page.waitForFunction(()=>document.querySelector('#identity').textContent.includes('revision 6'),null,WAIT);
F.refuse='operation_busy';
await page.locator('#selectRadio').click();await page.locator('#confirmSwitch').click();
await page.waitForFunction(()=>document.querySelector('#message').textContent.includes('Another pair operation is already running'),null,WAIT);
assert.match(await txt(page,'message'),/another pair operation/i);
assert.equal(selectPosts()[3].data.revision,6,'the retry did not use the refreshed revision');
assert(await page.locator('#operationSection').isHidden());
assert(await page.locator('#recoveryHint').isHidden());
assert.equal(selectPosts().length,4);
checks.push('operation_busy reports the running operation, starts nothing and keeps the recovery hint hidden');

// 5. A fresh reload renders the server operation and never repeats the request.
const contextB=await browser.newContext({viewport:{width:390,height:844}});
const pageB=await contextB.newPage();pageB.on('pageerror',e=>errors.push('reload: '+e.message));
const B=fixture({revision:9,operation:{id:'',tag:1298461,kind:'select',transport:'sik',previous_transport:'wifi',state:'negotiating',committed:false,coordinator:false,phase_remaining_ms:8000,reason:''},candidate:'sik'});
const logB=[];await pageB.route('**/*',handler(B,'reload',logB));
await pageB.goto('http://settings.test/settings');
await pageB.waitForFunction(()=>document.querySelector('#operationState').textContent.startsWith('Contacting the other instrument on Radio'),null,WAIT);
assert(!await pageB.locator('#operationSection').isHidden());
assert.match(await txt(pageB,'operationOwner'),/other instrument/);
assert.match(await txt(pageB,'operationOwner'),/only observes/);
assert(await pageB.locator('#cancelOperation').isHidden());
assert.equal(await txt(pageB,'controlState'),'View only · Take control');
assert.equal(await txt(pageB,'selectedRoute'),'Wi-Fi');
assert.equal(logB.filter(p=>p.path==='/api/v1/settings').length,0,'the reload submitted the observed request');
assert.equal(await txt(pageB,'message'),'');
const getsBefore=B.gets;
B.operation={...B.operation,state:'succeeded',committed:true,phase_remaining_ms:0};B.selected='sik';B.candidate=null;
await pageB.waitForFunction(()=>document.querySelector('#operationState').textContent==='Switch complete. Both instruments selected Radio (SiK).',null,WAIT);
assert.equal(await txt(pageB,'selectedRoute'),'Radio (SiK)');
await pageB.waitForTimeout(2200);
assert(B.gets>=getsBefore+2);
assert.equal(logB.filter(p=>p.path==='/api/v1/settings').length,0,'the completed operation triggered a resubmit');
checks.push('a fresh reload renders the peer operation without any POST, before and after it completes');

// 6. HTTP loss during an accepted switch: no second request, outcome read back.
F.operation=null;F.selected='wifi';F.candidate=null;F.accept=()=>{F.failGet=true};
await page.waitForFunction(()=>document.querySelector('#operationSection').hidden,null,WAIT);
const beforeLoss=selectPosts().length;
await page.locator('#selectRadio').click();await page.locator('#confirmSwitch').click();
await page.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('No live settings'),null,WAIT);
assert.equal(await page.locator('#connection').getAttribute('data-tone'),'bad');
assert.match(await txt(page,'connection'),/^No live settings\. Reconnecting · last update \d+ s ago\.$/);
const offline=await writes(page);
assert.equal(offline.length,4);
assert(offline.every(w=>w.disabled),'a write control stayed live without settings');
assert(await page.locator('#takeControl').isDisabled());
assert(await page.locator('#release').isDisabled());
checks.push('losing the settings stream reports no live settings and disables every write control');
F.failGet=false;F.accept=null;
F.operation={...F.operation,state:'failed',committed:false,phase_remaining_ms:0,reason:'peer_unreachable'};F.candidate=null;
await live(page);
await page.waitForFunction(()=>document.querySelector('#operationState').textContent==='Switch failed (peer_unreachable). Wi-Fi remains in use.',null,WAIT);
assert.equal(await txt(page,'selectedRoute'),'Wi-Fi');
const getsBeforeRecovery=F.gets;
await page.waitForTimeout(1200);
assert(F.gets>getsBeforeRecovery);
assert.equal(selectPosts().length,beforeLoss+1,'a lost stream caused a second switch request');
checks.push('the reviewed outcome is read back from the instrument with no repeat request');

// 7. Takeover: the earlier browser becomes view only and cannot switch.
const T=fixture({revision:3}),logT=[];
const context1=await browser.newContext({viewport:{width:768,height:1024}}),context2=await browser.newContext({viewport:{width:768,height:1024}});
const page1=await context1.newPage(),page2=await context2.newPage();
page1.on('pageerror',e=>errors.push('takeover-1: '+e.message));page2.on('pageerror',e=>errors.push('takeover-2: '+e.message));
await context1.route('**/*',handler(T,'first',logT));await context2.route('**/*',handler(T,'second',logT));
const firstPosts=()=>logT.filter(p=>p.label==='first'&&p.path==='/api/v1/settings').length;
await page1.goto('http://settings.test/settings');await live(page1);
await page1.locator('#takeControl').click();
await page1.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument',null,WAIT);
assert(!await page1.locator('#selectRadio').isDisabled());
assert.equal(firstPosts(),0);
await page2.goto('http://settings.test/settings');await live(page2);
await page2.locator('#takeControl').click();
await page2.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument',null,WAIT);
await page1.waitForFunction(()=>document.querySelector('#controlState').textContent==='View only · Take control',null,WAIT);
assert((await writes(page1)).every(w=>w.disabled),'the invalidated browser kept a live write control');
await page1.locator('#selectRadio').evaluate(n=>n.click());
await page1.locator('#selectRadio').dispatchEvent('click');
assert(await page1.locator('#confirmSection').isHidden());
await page1.locator('#confirmSwitch').dispatchEvent('click');
await page1.waitForTimeout(1500);
assert(await page1.locator('#confirmSection').isHidden());
assert(!/sending|accepted/i.test(await txt(page1,'message')));
assert.equal(firstPosts(),0,'the invalidated browser sent a switch request');
checks.push('takeover leaves the earlier browser view only with every write control disabled');
assert(!await page2.locator('#selectRadio').isDisabled());
await page2.locator('#selectRadio').click();await page2.locator('#confirmSwitch').click();
await page2.waitForFunction(()=>document.querySelector('#message').textContent.includes('queued'),null,WAIT);
assert.equal(logT.filter(p=>p.label==='second'&&p.path==='/api/v1/settings').length,1);
assert.equal(firstPosts(),0);
await page1.waitForFunction(()=>document.querySelector('#operationState').textContent.startsWith('Contacting the other instrument on Radio'),null,WAIT);
assert.match(await txt(page1,'operationOwner'),/other instrument/);
assert(await page1.locator('#cancelOperation').isHidden());
assert.equal(await txt(page1,'controlState'),'View only · Take control');
T.operation={...T.operation,state:'succeeded',committed:true,phase_remaining_ms:0};T.selected='sik';T.candidate=null;
await page1.waitForFunction(()=>document.querySelector('#selectedRoute').textContent==='Radio (SiK)',null,WAIT);
assert.equal(firstPosts(),0,'the invalidated browser sent a request while observing');
checks.push('the invalidated browser observes the new controller operation and never sends one');

// 8. Both roles: nothing role-specific is needed to render or operate.
const contextC=await browser.newContext({viewport:{width:390,height:844}});
const pageC=await contextC.newPage();pageC.on('pageerror',e=>errors.push('role: '+e.message));
const C=fixture({revision:11,role:'ROVER'}),logC=[];
await contextC.route('**/*',handler(C,'role',logC));
await pageC.goto('http://settings.test/settings');
await loadAndStates(pageC,11);
assert(!/rover|base/i.test(await pageC.locator('main').innerText()),'a role word reached the rendered copy');
C.role='BASE';await pageC.reload();
await loadAndStates(pageC,11);
assert(!/rover|base/i.test(await pageC.locator('main').innerText()));
await pageC.locator('#takeControl').click();
await pageC.waitForFunction(()=>document.querySelector('#controlState').textContent==='You control this instrument',null,WAIT);
await pageC.locator('#selectRadio').click();
assert(!await pageC.locator('#confirmSection').isHidden());
await pageC.locator('#confirmSwitch').click();
await pageC.waitForFunction(()=>document.querySelector('#message').textContent.includes('queued'),null,WAIT);
C.operation={...C.operation,state:'succeeded',committed:true,phase_remaining_ms:0};C.selected='sik';C.candidate=null;
await pageC.waitForFunction(()=>document.querySelector('#operationState').textContent==='Switch complete. Both instruments selected Radio (SiK).',null,WAIT);
assert.equal(await txt(pageC,'selectedRoute'),'Radio (SiK)');
assert.equal(logC.filter(p=>p.path==='/api/v1/settings').length,1);
checks.push('both roles render the same states and can perform a switch');

// 9. Layout, keyboard and no PIN.
await page.setViewportSize({width:1200,height:1000});
F.operation=null;F.selected='wifi';F.candidate=null;
await page.waitForFunction(()=>document.querySelector('#operationSection').hidden,null,WAIT);
await page.locator('#selectRadio').click();
assert(!await page.locator('#confirmSection').isHidden());
F.operation={id:'',tag:0,kind:'select',transport:'sik',previous_transport:'wifi',state:'recovery_required',committed:false,coordinator:false,phase_remaining_ms:4000,reason:'restore_failed'};
await page.waitForFunction(()=>!document.querySelector('#recoveryHint').hidden,null,WAIT);
for(const width of [320,390,768,1280]){await page.setViewportSize({width,height:900});
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`overflow at ${width} px`);
  await page.screenshot({path:path.join(out,'settings-'+width+'.png'),fullPage:true})}
checks.push('320/390/768/1280 px without horizontal overflow');
await page.setViewportSize({width:390,height:844});
// The page sizes its typography in rem, so doubling the root font is a real 200% text test.
await page.addStyleTag({content:':root{font-size:32px}'});
assert(!await page.locator('#takeControl').isHidden());
assert(!await page.locator('#confirmSwitch').isHidden());
await page.screenshot({path:path.join(out,'settings-200pct.png'),fullPage:true});
checks.push('Take control and Confirm switch stay visible at 200% text');
reached(await tabWalk(page,16),'confirmSwitch');
await page.locator('#release').click();
await page.waitForFunction(()=>document.querySelector('#controlState').textContent==='View only · Take control',null,WAIT);
assert(!await page.locator('#takeControl').isDisabled());
assert(!await page.locator('#takeControl').isHidden());
assert(!await page.locator('#confirmSwitch').isHidden());
reached(await tabWalk(page,16),'takeControl');
checks.push('Tab focus reaches Take control and Confirm switch at 200% text');

assert.equal(await page.locator('input[type=password]').count(),0);
assert.equal(await page.evaluate(()=>[...document.querySelectorAll('input')].filter(n=>/password|pin/i.test(n.type+n.name+n.id+n.autocomplete)).length),0);
const leaks=await page.evaluate(()=>{const pattern=/pin|password/i,found=[],walker=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);
  for(let node=walker.nextNode();node;node=walker.nextNode()){
    if(!pattern.test(node.nodeValue)||!node.parentElement||node.parentElement.offsetParent===null)continue;
    found.push(node.nodeValue.trim())}
  return found});
assert.equal(leaks.length,1,'a PIN or password is mentioned outside the explanatory sentence: '+leaks.join(' | '));
assert.match(leaks[0],/without a PIN/);
assert(!/PIN|password/i.test(log.filter(p=>p.path.startsWith('/api/v1/control')).map(p=>JSON.stringify(p.data)).join('')));
checks.push('no password field and no PIN request apart from the claim explanation');

// Measured last so that everything else in this file is still exercised in the same run.
const overflow=await page.evaluate(()=>{const offenders=[];
  for(const node of document.querySelectorAll('body *')){const box=node.getBoundingClientRect();
    if(box.width&&box.right>innerWidth+0.5)offenders.push('#'+(node.id||node.tagName.toLowerCase())+' right='+Math.round(box.right)+' width='+Math.round(box.width))}
  return {innerWidth,scrollWidth:document.documentElement.scrollWidth,root:getComputedStyle(document.documentElement).fontSize,offenders:offenders.slice(0,6)}});
assert(overflow.scrollWidth<=overflow.innerWidth,
  `the page overflows horizontally with 200% text: scrollWidth ${overflow.scrollWidth} > innerWidth ${overflow.innerWidth} at root ${overflow.root}; ${overflow.offenders.join(', ')}`);
checks.push('390 px with 200% text without horizontal overflow');

assert.deepEqual(errors,[]);
fs.writeFileSync(path.join(out,'settings-browser.json'),JSON.stringify({result:'PASS',browser:await browser.version(),checks},null,2));
console.log('PASS: settings page states, guarded switch, refusals, reload, stream loss, takeover, both roles, layout and no-PIN');
}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
