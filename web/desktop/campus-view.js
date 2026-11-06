'use strict';
let campusData=null,campusCandidates={},campusSelection={},campusEventStatus='',campusPickerField='';
const campusFields=['balance','used_bytes','remaining_bytes','quota_bytes','fee','billing_cycle'];
const campusKind={balance:'currency',used_bytes:'bytes',remaining_bytes:'bytes',quota_bytes:'bytes',fee:'currency',billing_cycle:'text'};
function campusBytes(value){return value==null?'—':bytes(Number(value));}
function campusMoney(value){return value==null?'—':'¥'+Number(value).toFixed(2);}
function campusLatestText(){return campusData?.latest?.observed_ms?new Date(campusData.latest.observed_ms).toLocaleString(PulseI18n.locale):tr('campus.never_synced');}
function campusQuality(){const s=campusEventStatus||campusData?.latest?.quality||'';return s==='confirmed'?tr('campus.confirmed'):s==='auto_recognized'?tr('campus.auto_short'):s==='low_confidence'?tr('campus.needs_confirmation'):s==='stale'?tr('campus.rule_stale'):s==='unknown'?tr('campus.unknown'):tr('campus.never_synced');}
function campusLivePanel(compact=false){
 const latest=campusData?.latest,cfg=state.config||{},used=latest?.used_bytes,price=Number(cfg.campus_unit_price||0),estimate=used==null||!price?null:used/1024/1024/1024*price,actual=latest?.fee;
 const quota=latest?.quota_bytes,progress=quota&&used!=null?Math.max(0,Math.min(1,used/quota)):null;
 const host=el('div',{class:'campus-live-host'});
 host.append(el('div',{class:'campus-live'},el('div',{},el('small',{},tr('campus.balance')),el('strong',{},campusMoney(latest?.balance))),el('div',{},el('small',{},tr('campus.used')),el('strong',{},campusBytes(used))),el('div',{},el('small',{},tr('campus.remaining')),el('strong',{},campusBytes(latest?.remaining_bytes))),el('div',{},el('small',{},tr('campus.quota')),el('strong',{},campusBytes(quota)))));
 if(!compact){
  host.append(el('div',{class:'campus-quality'},el('span',{class:'quality-badge'},campusQuality()),el('span',{},tr('campus.last_sync')+' '+campusLatestText()),el('span',{},tr('campus.portal_fee')+' '+(actual==null?'—':campusMoney(actual))),el('span',{},tr('campus.estimated_fee')+' '+(estimate==null?'—':campusMoney(estimate)))));
  if(progress!=null)host.append(el('div',{class:'campus-progress','aria-label':tr('campus.progress')},el('i',{style:`width:${progress*100}%`}),el('span',{},(progress*100).toFixed(1)+'%')));
  if(campusData?.rule_error)host.append(el('p',{class:'capture-error'},campusData.rule_error));
 }
 return host;
}
function campusDashboardCard(){
 if(!(state.config?.campus_enabled))return null;
 const latest=campusData?.latest,card=el('article',{class:'kpi kpi-campus'},el('div',{class:'kpi-head'},metricIcon('network'),el('span',{},tr('campus.title'))),el('div',{class:'campus-card-values'},el('strong',{},campusMoney(latest?.balance)),el('span',{},tr('campus.remaining')+' '+campusBytes(latest?.remaining_bytes))),el('div',{class:'kpi-foot'},campusQuality()+' · '+campusLatestText()));
 return card;
}
function campusNetworkPanel(){
 if(!(state.config?.campus_enabled))return null;
 return panel(tr('campus.title'),el('div',{class:'campus-live-target'},campusLivePanel(false)),el('p',{class:'muted'},tr('campus.basis')));
}
function campusCandidateOptions(field,current){
 const items=campusCandidates[field]||[],select=el('select',{'aria-label':tr('campus.field.'+field)});
 select.append(el('option',{value:''},tr('campus.not_mapped')));
 items.forEach((item,index)=>select.append(el('option',{value:String(index)},`${item.raw||item.value||''} · ${Math.round(Number(item.confidence||0)*100)}%`)));
 const selector=typeof current==='string'?current:current?.selector;
 if(selector){let match=items.findIndex(item=>item.selector===selector);if(match>=0)select.value=String(match);}
 return select;
}
function renderCampusSettings(host){
 const cfg=state.config||{},enabled=el('input',{type:'checkbox',checked:Boolean(cfg.campus_enabled)}),url=el('input',{type:'url',value:cfg.campus_portal_url||'',placeholder:'http://202.204.48.66',maxlength:2048}),origins=el('textarea',{rows:3,placeholder:'https://cas.example.edu.cn'}),refresh=el('input',{type:'number',min:5,max:1440,value:cfg.campus_refresh_minutes||5}),price=el('input',{type:'number',min:0,max:1000,step:'0.01',value:cfg.campus_unit_price||0});origins.value=(cfg.campus_allowed_origins||[]).join('\n');
 const feedback=el('p',{class:'muted',role:'status'},campusEventStatus),candidateHost=el('div',{class:'campus-candidates'}),actions=el('div',{class:'actions'});
 const save=async()=>{try{const allowed=origins.value.split(/\r?\n/).map(v=>v.trim()).filter(Boolean);await rpc('set_config',{campus_enabled:enabled.checked,campus_portal_url:url.value.trim(),campus_allowed_origins:allowed,campus_refresh_minutes:Number(refresh.value),campus_unit_price:Number(price.value)});state=await rpc('get_status');campusData=await rpc('get_campus_status');feedback.textContent=tr('campus.saved');drawCandidates();PulseCampus.renderLive();}catch(e){error(e);}};
 const drawCandidates=()=>{candidateHost.replaceChildren();const ruleFields=campusData?.rule?.fields||{};for(const field of campusFields){const select=campusCandidateOptions(field,ruleFields[field]||campusSelection[field]),row=el('div',{class:'campus-candidate-row'},el('strong',{},tr('campus.field.'+field)),select,el('button',{class:'secondary',onclick:async()=>{try{campusPickerField=field;await rpc('campus_pick_field',{field});feedback.textContent=tr('campus.pick_hint',{field:tr('campus.field.'+field)});}catch(e){error(e);}}},tr('campus.pick')));candidateHost.append(row);}const confirm=el('button',{onclick:async()=>{const fields={};for(const row of candidateHost.querySelectorAll('.campus-candidate-row')){const field=campusFields[[...candidateHost.querySelectorAll('.campus-candidate-row')].indexOf(row)],index=row.querySelector('select').value;if(index==='')continue;const item=(campusCandidates[field]||[])[Number(index)];if(!item)continue;fields[field]={selector:item.selector,kind:item.kind||campusKind[field],unit:item.unit||'',confidence:Number(item.confidence||0)};}try{if(!Object.keys(fields).length)throw new Error(tr('campus.no_selection'));await rpc('save_campus_mapping',{fields});campusData=await rpc('get_campus_status');feedback.textContent=tr('campus.mapping_saved');drawCandidates();PulseCampus.renderLive();}catch(e){error(e);}}},tr('campus.confirm_mapping'));candidateHost.append(el('div',{class:'actions'},confirm,el('button',{class:'secondary',onclick:async()=>{try{await rpc('campus_rescan');feedback.textContent=tr('campus.rescanning');}catch(e){error(e);}}},tr('campus.rescan')),el('button',{class:'danger',onclick:async()=>{try{await rpc('delete_campus_rule');campusCandidates={};campusSelection={};campusData=await rpc('get_campus_status');drawCandidates();}catch(e){error(e);}}},tr('campus.delete_rule'))));};
 drawCandidates();
 candidateHost.redraw=drawCandidates;
 host.append(el('div',{class:'settings-grid campus-settings'},el('label',{class:'checkbox'},enabled,tr('campus.enable')),el('label',{},el('strong',{},tr('campus.url')),url),el('label',{},el('strong',{},tr('campus.allowed')),origins,el('small',{},tr('campus.allowed_hint'))),el('label',{},el('strong',{},tr('campus.refresh')),refresh),el('label',{},el('strong',{},tr('campus.price')),price,el('small',{},tr('campus.price_hint')))),el('p',{class:'muted'},tr('campus.privacy')),actions,...[el('button',{onclick:async()=>{await save();try{await rpc('open_campus_portal');feedback.textContent=tr('campus.login_hint');}catch(e){error(e);}}},tr('campus.open')),el('button',{class:'secondary',onclick:save},tr('campus.save'))],feedback,el('div',{class:'campus-live-target'},campusLivePanel(true)),candidateHost);
}
const PulseCampus={
 async refresh(){try{campusData=await rpc('get_campus_status');}catch(e){campusData={...(campusData||{}),error:String(e.message||e)};}return campusData;},
 renderSettings:renderCampusSettings,
 dashboardCard:campusDashboardCard,
 networkPanel:campusNetworkPanel,
 livePanel:campusLivePanel,
 renderLive(){document.querySelectorAll('.campus-live-target').forEach(node=>node.replaceChildren(campusLivePanel(false)));},
 ingest(event){
  if(!event||!event.type)return;campusEventStatus='';
  if(event.type==='campus_candidates'){for(const [field,items]of Object.entries(event.candidates||{}))if(Array.isArray(items))campusCandidates[field]=items;if(!campusData?.rule)campusEventStatus='low_confidence';}
  else if(event.type==='campus_candidate'&&event.field&&event.candidate){const items=campusCandidates[event.field]||[];items.unshift(event.candidate);campusCandidates[event.field]=items.slice(0,5);campusSelection[event.field]=event.candidate.selector;campusEventStatus=tr('campus.manual_candidate');}
  else if(event.type==='campus_rule_stale'){campusEventStatus='stale';}
  else if(event.type==='snapshot_saved'&&event.snapshot){campusData=campusData||{};campusData.latest=event.snapshot;campusEventStatus='confirmed';}
  else if(event.type==='snapshot_failed'||event.type==='campus_error'){campusEventStatus=event.error||event.message||tr('campus.sync_failed');}
  if(page==='settings')document.querySelector('.campus-candidates')?.redraw?.();
  this.renderLive();
 }
};
