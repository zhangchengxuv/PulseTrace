'use strict';
async function validateZenPulse(checkMinimize){
 const stage=$('zen-stage'),canvas=$('zen-canvas'),screen=$('zen-fullscreen'),theme=document.documentElement.dataset.theme;
 const check=(condition,message)=>{if(!condition)throw new Error('Zen Pulse: '+message);};
 async function until(predicate,message){
  const deadline=performance.now()+2500;
  while(!predicate()&&performance.now()<deadline)
   await new Promise(resolve=>requestAnimationFrame(resolve));
  check(predicate(),message);
 }
 function escape(){document.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true,cancelable:true}));}
 try{
  applyTheme('light');
  $('zen-enter').click();
  check(PulseZen.active&&!stage.hidden&&getComputedStyle(document.querySelector('body>main')).display==='none','entry failed');
  const style=getComputedStyle(stage);
  check(style.colorScheme==='dark'&&style.getPropertyValue('--series-primary').trim()==='#38bdf8'&&
   style.getPropertyValue('--series-comparison').trim()==='#a78bfa','dark series change with main theme');
  for(const [action,scale] of [['125',1.25],['150',1.5]]){
   await rpc('selftest_zen',{action});
   await until(()=>Math.abs(devicePixelRatio-scale)<.02&&canvas.width===Math.round(canvas.clientWidth*devicePixelRatio)&&
    canvas.height===Math.round(canvas.clientHeight*devicePixelRatio),'backing pixels at '+action+'%');
  }
  await rpc('selftest_zen',{action:'reset'});
  screen.click();
  await until(()=>screen.getAttribute('aria-pressed')==='true','native fullscreen did not open');
  screen.click();
  await until(()=>screen.getAttribute('aria-pressed')==='false','native fullscreen did not restore');
  canvas.dispatchEvent(new MouseEvent('dblclick',{bubbles:true}));
  check(!PulseZen.active&&!PulseZen.running&&stage.hidden&&canvas.width===1,'double-click did not stop mode');
  $('zen-enter').click();
  if(checkMinimize){
   await until(()=>canvas.width>1,'no visible raster before minimization');
   await rpc('selftest_zen',{action:'minimize'});
   await until(()=>PulseZen.active&&nativeVisible&&canvas.width>1,'mode did not resume after native restore');
  }
  screen.click();
  await until(()=>screen.getAttribute('aria-pressed')==='true','fullscreen did not reopen');
  escape();
  check(!PulseZen.active&&!PulseZen.running&&canvas.width===1&&document.activeElement===$('zen-enter'),'Esc did not exit and restore focus');
 }finally{
  PulseZen.exit();
  await rpc('selftest_zen',{action:'reset'});
  applyTheme(theme);
 }
}
async function validateActivityHistory(host){
 const check=(condition,message)=>{if(!condition)throw new Error('Activity history: '+message);};
 await activityHistory.update();
 const rhythm=host.querySelector('.activity-grid.activity-rhythm');
 check(rhythm?.querySelectorAll('button').length===168,'weekly matrix dimensions');
 const tokens=getComputedStyle(document.documentElement);
 check(getComputedStyle(rhythm).getPropertyValue('--activity-series').trim()===tokens.getPropertyValue('--series-primary').trim(),'primary rhythm token');
 const first=rhythm.querySelector('button');
 first.focus({preventScroll:true});
 first.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowRight',bubbles:true}));
 check(document.activeElement.dataset.column==='1'&&host.querySelector('.activity-detail').textContent===document.activeElement.title,'arrow keys and cell details');
 document.activeElement.blur();
 const [year,metric]=host.querySelectorAll('.activity-options select');
 const oldYear=year.value,oldMetric=metric.value;
 const checkWall=()=>{
  const wall=host.querySelector('.activity-grid.activity-wall');
  const days=365+Number(PulseDate.days(Number(year.value),2)===29);
  check(wall?.querySelectorAll('button').length===days,'annual day count');
  check(getComputedStyle(wall).getPropertyValue('--activity-series').trim()===tokens.getPropertyValue('--series-comparison').trim(),'comparison wall token');
 };
 checkWall();
 year.selectedIndex=1;
 year.dispatchEvent(new Event('change'));
 metric.value='network';
 metric.dispatchEvent(new Event('change'));
 await activityHistory.update();
 checkWall();
 year.value=oldYear;metric.value=oldMetric;
 year.dispatchEvent(new Event('change'));
 await activityHistory.update();
}
function validateInsights(name,host){
 const check=(condition,message)=>{if(!condition)throw new Error('Insights: '+message);};
 if(name==='keyboard'){
  const hands=[...host.querySelectorAll('.insight-hand')];
  check(hands.length===2&&hands.every(hand=>hand.querySelectorAll('svg [data-finger]').length===5),'ten SVG fingers');
  const tokens=getComputedStyle(document.documentElement);
  check(hands.every(hand=>getComputedStyle(hand).getPropertyValue('--insight-series').trim()===
   tokens.getPropertyValue(hand.dataset.side==='left'?'--series-primary':'--series-comparison').trim()),'series tokens');
  const unit=PulseI18n.locale==='zh-CN'?'年':'yr';
  check(insightYears(99)==='99 '+unit&&insightYears(99.1)==='> 99 '+unit&&
   insightYears(null)==='—','hardware estimate display limit');
 }
 if(name==='mouse')check(host.querySelectorAll('.insight-stat').length===2,'individual mouse switch estimates');
 if(name==='settings'){
  const inputs=[...host.querySelectorAll('[data-insight-rating] input')];
  check(inputs.length===2&&inputs.every(input=>PulseInsights.validRating(Number(input.value))),'editable hardware ratings');
  const actions=[...host.querySelectorAll('[data-settings-group=general] .settings-form-actions')];
  check(actions.length===3&&actions.every(action=>action.parentElement.classList.contains('settings-grid')&&
   action.querySelector('button.secondary')),'general settings use the same save-button style and grid spacing');
 }
 if(name==='dashboard'){
  const ring=dashboardIpRing(1);
  host.querySelector('.kpi-protocol .kpi-body').append(ring);
  const inner=ring.firstElementChild,circle=inner.getBoundingClientRect();
  const x=circle.left+circle.width/2,y=circle.top+circle.height/2;
  for(const text of inner.children){
   const box=text.getBoundingClientRect();
   const distance=Math.hypot(Math.max(x-box.left,box.right-x),Math.max(y-box.top,box.bottom-y));
   check(distance<circle.width/2,'protocol label does not touch the ring');
  }
  ring.remove();
  const details=host.querySelector('.insight-achievements'),initial=details?.open;
  check(details&&(!initial?details.querySelectorAll('.insight-achievement').length===0:true),'lazy folded achievements');
  details.open=true;
  details.dispatchEvent(new Event('toggle'));
  check(details.querySelectorAll('.insight-achievement-grid article').length===5,'scoped achievement cards');
  details.open=initial;
  details.dispatchEvent(new Event('toggle'));
 }
}
async function validateSummaryDownload(){
 const sample=[
  {module:'keyboard',metric:'30',value:19472},
  {module:'mouse',metric:'left',value:743},
  {module:'network',metric:'interface_download_bytes',value:2147483648},
  {module:'ignored',metric:'test-private-value',value:99}
 ];
 const totals=summaryTotals(sample),modules=new Set(['keyboard','mouse','active','network']);
 const music={song:'Test-private-song',artist:'Test-private-artist'};
 const metrics=summaryMetrics(totals,3280000,music,modules),canvas=el('canvas');
 if(metrics.length!==5||metrics.some(metric=>metric.value.includes('Test-private')))throw Error('Summary privacy projection invalid');
 const period=summaryPeriod({from:0,to:200000,bucket:60});
 drawSummaryCard(canvas,period,metrics,true);
 const light=canvas.getContext('2d').getImageData(0,30,1,1).data[0];
 drawSummaryCard(canvas,period,metrics,false);
 if(canvas.getContext('2d').getImageData(0,30,1,1).data[0]>=light)throw Error('Summary palette did not switch');
 if(!(await saveSummaryPNG(canvas)).saved)throw Error('Summary PNG export returned no file');
}
function validateNetworkSummary(host){
 if(host.querySelectorAll('.network-transfer').length!==3)throw Error('Network summary must contain three transfer cards');
 const trend=host.querySelector('.network-trends');
 if(innerWidth>=1500&&innerHeight>=830&&trend.getBoundingClientRect().top+scrollY+130>innerHeight)
  throw Error('Network trends are outside the first viewport');
 const saved=state;
 const capture={running:true,interface_id:'test-link',rates:[10,20,30,0]};
 const network={selected:'test-link',interface_rates_ready:true,upload_bps:80,download_bps:90};
 const reading=(protocol,options={})=>{
  state={...saved,monitoring:true,suspended:false,quiescing:false,network_error:'',...options,
   network:{...network,protocol_capture:protocol}};
  const fixture=el('div');
  networkPage(fixture);
  return [...fixture.querySelectorAll('.network-directions small')].map(node=>node.textContent).join('|');
 };
 const rates=(...values)=>values.map(value=>typeof value==='number'?bytes(value)+'/s':tr(value)).join('|');
 try{
  if(reading(capture)!==rates(80,90,10,20,30,0))throw Error('IPv4 and IPv6 live rates or observed zero were lost');
  if(reading({...capture,rates:[10,null,null,null],rates_incomplete:true})!==
     rates(80,90,10,...Array(3).fill('network.ip_live_incomplete')))throw Error('Incomplete IP samples appeared as zero');
  if(reading({...capture,enable_status:5,error:'EnableTraceEx2: 5'})!==
     rates(80,90,...Array(4).fill('network.ip_live_permission')))throw Error('An IP capture fault hid the interface rates');
  if(reading(capture,{monitoring:false})!==rates(...Array(6).fill('ui.eb0c326b60ae')))
   throw Error('Paused network page retained old live rates');
 }finally{state=saved;}
}
function validateKeyboardLayouts(host,theme){
 const layout=host.querySelector('.heat-toolbar select'),previous=layout.value;
 const scale=host.querySelectorAll('.heat-toolbar select')[1],previousScale=scale.value;
 const choose=(select,value)=>{select.value=value;select.dispatchEvent(new Event('change'));};
 try{
  choose(layout,'ansi');
  const caps=host.querySelectorAll('.cap');
  if(caps.length!==104||new Set([...caps].map(key=>key.dataset.key)).size!==104)throw Error('ANSI 104 DOM mapping invalid');
  host.querySelector('[data-key="28"]').click();
  if(host.querySelector('.key-detail').dataset.key!=='28'||!host.querySelector('.key-detail strong').textContent.includes('Enter'))throw Error('Keyboard detail interaction failed');
  choose(layout,'tkl');choose(scale,'log');
  if(host.querySelectorAll('.cap').length!==87||host.querySelector('.cap[data-group="numpad"]'))throw Error('TKL DOM mapping invalid');
  if(PulseDate.preference(keyboardScalePreference,'linear',['linear','log'])!=='log')throw Error('Keyboard scale was not persisted');
  const rgb=getComputedStyle(host.querySelector('.zero-swatch')).backgroundColor.match(/\d+/g).slice(0,3).map(Number);
  if(rgb.join()!==PulseViz.color(heatmapName,0,0,'log',theme==='light').join())throw Error('Zero legend differs from keyboard colors');
 }finally{choose(layout,previous);choose(scale,previousScale);}
}
async function validateSettingsCategories(){
 if(!state.self_test||page!=='settings')return;
 const initial=settingsActive,before=await rpc('get_status'),buttons=[...document.querySelectorAll('[data-settings-tab]')];
 if(document.querySelector('[data-settings-tab=taskbar],.widget-settings'))throw Error('Retired taskbar UI remains');
 if(buttons.length!==7)throw Error('Settings categories missing');
 try{for(const button of buttons){
  button.click();await new Promise(resolve=>requestAnimationFrame(resolve));
  const key=button.dataset.settingsTab,visible=[...document.querySelectorAll('[data-settings-group]')].filter(node=>!node.hidden);
  if(visible.length!==1||visible[0].dataset.settingsGroup!==key)throw Error('Settings category visibility mismatch');
  if(visible[0].scrollWidth>visible[0].clientWidth+2)throw Error('Settings category overflow: '+key);
  if(key==='campus'){
   const campus=visible[0].querySelector('.campus-settings'),advanced=campus.querySelector('details.campus-section');
   if(campus.querySelectorAll(':scope>section.campus-section').length!==3||advanced.open)throw Error('Campus settings groups or initial disclosure mismatch');
   if(!campusData?.rule&&campus.querySelectorAll('.campus-candidate-row').length!==2)throw Error('Absent webpage fields generated redundant mapping controls');
   if(campus.querySelector('.campus-computed-target input,.campus-computed-target select'))throw Error('Computed results must be read only');
   const rows=[...campus.querySelectorAll('.campus-web-row')],computed=campus.querySelector('.campus-computed-target');
   PulseCampus.renderLive();const before=computed.firstChild;PulseCampus.renderLive();
   if(rows.length!==2||computed.firstChild!==before||rows.some(row=>!row.isConnected))throw Error('Campus live update rebuilt unchanged controls');
   for(const node of campus.querySelectorAll('*'))if(node.clientWidth&&node.scrollWidth>node.clientWidth+2)throw Error('Campus settings control overflow: '+node.className);
  }
  if(key==='cloud'){
   if(document.querySelector('.cloud-settings input[type=url]').value!=='https://dav.jianguoyun.com/dav/')throw Error('Default WebDAV server missing');
   if(document.querySelectorAll('.cloud-settings input[type=password]').length!==2)throw Error('Cloud secrets must use password controls');
   const status=await rpc('cloud_status');if(!status.secure_forms)throw Error('WebView password saving/autofill must be disabled');if(status.config.enabled||status.credentials_saved)throw Error('Cloud must remain off and unconfigured in isolated tests');
  }
  if(['general','campus','cloud'].includes(key))await rpc('selftest_snapshot',{phase:'settings-'+key});
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
  node.open=true;if(!keyboardRanking({},null).open)throw Error('Pending keyboard expansion lost during redraw');
  node.open=false;if(keyboardRanking({},null).open)throw Error('Pending keyboard collapse lost during redraw');
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
  for(const scenario of ['disabled','empty','partial','full','auth','error','billing','unconfirmed','stale']){
   state.config={...saved.config,campus_enabled:scenario!=='disabled',campus_unit_price:scenario==='partial'?0:.5,campus_price_unit:'GB',campus_price_basis:'school',campus_linear_tariff:true,campus_traffic_radix:1024};
   campusData={origin:'https://campus.example',portal_url:'https://campus.example/usage',rule:{fields},latest:scenario==='empty'?null:{observed_ms:Date.now(),cycle_text:new Date().getFullYear()+'-'+String(new Date().getMonth()+1).padStart(2,'0'),measurement:{raw_readings:Object.fromEntries(Object.entries({used_bytes:20,free_quota_bytes:100,used_free_bytes:20,free_remaining_bytes:80}).map(([k,value])=>[k,{value,unit:'GiB'}]))},details:{web_values:scenario==='partial'?{balance:10}:{balance:10,used_bytes:20*GiB,free_quota_bytes:100*GiB,used_free_bytes:20*GiB,free_remaining_bytes:80*GiB}}}};
   if(scenario==='full')campusData.latest.details.web_units={used_bytes:'MB',free_remaining_bytes:'GiB'};
   if(scenario==='billing')campusData.latest.cycle_text='';
   if(scenario==='unconfirmed')campusData.latest.measurement={raw_readings:{},status:'needs_confirmation'};
   campusLastEventType=scenario==='auth'?'campus_auth_required':scenario==='error'?'campus_error':scenario==='stale'?'campus_rule_stale':'';
   PulseCampus.renderLive();await tick();
   if(document.querySelector('.campus-overview')!==host||content.firstChild!==first)throw Error('Campus refresh replaced the page or outer card');
   const expected=scenario==='full'?'ready':scenario;
   if(host.dataset.status!==expected)throw Error('Campus state mismatch: '+scenario);
   const metrics=host.querySelectorAll('[data-campus-value]');
   if(scenario==='disabled'&&metrics.length||scenario!=='disabled'&&metrics.length!==3)throw Error('Campus metric layout mismatch');
   if(scenario==='full'){
    if(host.querySelector('[data-campus-part="used"] strong').textContent!=='20.00 GB'||!host.querySelector('.campus-conversion'))throw Error('Portal units or conversion label missing');
    let details=host.querySelector('.campus-overview-details');
    if(!details||details.open||details.querySelector('.campus-conversion').checkVisibility())throw Error('Campus details must start collapsed');
    if(!details.textContent.includes(tr('campus.card.total_basis'))||host.querySelector('.campus-usage').textContent.includes(tr('campus.card.total_basis')))throw Error('Campus calculation basis must remain in details');
    details.open=true;campusData.latest.observed_ms++;PulseCampus.renderLive();await tick();
    details=host.querySelector('.campus-overview-details');
    if(!details.open||!details.querySelector('.campus-conversion').checkVisibility())throw Error('Campus detail expansion lost on refresh');
    details.open=false;
    if(host.querySelector('.campus-segment-track').title!==tr('campus.free_track'))throw Error('Campus progress tooltip missing');
    const model=campusOverviewModel();if(model.values.total!==100*GiB||model.values.funded!==20*GiB||host.querySelectorAll('.campus-segment').length!==2)throw Error('Campus components or total mismatch');
    if(!host.textContent.includes(tr('campus.card.funded')))throw Error('Campus estimate label missing');
    const track=host.querySelector('.campus-segment-track');
    for(const segment of track.children)if(segment.getBoundingClientRect().width<1)throw Error('Campus segment width blocked by CSP');
   }
   if(scenario==='partial'&&host.querySelector('.campus-tone-funded.campus-segment'))throw Error('Unknown paid data has a segment');
   if(['auth','error','billing','unconfirmed','stale'].includes(scenario)&&host.querySelector('.campus-free-percent'))throw Error('Unconfirmed or stale data displays a live usage percentage');
   if(scenario==='billing'&&(!host.textContent.includes(tr('campus.accounting_unknown'))||host.textContent.includes(tr('campus.rule_stale'))))throw Error('Billing configuration was mistaken for a page structure failure');
   for(const node of host.querySelectorAll('*'))if(node.clientWidth&&node.scrollWidth>node.clientWidth+2)throw Error('Campus overview overflow: '+node.className);
   if(['empty','partial','full'].includes(scenario)){
    host.scrollIntoView({block:'center',behavior:'instant'});await tick();
    await rpc('selftest_snapshot',{phase:'campus-'+scenario});
   }
  }
  window.pulseCampusOverviewReport={page,states:9,outerCardPreserved:true};
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
 if(!state.config?.campus_portal_url){
  // Exercise the actual WebView2 parser with the metadata emitted by Settings.
  // An unconfigured isolated profile must reach the core's URL check, without
  // writing a rule, opening a portal or changing the configuration.
  const fields={
   used_bytes:{mode:'selector',selector:'#v4',kind:'bytes',unit:'MB',confidence:.95,scope:'ipv4'},
   free_quota_bytes:{mode:'manual',kind:'bytes',unit:'GB',value:120,traffic_radix:1024},
   billing_cycle:{mode:'manual',kind:'text',unit:'text',period:'monthly',start_date:'2026-11-01',value:'2026-11-01/monthly'},
   used_free_bytes:{mode:'formula',kind:'bytes',formula:'min(used_bytes, free_quota_bytes)',accounting_confirmed:true,cycle:'2026-11-01/monthly',confirmed_month:'2026-11'}
  };
  let reachedCore=false;
  try{await rpc('save_campus_mapping',{fields});}catch(failure){reachedCore=failure.message==='Configure and save a campus portal URL first';}
  if(!reachedCore)throw Error('Campus billing metadata did not pass the real native bridge');
 }
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
 const sequence=Array.from({length:4},()=>['dashboard','network','keyboard','mouse','usage','music','history','settings']).flat();
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
 if(page==='settings')check(document.querySelectorAll('[data-settings-group]').length===7,'seven settings groups');
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
  check(rangePreset==='all'&&range.from===0&&document.querySelector('[data-range-preset=all]').getAttribute('aria-pressed')==='true','default all-data range');
  check(document.querySelectorAll('#range-host .range select').length===2&&rangeFields.shortcuts.length===7,'shortcut presets and two selectors');
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
