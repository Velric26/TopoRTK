(()=>{
'use strict';
let selected=null,update=null,armed=false,oldBoot=null,targetVersion='',awaitingBoot=false;
window.otaUploading=false;
function updateControls(){
 const available=online&&enabled&&owner&&!requesting&&!window.otaUploading;
 const busy=update?.locked;
 $('firmwareFile').disabled=!available||busy;
 $('reviewFirmware').disabled=!available||!selected||busy||!update?.available;
 $('confirmFirmware').disabled=!available||update?.state!=='review'||!$('acceptInterruption').checked||(!update.peer_acknowledged&&!$('allowUnconfirmed').checked);
 $('cancelFirmware').disabled=!available||!busy||['uploading','restarting'].includes(update?.state);
 $('allowPeerWarning').hidden=update?.state!=='review';
}
// The Debug page renders page state and then calls this hook, so the update card
// follows owner, request and upload changes without patching that page's controls().
window.updateControls=updateControls;
$('acceptInterruption').onchange=updateControls;$('allowUnconfirmed').onchange=updateControls;
$('firmwareFile').onchange=async()=>{
 selected=null;armed=false;$('acceptInterruption').checked=false;$('allowUnconfirmed').checked=false;
 try{
  const file=$('firmwareFile').files[0];if(!file)throw Error('Choose a .tpk firmware package.');
  const bytes=new Uint8Array(await file.slice(0,128).arrayBuffer());
  const view=new DataView(bytes.buffer),version=new TextDecoder().decode(bytes.slice(48,80)).split('\0')[0];
  if(bytes.length!==128||String.fromCharCode(...bytes.slice(0,4))!=='TPK1'||bytes[4]!==1||bytes[6]!==1||![1,2].includes(bytes[5])||view.getUint32(8,true)+128!==file.size||!version)throw Error('Invalid TopoRTK package. Use the packaged application, not a raw or merged binary.');
  if(update&&bytes[5]!==update.unit)throw Error('This package targets the other instrument.');
  selected={file,header:Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join(''),version};
  $('firmwareReview').textContent='Unit '+String.fromCharCode(64+bytes[5])+' · Version '+version+' · '+Math.ceil(file.size/1024)+' KiB. The instrument verifies the full image before selecting it for boot.';
 }catch(e){$('firmwareReview').textContent=e.message;}updateControls();
};
$('reviewFirmware').onclick=()=>action(async()=>{if(!selected)return;const r=await topoRequest('/api/v1/update',{op:'prepare',header:selected.header},{timeout:3500});if(!r.ok)throw Error(r.body?.error||'Request failed');$('otaProgress').textContent='Reserving the instrument and notifying its peer. Normal forwarding continues until you confirm.';});
$('cancelFirmware').onclick=()=>action(async()=>{armed=false;const r=await topoRequest('/api/v1/update',{op:'cancel'},{timeout:3500});if(!r.ok)throw Error(r.body?.error||'Request failed');});
$('confirmFirmware').onclick=()=>action(async()=>{
 if(!selected||!$('acceptInterruption').checked)return;
 const r=await topoRequest('/api/v1/update',{op:'start',confirm:true,allow_unconfirmed:$('allowUnconfirmed').checked},{timeout:3500});if(!r.ok)throw Error(r.body?.error||'Request failed');
 oldBoot=update.boot_id;targetVersion=selected.version;armed=true;
 $('otaProgress').textContent='Pausing local operations and confirming the update notice…';
});
// The card title carries the live percentage while bytes are in flight (R7b).
function setHeading(percent){$('updateHeading').textContent='Updating Firmware'+(percent==null?'':' – '+percent+'%');}
function upload(){
 armed=false;window.otaUploading=true;window.pauseDebugRequests();controls();
 const xhr=new XMLHttpRequest();xhr.open('POST','/api/v1/update/upload');xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.setRequestHeader('Authorization','Bearer '+topoToken());xhr.timeout=130000;
 xhr.upload.onprogress=e=>{const percent=e.lengthComputable?Math.floor(e.loaded/e.total*100):null;setHeading(percent);$('otaProgress').textContent='Uploading '+(percent==null?'firmware':percent+'%')+' · Keep power connected. Verification and reboot follow.';};
 xhr.onload=()=>{window.otaUploading=false;setHeading();if(xhr.status===200){awaitingBoot=true;$('otaProgress').textContent='Image accepted. Waiting for a new boot and startup verification…';}else{$('otaProgress').textContent='Upload rejected. Reconnect and check update status before retrying.';}controls();};
 xhr.onerror=xhr.ontimeout=()=>{window.otaUploading=false;setHeading();awaitingBoot=true;$('otaProgress').textContent='Connection interrupted. Update outcome is unknown until the instrument reconnects.';controls();};
 xhr.send(selected.file);
}
async function pollUpdate(){
 try{
  if(window.otaUploading)return;
  const r=await topoRequest('/api/v1/update',undefined,{timeout:3000});if(!r.ok)throw Error();update=r.body;
  $('peerUpdate').textContent=update.peer_status||'Paired unit: no update notice.';
  $('otaState').textContent='Update: '+update.state+' · '+update.boot;
  setHeading(update.state==='uploading'&&update.total>0?Math.min(100,Math.floor(update.received/update.total*100)):null);
  if(update.state==='review')$('otaProgress').textContent=update.peer_acknowledged?'Peer acknowledged preparation. Review the interruption warning, then confirm to begin.':'Peer notification unconfirmed. The other unit may show ordinary link loss; explicit acknowledgement below is required to proceed.';
  if(update.state==='failed'){armed=false;awaitingBoot=false;$('otaProgress').textContent=update.error||'Update stopped. Current firmware retained.';}
  if(update.state==='ready'&&armed&&selected)upload();
  if(awaitingBoot&&update.boot_id!==oldBoot){
   if(update.firmware===targetVersion&&update.boot==='New firmware verified'){owner=false;report=null;controls();$('otaProgress').textContent='Update complete. New firmware passed startup checks. Debug is On after restart (default).';awaitingBoot=false;}
   else if(update.boot.includes('rollback')){$('otaProgress').textContent='Update rolled back. Previous firmware restored; review diagnostics before retrying.';awaitingBoot=false;}
   else $('otaProgress').textContent='Instrument reconnected: '+update.boot+'. Waiting for verified startup.';
  }
 }catch{if(!window.otaUploading)$('otaState').textContent='Update status unavailable. Keep power on if an update has started.';}
 finally{updateControls();setTimeout(pollUpdate,1000)}
}
pollUpdate();
})();