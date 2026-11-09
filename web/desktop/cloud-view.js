'use strict';
let cloudPoll=0;
function cloudError(code){
 const key=/RUNTIME/.test(code)?'runtime':/AUTH/.test(code)?'auth':/PASSWORD|INTEGRITY|CRYPTO/.test(code)?'password':/RATE/.test(code)?'rate':/STORAGE|WRITE/.test(code)?'space':/NETWORK|TLS|TIMEOUT|INTERRUPTED/.test(code)?'network':/CREDENTIAL|CONFIG|HTTPS|DIRECTORY|INTERVAL|ACCOUNT/.test(code)?'config':/SNAPSHOT|DATABASE/.test(code)?'snapshot':/CANCEL/.test(code)?'cancelled':'generic';
 return tr('cloud.error.'+key);
}
function renderCloudSettings(group){
 clearTimeout(cloudPoll);
 const root=el('div',{class:'cloud-settings'}),form=el('form',{class:'settings-grid'}),fields={},result=el('div',{class:'cloud-status',role:'status'}),feedback=el('p',{class:'muted',role:'status'});
 const add=(key,type,value,options={})=>{const n=el('input',{type,value,...options,'aria-label':tr('cloud.'+key)});fields[key]=n;form.append(el('label',{class:type==='checkbox'?'checkbox':''},type==='checkbox'?n:el('span',{},tr('cloud.'+key)),type==='checkbox'?tr('cloud.'+key):n));return n;};
 add('enabled','checkbox','');add('automatic','checkbox','');add('account','text','',{autocomplete:'off',maxlength:320,disabled:true});add('app_password','password','',{autocomplete:'new-password',maxlength:1024,disabled:true});
 add('server','url','https://dav.jianguoyun.com/dav/',{required:true,maxlength:2048});add('directory','text','PulseTrace/Backups',{required:true,maxlength:512});
 add('encryption_password','password','',{autocomplete:'new-password',maxlength:1024,minlength:12,disabled:true});add('interval_minutes','number','1440',{min:60,max:43200,step:1,required:true});
 let initialized=false,status={},listKey='';const files=el('select',{'aria-label':tr('cloud.backups')}),confirmText=el('input',{placeholder:'RESTORE','aria-label':tr('cloud.confirm_text'),autocomplete:'off'});
 const buttons=[];
 async function save(){if(!form.checkValidity()){form.reportValidity();return false;}const params={};for(const [key,n]of Object.entries(fields))params[key]=n.type==='checkbox'?n.checked:n.type==='number'?Number(n.value):n.value.trim();
  // Password spaces are significant; never trim a user's secret.
  params.app_password=fields.app_password.value;params.encryption_password=fields.encryption_password.value;
  params.server=params.server.replace(/\/?$/,'/');params.directory=params.directory.replace(/^\/+|\/+$/g,'');
  await rpc('cloud_configure',params);fields.app_password.value='';fields.encryption_password.value='';feedback.textContent=tr('cloud.saved');await poll();return true;
 }
 const action=(label,kind,params=()=>({}),saveFirst=true)=>{const button=el('button',{type:'button',class:'secondary',onclick:async()=>{try{if(saveFirst&&!await save())return;await rpc(kind,params());await poll();}catch(e){feedback.textContent=cloudError(e.code||'');}}},tr(label));buttons.push(button);return button;};
 const test=action('cloud.test','cloud_test'),backup=action('cloud.backup','cloud_backup'),list=action('cloud.list','cloud_list'),cancel=action('cloud.cancel','cloud_cancel',()=>({}),false);
 const prepare=action('cloud.prepare','cloud_prepare_restore',()=>({name:files.value}));
 const restore=el('button',{class:'danger',type:'button',onclick:async()=>{try{if(confirmText.value!=='RESTORE'||!status.restore_token){feedback.textContent=tr('cloud.confirm_text');return;}await rpc('cloud_confirm_restore',{token:status.restore_token,confirmation:confirmText.value});confirmText.value='';await poll();}catch(e){feedback.textContent=cloudError(e.code||'');}}},tr('cloud.confirm_restore'));
 const cancelRestore=action('cloud.cancel_restore','cloud_cancel_restore',()=>({}),false),records=el('div');
 const saveButton=el('button',{type:'submit'},tr('cloud.save'));form.onsubmit=async e=>{e.preventDefault();try{await save();}catch(failure){feedback.textContent=cloudError(failure.code||'');}};
 form.append(el('div',{class:'actions settings-form-actions'},saveButton),feedback);
 const restoreHint=el('p',{class:'muted'}),restorePanel=el('div',{class:'cloud-restore'},el('p',{class:'muted'},tr('cloud.restore_hint')),files,el('div',{class:'actions'},prepare),restoreHint,confirmText,el('div',{class:'actions'},restore,cancelRestore));
 root.append(panel(tr('cloud.title'),form,el('p',{class:'cloud-encryption',title:tr('cloud.encrypted'),tabindex:0},tr('cloud.encrypted_short')),help(tr('ui.details'),el('p',{},tr('cloud.intro'))),el('p',{class:'muted'},tr('cloud.password_hint')),el('div',{class:'actions'},test,backup,list,cancel),result),disclosure('settings.cloud.restore',tr('cloud.restore'),null,restorePanel),disclosure('settings.cloud.history',tr('cloud.records'),null,records));
 group.append(root);group.onSettingsShown=()=>poll();
 async function poll(){
  clearTimeout(cloudPoll);if(!root.isConnected||page!=='settings'||document.hidden||!nativeVisible||root.closest('[hidden]'))return;
  try{
   status=await rpc('cloud_status');if(!root.isConnected)return;
   if(!initialized){for(const [key,n]of Object.entries(fields)){if(n.type==='password')continue;const value=key==='account'?status.account:status.config?.[key];if(n.type==='checkbox')n.checked=!!value;else if(value!=null)n.value=String(value);}initialized=true;}
   fields.app_password.placeholder=tr(status.credentials_saved?'cloud.stored':'cloud.app_password');fields.encryption_password.placeholder=tr(status.encryption_password_saved?'cloud.stored':'cloud.encryption_password');
   result.replaceChildren(...[el('p',{},tr('cloud.phase.'+(['idle','working','success','error','cancelled','restore_ready','restart_required'].includes(status.phase)?status.phase:'idle'))),status.error?el('p',{class:'capture-error'},cloudError(status.error)):null,status.last_backup_ms?el('p',{class:'muted'},tr('cloud.last_backup')+' '+new Date(status.last_backup_ms).toLocaleString(PulseI18n.locale)):null,status.pending_restore?el('p',{class:'cloud-pending'},tr('cloud.pending')):null,status.restore_result?.phase==='restore_error'?el('p',{class:'capture-error'},tr('cloud.restore_failed')):null,status.restore_result?.phase==='restored'?el('p',{class:'muted'},tr('cloud.restored')):null].filter(Boolean));
   for(const b of buttons)b.disabled=!!status.busy;cancel.disabled=!status.busy;saveButton.disabled=!!status.busy||status.secure_forms!==true;for(const key of ['account','app_password','encryption_password'])fields[key].disabled=status.secure_forms!==true;if(status.secure_forms!==true)feedback.textContent=tr('cloud.error.runtime');
   restore.disabled=!!status.busy||!status.restore_token;confirmText.disabled=!status.restore_token;cancelRestore.disabled=!!status.busy||(!status.pending_restore&&!status.restore_token);prepare.disabled=!!status.busy||!files.value||!!status.pending_restore;
   restoreHint.textContent=status.restore_token?tr('cloud.restore_ready',{version:status.restore_version,time:new Date(status.restore_created_ms).toLocaleString(PulseI18n.locale)}):'';
   const key=JSON.stringify(status.files||[]);if(key!==listKey){const selected=files.value;files.replaceChildren(el('option',{value:''},tr('cloud.choose_backup')),...(status.files||[]).map(name=>el('option',{value:name},name)));if((status.files||[]).includes(selected))files.value=selected;listKey=key;}
   files.onchange=()=>{prepare.disabled=!!status.busy||!files.value||!!status.pending_restore;};
   records.replaceChildren(table([tr('cloud.time'),tr('cloud.operation'),tr('cloud.result'),tr('cloud.backups')],(status.history||[]).slice().reverse().map(item=>[new Date(item.time_ms).toLocaleString(PulseI18n.locale),tr('cloud.op.'+({cloud_test:'test',cloud_backup:'backup',cloud_list:'list',cloud_prepare_restore:'restore',cloud_confirm_restore:'restore'}[item.operation]||'backup')),item.result==='verified'?tr('cloud.verified'):item.result==='restart_required'?tr('cloud.phase.restart_required'):cloudError(item.result||''),item.name||'—'])));
  }catch(failure){result.textContent=cloudError(failure.code||'');}
  if(root.isConnected&&page==='settings'&&!root.closest('[hidden]'))cloudPoll=setTimeout(poll,1500);
 }
 queueMicrotask(poll);
}
