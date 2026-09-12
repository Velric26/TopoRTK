#pragma once
const char kDiagnosticPage[] PROGMEM=R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TopoRTK · Link test</title><style>
*{box-sizing:border-box}body{font:16px/1.5 system-ui;background:#eef2f5;color:#172d3b;margin:0}main{max-width:880px;margin:auto;padding:24px}section{background:white;border:1px solid #d2dde4;border-radius:14px;padding:20px;margin:18px 0}h1,h2{line-height:1.2}h1{margin-bottom:8px}h2{font-size:1.25rem;margin-top:0}label{display:block}input,select,button{font:inherit;padding:12px;border:1px solid #a6bac5;border-radius:7px;max-width:100%;min-height:48px}input,select{display:block;width:100%;background:white;color:inherit}button{background:#166546;color:white;cursor:pointer;margin:8px 8px 0 0}button.secondary{background:#edf3f6;color:#172d3b}button:disabled{opacity:.5;cursor:default}a{color:#155a77}.muted{color:#526674}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.check{display:flex;align-items:flex-start;gap:10px;margin-top:20px}.check input{width:24px;min-height:24px;flex:none;margin-top:1px}#connection{padding:12px;background:#e3eaf0;border-radius:8px}#message:empty{display:none}#message{border-left:4px solid #155a77;padding:10px}#result{font-size:1.15rem;font-weight:650}dl{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin:20px 0}dl div{padding:12px;background:#f2f6f8;border-radius:8px}dt{font-size:.85rem;color:#526674}dd{margin:4px 0 0;font-weight:650}p,dd{overflow-wrap:anywhere}progress{width:100%;height:12px;accent-color:#166546}input:focus-visible,select:focus-visible,button:focus-visible,a:focus-visible{outline:3px solid #2585b5;outline-offset:3px}[hidden]{display:none!important}@media(max-width:540px){main{padding:14px}section{padding:16px}.grid{grid-template-columns:1fr}dl{grid-template-columns:repeat(2,minmax(0,1fr))}}
</style></head><body><main>
<a href="/survey">← Survey</a><h1>Link test</h1><p class="muted">Runs on the two instruments. The tablet can disconnect during the test.</p>
<p id="connection" role="status">Connecting…</p><p id="message" role="alert"></p>
<section><h2>1. Prepare this instrument</h2><p>Keep antennas connected. The latest Take control request controls either instrument. Use the same test code and settings on both. Survey writes are locked during the test.</p>

<p id="controlState" class="muted">View only</p><button id="control" disabled>Take control</button><button id="release" class="secondary" disabled>Release control</button>
</section>
<section><h2>2. Match settings on Base and Rover</h2><div class="grid">
<label>Six-digit test code<input id="run" inputmode="numeric" pattern="[0-9]{6}" maxlength="6"></label>
<label>Link<select id="transport"><option value="sik">SiK radio</option><option value="wifi">Wi-Fi between instruments</option></select></label>
<label>Traffic<select id="mode"><option value="0">Base → Rover</option><option value="1">Rover → Base</option><option value="2">Both directions</option></select></label>
<label>Duration<select id="seconds"><option>30</option><option selected>60</option><option>120</option><option>300</option></select></label>
<label>Bytes per second, each sender<select id="rate"><option>200</option><option selected>1000</option><option>3000</option></select></label>
</div><p class="muted">Arm both within two minutes. The test starts when they find each other. Use a fresh code for each test.</p>
<label class="check"><input type="checkbox" id="confirm"> Antennas and wiring are ready; no survey occupation is active.</label>
<button id="arm" disabled>Arm this instrument</button><button id="cancel" class="secondary" disabled>Cancel test</button><button id="probe" class="secondary" disabled>Check radio wiring</button>
<p id="wiring" class="muted"></p></section>
<section><h2 id="reportTitle">3. Results</h2><p id="result" aria-live="polite">Waiting for the instrument…</p><p id="reportDetails" class="muted"></p><progress id="progress" max="100" value="0" hidden></progress>
<dl id="metrics" hidden><div><dt>Sent / expected</dt><dd id="sent"></dd></div><div><dt>Received / expected</dt><dd id="received"></dd></div><div><dt>Errors</dt><dd id="errors"></dd></div><div><dt>Duplicates</dt><dd id="duplicates"></dd></div><div><dt>Out of order</dt><dd id="reordered"></dd></div><div><dt>Longest receive gap</dt><dd id="gap"></dd></div></dl>
<p id="reportStatus" class="muted"></p><button id="download" disabled>Download report</button>
<p class="muted">Only the latest report is kept on each instrument. Download it before the next test. Missing peer results mean the pair has not passed.</p>
<p class="muted">Synthetic delivery test, not RTK accuracy. SiK: 57,600 baud. Bluetooth is not available yet.</p></section>
<section><h2>4. Local transport fault checks</h2><p>Check loss handling, corruption rejection, reassembly and recovery on this ESP32. No data is sent to the radios or receiver. This does not qualify the radio link.</p><button id="selftest" disabled>Run local fault checks</button><button id="selfdownload" class="secondary" disabled>Download self-test report</button><p id="selfresult" aria-live="polite">No local test report loaded.</p></section>
</main><script>
'use strict';
const $=id=>document.getElementById(id);
const random=()=>Array.from(crypto.getRandomValues(new Uint8Array(16)),v=>v.toString(16).padStart(2,'0')).join('');
function stored(key,fallback){try{return sessionStorage.getItem(key)||fallback}catch(e){return fallback}}
function remember(key,value){try{sessionStorage.setItem(key,value)}catch(e){}}
const client=stored('diagnosticClient',random());remember('diagnosticClient',client);
let token=stored('diagnosticToken',''),state=null,online=false,owner=false,requesting=false,lastUptime=null,lastAdvance=0;
$('run').value=stored('diagnosticRun',String(100000+crypto.getRandomValues(new Uint32Array(1))[0]%900000));
function message(s){$('message').textContent=s}
function report(){return state?.state==='idle'?state.last_report:state?.run?state:null}
function controls(){
  $('control').disabled=!online||requesting||owner;$('release').disabled=!online||requesting||!owner;
  $('controlState').textContent=owner&&online?'You control this instrument':'View only · Take control to start or cancel';
  for(const id of ['arm','probe','selftest'])$(id).disabled=!online||!owner||requesting||state?.busy;
  $('cancel').disabled=!online||!owner||requesting||!['armed','running'].includes(state?.state);
  for(const id of ['run','transport','mode','seconds','rate','confirm'])$(id).disabled=!!state?.busy||requesting;
  $('download').disabled=!report();$('selfdownload').disabled=!state?.self_test;
}
async function api(url,data){
  const abort=new AbortController(),timer=setTimeout(()=>abort.abort(),3500);
  try{
    const options={cache:'no-store',signal:abort.signal,headers:{Authorization:'Bearer '+token}};
    if(data!==undefined){options.method='POST';options.headers['Content-Type']='application/json';options.body=JSON.stringify(data)}
    const response=await fetch(url,options),body=await response.json();
    if(!response.ok){if(response.status===401){owner=false;token='';remember('diagnosticToken','')}throw Error(response.status===401?'Control expired or changed. Take control again.':body.error||'Request failed')}
    return {body,response};
  }finally{clearTimeout(timer)}
}
async function action(fn){requesting=true;controls();try{await fn()}catch(e){message(e.name==='AbortError'?'No reply. Check the live state before retrying.':e.message)}finally{requesting=false;controls()}}
$('control').onclick=()=>action(async()=>{
  const {body}=await api('/api/v1/control',{client});token=body.token;remember('diagnosticToken',token);owner=true;message('Control acquired.');
});
$('release').onclick=()=>action(async()=>{await api('/api/v1/control/release',{});token='';owner=false;remember('diagnosticToken','');message('Control released.')});
$('arm').onclick=()=>action(async()=>{
  if(!$('confirm').checked)throw Error('Confirm preparation first.');
  if(!/^[0-9]{6}$/.test($('run').value)||Number($('run').value)<100000)throw Error('Enter a test code from 100000 to 999999.');
  if(Number($('run').value)===state?.run)throw Error('Use a fresh test code for the next run.');
  remember('diagnosticRun',$('run').value);
  await api('/api/v1/diagnostic',{op:'arm',run:Number($('run').value),transport:$('transport').value,mode:Number($('mode').value),seconds:Number($('seconds').value),rate:Number($('rate').value),confirm:true});message('Arm request queued. Check for “Waiting for the other instrument” below.');
});
$('probe').onclick=()=>action(async()=>{if(!$('confirm').checked)throw Error('Confirm preparation first.');await api('/api/v1/diagnostic',{op:'probe',confirm:true});message('Wiring check queued. Watch the radio result below.')});
$('cancel').onclick=()=>action(async()=>{await api('/api/v1/diagnostic',{op:'cancel',run:state.run});message('Cancellation requested. Check for “Test stopped” below.')});
$('selftest').onclick=()=>action(async()=>{await api('/api/v1/diagnostic',{op:'selftest',confirm:true});message('Local fault checks requested. See their separate report below.')});
$('selfdownload').onclick=()=>{const d=state?.self_test;if(!d)return;const a=document.createElement('a'),u=URL.createObjectURL(new Blob([JSON.stringify(d,null,2)],{type:'application/json'}));a.href=u;a.download='TopoRTK-local-transport-'+d.run+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(u),1000)};
$('download').onclick=()=>{
  const d=report();if(!d)return;
  const a=document.createElement('a'),u=URL.createObjectURL(new Blob([JSON.stringify(d,null,2)],{type:'application/json'}));
  a.href=u;a.download='TopoRTK-link-'+d.run+'-'+(d.role||'instrument').toLowerCase()+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(u),1000);
};
const descriptions={waiting_for_other_instrument:'Waiting for the other instrument. Match the test code and settings, then arm it.',peer_timeout:'No matching peer arrived within two minutes.',cancelled:'Cancelled on this instrument.',peer_aborted:'The other instrument stopped the test.',instrument_restarted_during_test:'The instrument restarted before completion.',finish_survey_or_receiver_operation_first:'Finish the survey or receiver operation before arming.',cannot_save_test_start:'The test could not start because its restart marker could not be saved.'};
function render(){
  $('connection').textContent='Connected · '+(state.role==='BASE'?'Base':'Rover');
  $('wiring').textContent='Radio wiring: '+state.radio_probe;
  const self=state.self_test;$('selfresult').textContent=state.self_test_error||(self?(self.passed?'PASS · '+self.checks+' local checks passed.':self.state==='interrupted'?'Interrupted · The instrument restarted before the self-test finished.':'Local self-test failed.')+(self.checks?' Memory '+self.workspace_bytes+' bytes; '+(self.duration_us/1000).toFixed(1)+' ms.':'')+' Radio qualification is separate.':'No local test has run.');
  const d=report(),saved=state.state==='idle'&&!!d;
  if(($('message').textContent.startsWith('Arm request queued')&&state.run===Number($('run').value)&&['armed','running','done','failed'].includes(state.state))||($('message').textContent.startsWith('Wiring check queued')&&state.state==='probing')||($('message').textContent.startsWith('Cancellation requested')&&state.state==='failed'))message('');
  $('reportTitle').textContent=saved?'3. Saved report':'3. Results';
  $('progress').hidden=state.state!=='running';$('progress').value=state.seconds?(state.seconds-state.remaining_seconds)*100/state.seconds:0;
  if(state.state==='probing')$('result').textContent='Checking the local radio UART…';
  else if(!d)$('result').textContent=descriptions[state.reason]||'Ready. No test has been started.';
  else if(d.state==='done')$('result').textContent=d.pair_pass?'PASS · Both instruments received every test packet.':d.local_pass&&!d.peer_report_received?'Local checks passed. Waiting for the peer report; the pair has not passed.':'Not passed · Check the counters and peer report.';
  else if(d.state==='running')$('result').textContent=state.remaining_seconds?'Running · '+state.remaining_seconds+' seconds remaining':'Starting or finishing packet checks…';
  else if(d.state==='armed')$('result').textContent=descriptions.waiting_for_other_instrument;
  else $('result').textContent='Test stopped · '+(descriptions[d.reason]||d.reason||d.state);
  $('reportDetails').textContent=d?'Test '+d.run+' · '+(d.role||state.role)+' · '+(d.transport==='sik'?'SiK radio':d.transport==='wifi'?'Wi-Fi':'interrupted')+(d.seconds?' · '+d.seconds+' s · '+d.rate+' bytes/s':''):'';
  $('metrics').hidden=!d||d.state==='interrupted';
  if(d){$('sent').textContent=(d.sent||0)+' / '+(d.expected_tx||0);$('received').textContent=(d.received||0)+' / '+(d.expected_rx||0);for(const id of ['errors','duplicates','reordered'])$(id).textContent=d[id]||0;$('gap').textContent=(d.max_gap_ms||0)+' ms'}
  $('reportStatus').textContent=!d?'':saved?'Restored from instrument storage.':state.busy?'Test runs on the instrument if this browser disconnects.':state.persisted?'Saved on the instrument.':'Report has not been confirmed saved; download it now.';
  controls();
}
async function poll(){
  try{
    const {body,response}=await api('/api/v1/diagnostic');
    if(!Number.isFinite(body.uptime_ms))throw Error('No fresh instrument state');
    if(body.uptime_ms!==lastUptime){lastUptime=body.uptime_ms;lastAdvance=Date.now()}
    if(Date.now()-lastAdvance>3500)throw Error('Instrument state stopped updating');
    state=body;online=true;owner=response.headers.get('X-Controller')==='true';render();
  }catch(e){online=false;owner=false;$('connection').textContent='Disconnected or stale · Last received results shown. Reconnect to retrieve the final report.';controls()}
  finally{setTimeout(poll,1000)}
}
poll();
</script></body></html>)HTML";
