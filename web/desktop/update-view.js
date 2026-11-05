'use strict';
let updatePoll=0;
function renderUpdateSettings(c){
 clearTimeout(updatePoll);
 const cfg=state.config||{},channel=el('select',{'aria-label':tr("ui.b5260335fcac")},el('option',{value:'stable'},tr("ui.320e85714d07")),el('option',{value:'preview'},tr("ui.f8ba3e6e5825")));
 channel.value=cfg.update_channel||'stable';
 const source=el('select',{'aria-label':tr("ui.de0e6877cfcb")},... [['auto',tr("ui.244ea8172916")],['github','GitHub'],['gitee','Gitee']].map(([value,title])=>el('option',{value},title)));source.value=cfg.update_source||'auto';
 const startup=el('input',{type:'checkbox',checked:cfg.check_updates_on_start||false}),result=el('div',{'aria-live':'polite'}),progress=el('progress',{max:'1',value:'0','aria-label':tr("ui.fad86bf65f72")});
 const check=el('button',{onclick:async()=>{try{await save();await rpc('check_updates');await poll();}catch(e){error(e);}}},tr("ui.7f68ebad19ba")),download=el('button',{hidden:'',onclick:async()=>{try{await rpc('download_update');await poll();}catch(e){error(e);}}},tr("ui.a78b542de282")),cancel=el('button',{class:'secondary',hidden:'',onclick:async()=>{try{await rpc('cancel_update');}catch(e){error(e);}}},tr("ui.2cd0f3be8738"));
 const folder=el('button',{class:'secondary',hidden:'',onclick:()=>rpc('open_update_folder').catch(error)},tr("ui.5815ee0ded84"));
 const statuses={idle:tr("ui.cdbc0434f568"),checking:tr("ui.d85ff08f9141"),up_to_date:tr("ui.c11359e9416f"),available:tr("ui.e4d244d6bacf"),downloading:tr("ui.06b2117d58cc"),manual_install:tr("ui.c6d316acd1dc"),ready:tr("ui.c1c79b63f418"),cancelled:tr("ui.a37778f17c5f"),error:tr("ui.ec99e5c45d64")};
 async function save(){await rpc('set_config',{update_channel:channel.value,update_source:source.value,check_updates_on_start:startup.checked});state.config={...state.config,update_channel:channel.value,update_source:source.value,check_updates_on_start:startup.checked};}
 async function poll(){clearTimeout(updatePoll);if(!result.isConnected||page!=='settings'||document.hidden||!nativeVisible)return;try{const s=await rpc('get_update_status');if(!result.isConnected)return;check.disabled=!!s.busy;download.hidden=s.phase!=='available';cancel.hidden=!s.busy;folder.hidden=!s.downloaded;progress.hidden=s.phase!=='downloading';progress.value=s.size?s.received/s.size:0;
  result.replaceChildren(...[el('p',{},statuses[s.phase]||s.phase),el('p',{class:'muted'},tr("ui.b841058cf971")+(s.current||state.version)+(s.latest?' → '+s.latest:'')),s.last_check_ms?el('small',{},tr("ui.191a365ecef9")+new Date(s.last_check_ms).toLocaleString(PulseI18n.locale)):null,s.error?el('div',{},el('p',{class:'capture-error'},tr('error.network')),help(tr('error.details'),el('p',{},s.error))):null].filter(Boolean));
  if(s.size)result.append(el('p',{class:'muted'},bytes(s.received||0)+' / '+bytes(s.size)));
  if(s.notes)result.append(help(tr("ui.de6811e73d40"),el('p',{class:'release-notes'},s.notes)));
  if(s.providers?.length)result.append(help(tr("ui.1062709931cc"),...s.providers.map(p=>el('p',{},p.source+' · '+(p.ok?(p.version||tr("ui.1a7405b63614")):(p.error||tr("ui.460b3574e4bd")))))));
  if(s.partial_sources)result.append(el('small',{},tr("ui.9f784efed0ad")));
  if(s.phase==='manual_install')result.append(el('p',{class:'muted'},tr("ui.198d9866b1cd")));
 }catch(e){if(result.isConnected)result.textContent=e.message;}if(result.isConnected&&page==='settings'&&!document.hidden&&nativeVisible)updatePoll=setTimeout(poll,1000);}
 c.append(panel(tr("ui.651f27447015"),el('div',{class:'settings-grid'},el('label',{},tr("ui.0c04098e48ab"),channel),el('label',{},tr("ui.a488e93d69cc"),source),el('label',{class:'checkbox'},startup,tr("ui.e8192e07126c"))),el('div',{class:'actions'},check,download,cancel,folder,el('button',{class:'secondary',onclick:()=>rpc('open_release_page').catch(error)},tr("ui.5c5a53a77586")),el('button',{class:'secondary',onclick:()=>save().then(()=>{result.textContent=tr("ui.1bd91a7d0c53");}).catch(error)},tr("ui.b2f5b0da3e67"))),progress,result));
 c.append(panel(tr("ui.0fe8acdee57b"),el('p',{},'PulseTrace '+state.version),el('p',{class:'muted'},state.installation_mode==='installed'?tr("ui.197d648a8b9a"):tr("ui.bd44514fa3e9")),help(tr("ui.4108a4d106f2"),el('p',{class:'release-notes'},state.data_directory||'')),el('p',{class:'muted'},tr("ui.9b67b1c8578b")+(state.database_schema??tr("ui.d80f8bbe1ddf"))),help(tr("ui.146b8706945f"),el('pre',{},JSON.stringify(state.database_migration||{},null,2))),el('div',{class:'actions'},el('button',{class:'secondary',onclick:()=>rpc('open_data_folder').catch(error)},tr("ui.da99ea1331dc")),el('button',{class:'secondary',onclick:async()=>{try{const r=await rpc('backup_database');result.textContent=tr("ui.f61a48aa6844")+r.directory;}catch(e){error(e);}}},tr("ui.4e7b4b14507c")))));
 // The panel is attached by the caller after this function returns.
 queueMicrotask(poll);
}
document.addEventListener('visibilitychange',()=>{if(document.hidden){clearTimeout(updatePoll);updatePoll=0;}else if(page==='settings')refresh(true);});
