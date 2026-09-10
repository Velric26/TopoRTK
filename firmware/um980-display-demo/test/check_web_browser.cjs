// NODE_PATH must resolve Playwright. Uses an isolated headless Edge profile.
// Run after run_host_tests.py: node test/check_web_browser.cjs [Rover URL]
const {chromium} = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const output = path.resolve(root, '../..', process.env.TOPORTK_TEST_RECORD || 'tests/2026-09-10-rover-web-status');
fs.mkdirSync(output, {recursive:true});
const html = fs.readFileSync(path.join(root,'src/web_ui.h'),'utf8').split('R"TOPOHTML(')[1].split(')TOPOHTML"')[0];
const ready = JSON.parse(fs.readFileSync(path.join(root,'.pio/status-ready.json'),'utf8'));
const stale = JSON.parse(fs.readFileSync(path.join(root,'.pio/status-stale.json'),'utf8'));
(async () => {
  const browser = await chromium.launch({channel:'msedge', headless:true});
  try {
    const page = await browser.newPage({viewport:{width:1200,height:1000}});
    const errors=[]; page.on('pageerror', e => errors.push(e.message));
    const liveUrl = process.argv[2];
    if (liveUrl) {
      await page.goto(liveUrl);
      await page.waitForFunction(() => document.querySelector('#connection').textContent === 'Live from Rover');
      await page.locator('summary').filter({hasText:'Connect a phone or tablet'}).click();
      assert.match(await page.locator('#phone-url').textContent(), /^http:\/\//);
      await page.locator('summary').filter({hasText:'Connect a phone or tablet'}).click();
      const packets = Number(await page.locator('#packets').textContent());
      await page.waitForFunction(old => Number(document.querySelector('#packets').textContent) > old, packets);
      await page.screenshot({path:path.join(output,'live-desktop.png'),fullPage:true});
      await page.setViewportSize({width:390,height:844});
      await page.screenshot({path:path.join(output,'live-phone-width.png'),fullPage:true});
      for (let i=0; i<3; i++) {
        await page.reload();
        await page.waitForFunction(() => document.querySelector('#connection').textContent === 'Live from Rover');
      }
    }
    // Controlled snapshots test failure states without altering real hardware.
    let mode = 'ready', sample = 10000;
    const requests = [];
    await page.route('**/*', async route => {
      const req = route.request(); requests.push({url:req.url(),method:req.method()});
      if (req.url().endsWith('/api/v1/status')) {
        if (mode === 'offline') return route.abort();
        if (mode === 'bad') return route.fulfill({body:'invalid JSON',contentType:'application/json'});
        const s = structuredClone(mode === 'stale' ? stale : ready);
        s.uptime_ms = mode === 'frozen' ? 20000 : ++sample;
        return route.fulfill({json:s});
      }
      return route.fulfill({body:html,contentType:'text/html'});
    });
    await page.goto('http://rover.test/');
    const title = text => page.waitForFunction(t => document.querySelector('#ready-title').textContent === t,text,{timeout:7000});
    await title('READY');
    for (const width of [320,390,768,1200]) {
      await page.setViewportSize({width,height:1000});
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `overflow at ${width}`);
      assert(await page.locator('#link').evaluate(node => {
        const range = document.createRange(); range.selectNodeContents(node);
        return range.getClientRects().length === 1;
      }), `broken connection label at ${width}`);
    }
    mode = 'stale'; await title('NOT READY');
    assert.equal(await page.locator('#fix').textContent(),'GNSS STALE');
    mode = 'ready'; await title('READY');
    mode = 'offline'; await title('STATUS UNAVAILABLE');
    assert.equal(await page.locator('#readiness').getAttribute('data-ready'),'false');
    for (const id of ['fix','link','accuracy','quality']) assert.equal(await page.locator('#'+id).textContent(),'—');
    await page.screenshot({path:path.join(output,'simulated-disconnect.png'),fullPage:true});
    mode = 'ready'; await title('READY');
    mode = 'frozen'; await title('STATUS UNAVAILABLE');
    mode = 'ready'; await title('READY');
    mode = 'bad'; await title('STATUS UNAVAILABLE');
    mode = 'ready'; await page.locator('#refresh').click(); await title('READY');
    assert.deepEqual(errors,[]);
    assert(requests.every(r => r.method === 'GET' && r.url.startsWith('http://rover.test/')));
    const result = {result:'PASS',browser:await browser.version(),liveUrl:liveUrl||null,
      checks:[...(liveUrl ? ['live page and advancing counters','three real browser reloads'] : []),'320/390/768/1200 px no overflow',
        'stale GNSS','disconnect clears ready and metrics','frozen snapshot','invalid JSON','automatic and manual recovery',
        'no JavaScript errors','only local GET requests']};
    fs.writeFileSync(path.join(output,'browser-results.json'),JSON.stringify(result,null,2)+'\n');
    console.log(JSON.stringify(result,null,2));
  } finally { await browser.close(); }
})().catch(error => {console.error(error);process.exitCode=1;});
