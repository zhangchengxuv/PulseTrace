'use strict';
async function validateSettingsCategories(){
 if(!state.self_test||page!=='settings')return;
 const initial=settingsActive,before=await rpc('get_status'),buttons=[...document.querySelectorAll('[data-settings-tab]')];
 if(buttons.length!==8)throw Error('Settings categories missing');
 try{for(const button of buttons){
  button.click();await new Promise(resolve=>requestAnimationFrame(resolve));
  const key=button.dataset.settingsTab,visible=[...document.querySelectorAll('[data-settings-group]')].filter(node=>!node.hidden);
  if(visible.length!==1||visible[0].dataset.settingsGroup!==key)throw Error('Settings category visibility mismatch');
  if(visible[0].scrollWidth>visible[0].clientWidth+2)throw Error('Settings category overflow: '+key);
  if(key==='cloud'){
   if(document.querySelector('.cloud-settings input[type=url]').value!=='https://dav.jianguoyun.com/dav/')throw Error('Default WebDAV server missing');
   if(document.querySelectorAll('.cloud-settings input[type=password]').length!==2)throw Error('Cloud secrets must use password controls');
   const status=await rpc('cloud_status');if(!status.secure_forms)throw Error('WebView password saving/autofill must be disabled');if(status.config.enabled||status.credentials_saved)throw Error('Cloud must remain off and unconfigured in isolated tests');
  }
  if(['general','taskbar','campus','cloud'].includes(key))await rpc('selftest_snapshot',{phase:'settings-'+key});
 }}finally{document.querySelector('[data-settings-tab="'+initial+'"]').click();}
 const after=await rpc('get_status');if(before.session!==after.session||before.monitoring!==after.monitoring)throw Error('Settings navigation restarted monitoring');
}
function validateCompactPages(){
 if(!state.self_test)return;
 if(page==='network'&&document.querySelector('.traffic-ranking'))throw Error('Removed network ranking is still mounted');
 if(page==='keyboard'){
  const node=document.querySelector('[data-disclosure="keyboard.ranking"]');
  if(!node)throw Error('Keyboard ranking disclosure missing');
  const previous=disclosureState.get('keyboard.ranking'),open=node.open;
  if(!previous&&open)throw Error('Keyboard ranking must start collapsed');
  node.open=true;node.dispatchEvent(new Event('toggle'));
  if(!disclosure('keyboard.ranking','fixture',null).open)throw Error('Keyboard ranking state lost during redraw');
  node.open=open;if(previous)disclosureState.set('keyboard.ranking',previous);else disclosureState.delete('keyboard.ranking');
 }
}
async function validateCampusOverview(){
 if(!state.self_test||!['dashboard','network'].includes(page))return;
 // Memory-only synthetic display fixtures. Never configure/open a real portal,
 // persist a mapping, or write a snapshot through the native application.
 const saved={data:campusData,fields:campusFieldState,status:campusEventStatus,event:campusLastEventType,config:state.config,scroll:window.scrollY};
 const host=document.querySelector('.campus-overview'),content=$('content'),first=content.firstChild;
 if(!host)throw Error('Campus overview missing from '+page);
 const tick=()=>new Promise(resolve=>requestAnimationFrame(resolve));
 try{
  campusFieldState={};campusEventStatus='';campusLastEventType='';
  const fields=Object.fromEntries(['balance','used_bytes','free_quota_bytes','used_free_bytes','free_remaining_bytes'].map(field=>[field,{mode:'auto',kind:field==='balance'?'currency':'bytes',selector:'#'+field}]));
  fields.spendable_balance={mode:'formula',kind:'currency',formula:'balance'};
  for(const scenario of ['disabled','empty','partial','full','auth','error']){
   state.config={...saved.config,campus_enabled:scenario!=='disabled',campus_unit_price:scenario==='partial'?0:.5,campus_price_unit:'GiB'};
   campusData={origin:'https://campus.example',portal_url:'https://campus.example/usage',rule:{fields},latest:scenario==='empty'?null:{observed_ms:1794017725000,details:{web_values:scenario==='partial'?{balance:10}:{balance:10,used_bytes:20*GiB,free_quota_bytes:100*GiB,used_free_bytes:20*GiB,free_remaining_bytes:80*GiB}}}};
   campusLastEventType=scenario==='auth'?'campus_auth_required':scenario==='error'?'campus_error':'';
   PulseCampus.renderLive();await tick();
   if(document.querySelector('.campus-overview')!==host||content.firstChild!==first)throw Error('Campus refresh replaced the page or outer card');
   const expected=scenario==='full'?'ready':scenario;
   if(host.dataset.status!==expected)throw Error('Campus state mismatch: '+scenario);
   const metrics=host.querySelectorAll('[data-campus-value]');
   if(scenario==='disabled'&&metrics.length||scenario!=='disabled'&&metrics.length!==3)throw Error('Campus metric layout mismatch');
   if(scenario==='full'){
    const model=campusOverviewModel();if(model.values.total!==100*GiB||model.values.funded!==20*GiB||host.querySelectorAll('.campus-segment').length!==3)throw Error('Campus components or total mismatch');
    if(!host.textContent.includes(tr('campus.card.funded')))throw Error('Campus estimate label missing');
    const track=host.querySelector('.campus-segment-track');
    for(const segment of track.children)if(segment.getBoundingClientRect().width<1)throw Error('Campus segment width blocked by CSP');
   }
   if(scenario==='partial'&&host.querySelector('.campus-tone-funded.campus-segment'))throw Error('Unknown paid data has a segment');
   if(['auth','error'].includes(scenario)&&host.querySelector('.campus-free-percent'))throw Error('Stale data displays a live usage percentage');
   for(const node of host.querySelectorAll('*'))if(node.clientWidth&&node.scrollWidth>node.clientWidth+2)throw Error('Campus overview overflow: '+node.className);
   if(['empty','partial','full'].includes(scenario)){
    host.scrollIntoView({block:'center',behavior:'instant'});await tick();
    await rpc('selftest_snapshot',{phase:'campus-'+scenario});
   }
  }
  window.pulseCampusOverviewReport={page,states:6,outerCardPreserved:true};
 }finally{
  campusData=saved.data;campusFieldState=saved.fields;campusEventStatus=saved.status;campusLastEventType=saved.event;state.config=saved.config;
  PulseCampus.renderLive();window.scrollTo({top:saved.scroll,behavior:'instant'});
 }
}
async function validateCampusPickerBridge(){
 if(!state.self_test||state.config?.campus_enabled)return;
 // Disabled monitoring lets the real UI -> WebView2 parser -> native dispatch
 // reject each request without opening any configured or external website.
 for(const [field]of campusFields){
  try{await rpc('campus_pick_field',{field});throw Error('Disabled portal unexpectedly opened');}
  catch(failure){if(failure.code!=='CAMPUS_PORTAL_FAILED')throw Error('Campus picker bridge failed for '+field+': '+(failure.code||'timeout/no correlated reply'));}
 }
 let rejected=false;try{await rpc('campus_pick_field',{field:'password'});}catch(failure){rejected=failure.code==='INVALID_MESSAGE';}
 if(!rejected)throw Error('Invalid picker field did not receive an immediate correlated rejection');
 window.pulseCampusPickerBridgeReport={fields:campusFields.length,invalidFieldRejected:true};
}
async function validateNavigationTransitions(){
 if(!state.self_test)return;
 const c=$('content'),nav=$('navigation'),toolbar=$('dashboard-toolbar'),initial=page,initialScroll=window.scrollY;
 const initialRange=JSON.stringify(range),nodes=new Map(),times=[];let reused=0,empty=0;
 const tick=()=>new Promise(resolve=>requestAnimationFrame(resolve));
 navigate('settings');const draft=c.querySelector('.campus-settings input[type=url]'),value=draft?.value;
 if(draft)draft.value='https://pending.example';
 navigate('dashboard');const probe=el('canvas',{width:32,height:8});c.append(probe);let paints=0;
 const observer=PulseRender.observe(probe,()=>{++paints;probe.getContext('2d').fillRect(0,0,32,8);});chartObservers.push(observer);
 const sequence=Array.from({length:4},()=>['dashboard','network','keyboard','mouse','usage','history','settings']).flat();
 for(const next of sequence){
  const start=performance.now();navigate(next);times.push(performance.now()-start);
  if(c.dataset.page!==next)throw Error('Navigation did not present the selected page immediately');
  if(nodes.has(next)){if(nodes.get(next)!==c.firstChild)throw Error('Cached page was reconstructed');++reused;}else nodes.set(next,c.firstChild);
  await tick();if(!c.childElementCount)++empty;
  if($('navigation')!==nav||$('dashboard-toolbar')!==toolbar)throw Error('Common navigation layout was reconstructed');
 }
 if(draft&&c.querySelector('.campus-settings input[type=url]')?.value!=='https://pending.example')throw Error('Settings draft lost on navigation');
 if(draft)draft.value=value;
 if(paints<2)throw Error('Cached chart did not redraw when reattached');
 observer.disconnect();probe.remove();chartObservers=chartObservers.filter(item=>item!==observer);
 for(const view of pageViews.values()){view.nodes=view.nodes.filter(node=>node!==probe);view.observers=view.observers.filter(item=>item!==observer);}
 const motion=document.documentElement.dataset.motion;document.documentElement.dataset.motion='reduced';navigate('keyboard');
 if(c.getAnimations().length)throw Error('Reduced-motion navigation animated');
 document.documentElement.dataset.motion=motion;navigate(initial);
 if(JSON.stringify(range)!==initialRange)throw Error('Page range lost on navigation');
 window.scrollTo({top:initialScroll,behavior:'instant'});await tick();
 if(empty)throw Error('Navigation produced an empty content frame');
 const sorted=times.slice().sort((a,b)=>a-b),metrics={navigation_count:times.length,cached_count:reused,empty_frames:empty,dom_mean_ms:times.reduce((a,b)=>a+b,0)/times.length,dom_p95_ms:sorted[Math.floor((sorted.length-1)*.95)]};
 window.pulseNavigationReport=metrics;await rpc('selftest_snapshot',{phase:'navigation',metrics});
}
async function validateStaticInteractions(){
 const check=(value,message)=>{if(!value)throw new Error('Static UI: '+message);};
 if(page==='settings')check(document.querySelectorAll('[data-settings-group]').length===8,'eight settings groups');
 check(!document.getElementById('eyebrow')&&!document.querySelector('aside .privacy'),'removed labels');
 const action=el('button',{disabled:false,onclick:()=>{action.dataset.clicked='true';}});action.click();
 check(!action.disabled&&action.dataset.clicked==='true','enabled button interaction');
 for(const node of document.querySelectorAll('nav button,h2,h3,.state,.kpi-head,.chart-legend,.card>span'))
  check(getComputedStyle(node).userSelect==='none','selectable static label');
 const probe=document.createElement('label');
 probe.innerHTML='<input value="12:34"><select><option>A</option><option>B</option></select><code>copy/path</code><span class="selectable-text">copy</span>';
 document.getElementById('content').append(probe);
 try{
  const input=probe.querySelector('input');input.focus();input.setSelectionRange(0,2);
  check(document.activeElement===input&&input.selectionStart===0&&input.selectionEnd===2,'input focus/selection');
  input.setRangeText('18',0,2,'end');check(input.value==='18:34','input editing');
  for(const n of probe.querySelectorAll('input,select,code,.selectable-text'))check(getComputedStyle(n).userSelect==='text','copy/edit escape hatch');
  const select=probe.querySelector('select');select.focus();select.selectedIndex=1;select.dispatchEvent(new Event('change',{bubbles:true}));
  check(document.activeElement===select&&select.value==='B'&&!select.disabled,'select operation');
 }finally{probe.remove();}
 if(page!=='settings'&&rangeFields){
  check(rangePreset==='all'&&range.from===0&&rangeFields.preset.value==='all','default all-data range');
  rangeFields.from.click();const picker=PulseDatePicker.current;
  check(picker?.dialog.open,'date button click');picker.hour.focus();picker.hour.select();
  check(document.activeElement===picker.hour&&getComputedStyle(picker.hour).userSelect==='text','date editor');
  picker.hour.setRangeText('18',0,picker.hour.value.length,'end');check(picker.hour.value==='18','date edit');picker.close();
 }
 // Range selection uses actual browser Selection, independent of styling.
 const copy=document.createElement('pre');copy.textContent='export/path';document.getElementById('content').append(copy);
 const selectionRange=document.createRange();selectionRange.selectNodeContents(copy);const selection=getSelection();selection.removeAllRanges();selection.addRange(selectionRange);
 check(selection.toString()==='export/path'&&getComputedStyle(copy).userSelect==='text','export copying');selection.removeAllRanges();copy.remove();
 window.pulseStaticInteractionReport={passed:true};
}
// Runs only in the explicit native --self-test process, never during ordinary monitoring.
async function validateScrollMaterial(){
 if(!state.self_test)return;
 const root=document.documentElement,toolbar=document.getElementById('dashboard-toolbar'),previous=root.dataset.transparency;
 const tick=()=>new Promise(resolve=>requestAnimationFrame(resolve));
 window.scrollTo({top:0,behavior:'instant'});await tick();await tick();
 await rpc('selftest_snapshot',{phase:'top'});
 const distance=Math.min(320,Math.max(0,root.scrollHeight-innerHeight)),frames=distance&&root.dataset.motion!=='reduced'?36:0,intervals=[];
 let last=performance.now();
 for(let i=1;i<=frames;i++){await tick();const now=performance.now();intervals.push(now-last);last=now;window.scrollTo({top:distance*i/frames,behavior:'instant'});}
 if(!frames&&distance)window.scrollTo({top:distance,behavior:'instant'});
 await tick();await tick();
 let top=0,blur=false;
 if(toolbar){const style=getComputedStyle(toolbar);top=toolbar.getBoundingClientRect().top;blur=style.backdropFilter!=='none'&&style.backdropFilter!=='';
  if(distance>toolbar.offsetTop+2&&Math.abs(top)>2)throw new Error('Toolbar did not remain sticky');
  if(style.filter!=='none'||style.opacity!=='1')throw new Error('Toolbar foreground was blurred');
  const hit=document.elementFromPoint(toolbar.getBoundingClientRect().left+20,Math.max(1,top)+20);
  if(!toolbar.contains(hit))throw new Error('Chart painted over toolbar');
 }
 const sorted=intervals.slice().sort((a,b)=>a-b),mean=intervals.length?intervals.reduce((a,b)=>a+b,0)/intervals.length:0;
 const metrics={frames,mean_ms:mean,p95_ms:sorted.length?sorted[Math.floor((sorted.length-1)*.95)]:0,fps:mean?1000/mean:0,scroll_px:distance,toolbar_top:top,blur_enabled:Number(blur),opacity_fallback:0};
 await rpc('selftest_snapshot',{phase:'scrolled',metrics});
 if(toolbar){root.dataset.transparency='off';await tick();await tick();const style=getComputedStyle(toolbar);
  if(style.backdropFilter!=='none')throw new Error('Opaque material fallback failed');
  metrics.opacity_fallback=1;await rpc('selftest_snapshot',{phase:'opaque',metrics});
 }
 if(previous==null)delete root.dataset.transparency;else root.dataset.transparency=previous;
 window.scrollTo({top:0,behavior:'instant'});await tick();await tick();
 window.pulseTraceScrollReport=metrics;
}
