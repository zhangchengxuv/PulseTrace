'use strict';
// Controller is independent of the DOM so open/hide races and duplicate requests are testable.
globalThis.PulseOpenUpdates=class {
 constructor(request,notify,wait=ms=>new Promise(r=>setTimeout(r,ms))){this.request=request;this.notify=notify;this.wait=wait;this.running=null;this.generation=0;}
 stop(){this.generation++;}
 open(){if(this.running&&this.runningGeneration===this.generation)return this.running;const generation=this.generation;this.runningGeneration=generation;
  this.running=(async()=>{try{
   let s=await this.request('get_update_status');if(generation!==this.generation)return;
   if(s.busy&&s.phase==='downloading')return;
   if(!s.busy)s=await this.request('check_updates');
   while(s.busy||s.phase==='checking'){await this.wait(750);if(generation!==this.generation)return;s=await this.request('get_update_status');}
   if(generation===this.generation&&s.phase==='available'&&s.latest)this.notify(s);
  }catch(e){this.lastError=String(e.message||e);}finally{if(this.runningGeneration===generation)this.running=null;}})();return this.running;
 }
};
let updateNotice=null,updateNoticePoll=0;
function showUpdateNotice(s,validation=false){
 if(document.hidden||!nativeVisible||(state.self_test&&!validation)||updateNotice?.open)return;
 const dialog=el('dialog',{class:'update-notice','aria-label':tr('update.available')}),status=el('p',{role:'status'}),progress=el('progress',{max:1,value:0,hidden:''}),folder=el('button',{class:'secondary',hidden:'',onclick:()=>rpc('open_update_folder').catch(error)},tr('ui.5815ee0ded84'));
 const close=()=>{clearTimeout(updateNoticePoll);updateNoticePoll=0;dialog.close();dialog.remove();if(updateNotice===dialog)updateNotice=null;};
 const later=el('button',{class:'secondary',onclick:close},tr('update.later'));
 const download=el('button',{onclick:async()=>{download.disabled=true;try{await rpc('download_update');poll();}catch(e){status.textContent=tr('error.network');download.disabled=false;}}},tr('update.now'));
 async function poll(){clearTimeout(updateNoticePoll);if(!dialog.open||document.hidden||!nativeVisible)return;try{const value=await rpc('get_update_status');if(!dialog.open)return;progress.hidden=value.phase!=='downloading';progress.value=value.size?value.received/value.size:0;
  status.textContent=value.phase==='downloading'?bytes(value.received||0)+' / '+bytes(value.size||0):value.phase==='manual_install'?tr('ui.198d9866b1cd'):value.phase==='ready'?tr('ui.c1c79b63f418'):value.phase==='error'?tr('error.network'):'';
  folder.hidden=!value.downloaded;if(!value.busy){download.disabled=value.phase!=='available';return;}
 }catch(e){status.textContent=tr('error.network');return;}updateNoticePoll=setTimeout(poll,1000);}
 dialog.append(el('h3',{},tr('update.available')),el('p',{},tr('update.versions',{current:s.current||state.version,latest:s.latest})),el('p',{class:'muted'},tr('update.source',{source:s.source||'—',size:bytes(s.size||0)})),el('div',{class:'release-notes'},s.notes||tr('update.no_notes')),status,progress,el('div',{class:'actions'},download,later,folder));
 dialog.addEventListener('cancel',e=>{e.preventDefault();close();});document.body.append(dialog);updateNotice=dialog;dialog.showModal();later.focus();
}
function stopOpenUpdates(){openUpdates?.stop();clearTimeout(updateNoticePoll);updateNoticePoll=0;if(updateNotice?.open){updateNotice.close();updateNotice.remove();updateNotice=null;}}
let openUpdates=null,updateOpenPending=true;
function checkOpenUpdates(){if(state.self_test||!state.version||document.hidden||!nativeVisible)return;if(!openUpdates)openUpdates=new PulseOpenUpdates(rpc,showUpdateNotice);openUpdates.open();}
window.addEventListener('pagehide',stopOpenUpdates);

async function validateUpdateNotice(){
 if(!state.self_test)return;
 showUpdateNotice({current:state.version,latest:'9.0.0-self-test',source:'controlled fixture',size:1024,notes:'<script>must remain text</script>'},true);
 const d=updateNotice;if(!d?.open||d.querySelector('script')||d.querySelector('.release-notes').textContent!=='<script>must remain text</script>')throw new Error('Update notice content/escaping invalid');
 await new Promise(resolve=>requestAnimationFrame(resolve));const r=d.getBoundingClientRect();if(r.width>innerWidth||r.height>innerHeight)throw new Error('Update notice exceeds viewport');
 d.dispatchEvent(new Event('cancel',{cancelable:true}));if(updateNotice||d.isConnected)throw new Error('Update notice close leaked DOM');
}
