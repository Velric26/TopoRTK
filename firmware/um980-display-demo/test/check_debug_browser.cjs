const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../..'),out=path.join(root,'tests/2026-09-13-debug');
const source=fs.readFileSync(path.join(__dirname,'../src/debug_ui.h'),'utf8'),html=source.split('R"HTML(')[1].split(')HTML"')[0],nav=source.split('R"JS(')[1].split(')JS"')[0];
(async()=>{fs.mkdirSync(out,{recursive:true});const browser=await chromium.launch({channel:'msedge',headless:true});try{
 const context=await browser.newContext({viewport:{width:390,height:844},acceptDownloads:true}),page=await context.newPage(),errors=[],posts=[];
 let enabled=false,owner=false,offline=false,frozen=false,uptime=0,role='ROVER',token='';
 const report={version:1,boot_id:7,uptime_ms:2000,throttled:4,overwritten:8,truncated:1,entries:[{sequence:1,at_ms:1000,channel:'GNSS RX',text:'$GPGGA,passive sample'},{sequence:2,at_ms:1050,channel:'SiK RX',text:'<img src=x onerror="window.injected=true">'}]};
 page.on('pageerror',e=>errors.push(e.message));
 await context.route('**/*',async route=>{const r=route.request(),url=new URL(r.url());
  if(offline&&url.pathname.startsWith('/api/'))return route.abort('internetdisconnected');
  if(r.method()==='POST'){
   const data=r.postDataJSON();posts.push({path:url.pathname,data});
   if(url.pathname==='/api/v1/control'){owner=true;token='a'.repeat(32);return route.fulfill({json:{token}})}
   if(r.headers().authorization!=='Bearer '+token||!owner)return route.fulfill({status:401,json:{error:'claim_control_first'}});
   if(data.op==='disable')enabled=false;
   else if(data.op!=='activity'||!enabled)return route.fulfill({status:403,json:{error:'enable_debug_on_touchscreen'}});
   return route.fulfill({json:{state:'applied'}});
  }
  if(url.pathname==='/api/v1/debug'){
   if(!frozen)uptime+=1000;
   return route.fulfill({json:{version:1,boot_id:7,uptime_ms:uptime,firmware:'0.10.5',enabled,remaining_ms:enabled?900000:0,ota_available:false},headers:{'X-Controller':String(owner&&r.headers().authorization==='Bearer '+token)}});
  }
  if(url.pathname==='/api/v1/debug/log')return route.fulfill({json:report});
  if(url.pathname==='/api/v1/survey')return route.fulfill({json:{unit:'B',role,collection:{active:true},gnss:{profile_verified:true,fixed:true}}});
  if(url.pathname==='/api/v1/diagnostic')return route.fulfill({json:{corrections:{transport:'sik',output:{forwarded:42}}}});
  if(url.pathname==='/debug-nav.js')return route.fulfill({body:nav,contentType:'application/javascript'});
  if(url.pathname==='/survey')return route.fulfill({body:'<nav><button>Jobs</button><button>Setup</button><button>Collect</button><button>Points</button></nav><script src="/debug-nav.js"></script>',contentType:'text/html'});
  return route.fulfill({body:html,contentType:'text/html'});
 });
 await page.goto('http://debug.test/survey');await page.waitForSelector('#debugTab');assert(await page.locator('#debugTab').isDisabled());assert.match(await page.locator('#debugAvailability').innerText(),/Setup.*Debug.*Enable/);
 enabled=true;await page.waitForFunction(()=>!document.querySelector('#debugTab').disabled);await page.locator('#debugTab').click();await page.waitForFunction(()=>!document.querySelector('#claim').disabled);
 await page.locator('#claim').click();await page.waitForFunction(()=>document.querySelector('#log').textContent.includes('GPGGA'));
 assert.equal(await page.locator('#log img').count(),0);assert.equal(await page.evaluate(()=>window.injected),undefined);
 assert.equal(await page.locator('#gnss').innerText(),'RTK FIXED');assert.equal(await page.locator('#forwarded').innerText(),'42');
 const activity=posts.filter(p=>p.data.op==='activity').length;await page.waitForTimeout(2500);assert.equal(posts.filter(p=>p.data.op==='activity').length,activity,'polling must not renew Debug');
 await page.locator('#filter').selectOption('SiK RX');assert(!(await page.locator('#log').innerText()).includes('GPGGA'));
 const downloaded=page.waitForEvent('download');await page.locator('#download').click();const download=await downloaded;assert.deepEqual(JSON.parse(fs.readFileSync(await download.path(),'utf8')),report);
 await page.locator('#pause').click();assert.equal(await page.locator('#pause').innerText(),'Resume view');
 owner=false;await page.waitForFunction(()=>document.querySelector('#ownership').textContent.startsWith('Take control'));assert(await page.locator('#download').isDisabled());assert(!(await page.locator('#log').innerText()).includes('GPGGA'));
 await page.locator('#claim').click();await page.waitForFunction(()=>document.querySelector('#ownership').textContent.startsWith('You control'));await page.locator('#pause').click();
 frozen=true;await page.waitForFunction(()=>document.querySelector('#connection').textContent.startsWith('Disconnected'),null,{timeout:10000});assert(await page.locator('#extend').isDisabled());frozen=false;await page.waitForFunction(()=>!document.querySelector('#extend').disabled);
 for(const width of [320,390,768,1280]){await page.setViewportSize({width,height:900});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));await page.screenshot({path:path.join(out,'debug-'+width+'.png'),fullPage:true})}
 role='BASE';await page.waitForFunction(()=>document.querySelector('#identity').textContent.includes('BASE'));assert.match(await page.locator('#updateWarning').innerText(),/Rover may lose RTK fix/);assert.equal(await page.locator('nav a').count(),1);
 enabled=false;await page.waitForFunction(()=>document.querySelector('#mode').textContent.includes('Debug unavailable'));assert(await page.locator('#claim').isDisabled());assert(await page.locator('#download').isDisabled());
 await page.goto('http://debug.test/survey');await page.waitForSelector('#debugTab');assert(await page.locator('#debugTab').isDisabled());
 offline=true;await page.waitForFunction(()=>document.querySelector('#debugAvailability').textContent.includes('disconnected'));assert(await page.locator('#debugTab').isDisabled());
 assert(posts.every(p=>p.path==='/api/v1/control'||p.path==='/api/v1/debug'),'passive page must not send survey, GNSS or radio commands');assert.deepEqual(errors,[]);
 fs.writeFileSync(path.join(out,'debug-browser.json'),JSON.stringify({result:'PASS',checks:['gray tab and enable instructions','hardware-enabled availability','takeover without PIN','monitoring while occupation is active','polling does not renew idle timer','text escaping/filter/download/pause','controller loss clears private view','stale/offline and timeout disable access','role-specific update warnings and unavailable OTA button','320/390/768/1280 layouts','no active diagnostic or survey commands']},null,2));console.log('PASS: Debug navigation, passive browser, access/expiry, role warnings, downloads and responsive layout');
 }finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
