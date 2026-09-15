// Read-only checks against an installed instrument with Debug physically Off.
// No takeover, receiver commands, firmware writes or survey mutations.
const {chromium}=require('playwright'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const [base,unit]=process.argv.slice(2);if(!base||!['A','B'].includes(unit))throw Error('Usage: node check_debug_installed.cjs http://instrument UnitLetter');
const out=path.resolve(__dirname,'../../tests/2026-09-14-ota-usb');fs.mkdirSync(out,{recursive:true});
(async()=>{const browser=await chromium.launch({channel:'msedge',headless:true});try{
 const page=await browser.newPage({viewport:{width:390,height:844}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto(base+'/survey');await page.waitForFunction(()=>document.querySelector('#debugAvailability')?.textContent.includes('Enable Debug'));
 assert(await page.locator('#debugTab').isDisabled());
 await page.goto(base+'/debug');await page.waitForFunction(unit=>document.querySelector('#identity')?.textContent.includes('Unit '+unit),unit);
 await page.waitForFunction(()=>document.querySelector('#otaState').textContent.includes('idle'));
 assert.match(await page.locator('#connection').innerText(),/0\.11\.0/);
 assert.match(await page.locator('#mode').innerText(),/Debug unavailable/);
 for(const id of ['claim','firmwareFile','reviewFirmware','confirmFirmware'])assert(await page.locator('#'+id).isDisabled(),id);
 assert.deepEqual(errors,[]);
 await page.screenshot({path:path.join(out,'unit-'+unit.toLowerCase()+'-debug-off.png'),fullPage:true});
 fs.writeFileSync(path.join(out,'unit-'+unit.toLowerCase()+'-browser.json'),JSON.stringify({result:'PASS',unit,checks:['live Survey Debug tab disabled','touchscreen enable instructions','live Debug page version 0.11.0','OTA idle','takeover and upload controls unavailable while Debug Off','no JavaScript errors']},null,2));
 console.log('PASS: Unit '+unit+' installed Debug/OTA UI, physical-enable gate and live JavaScript');
 }finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
