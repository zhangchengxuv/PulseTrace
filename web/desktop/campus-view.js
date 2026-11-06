'use strict';
let campusData=null,campusCandidates={},campusEventStatus='',campusPickerField='',campusFieldState={},campusRenderHost=null;
const GiB=1024**3,GB=1e9,MiB=1024**2;
const campusFields=[
 ['balance','currency'],['free_quota_bytes','bytes'],['free_remaining_bytes','bytes'],['used_free_bytes','bytes'],
 ['spendable_balance','currency'],['paid_used_bytes','bytes'],['used_bytes','bytes'],['remaining_bytes','bytes'],
 ['quota_bytes','bytes'],['fee','currency'],['billing_cycle','text']
];
const campusFieldKind=Object.fromEntries(campusFields);
const campusModes=['auto','selector','manual','formula','disabled'];
function campusBytes(value){return value==null?'—':bytes(Number(value));}
function campusMoney(value){return value==null?'—':'¥'+Number(value).toFixed(2);}
function campusLatestText(){return campusData?.latest?.observed_ms?new Date(campusData.latest.observed_ms).toLocaleString(PulseI18n.locale):tr('campus.never_synced');}
function campusQuality(){const s=campusEventStatus||campusData?.latest?.quality||'';return s==='confirmed'?tr('campus.confirmed'):s==='auto_recognized'?tr('campus.auto_short'):s==='low_confidence'?tr('campus.needs_confirmation'):s==='stale'?tr('campus.rule_stale'):s==='unknown'?tr('campus.unknown'):tr('campus.never_synced');}
function campusUnitFactor(unit){return ({B:1,KB:1e3,MB:1e6,GB:1e9,TB:1e12,KiB:1024,MiB:1024**2,GiB:1024**3,TiB:1024**4,CNY:1})[unit]||1;}
function campusCycle(){
 const mapping=campusFieldState.billing_cycle||campusData?.rule?.fields?.billing_cycle;
 if(mapping?.mode==='manual'&&mapping.kind==='text'&&mapping.value)return String(mapping.value);
 return String(campusData?.latest?.cycle_text||'');
}
function campusWebValues(){const d=campusData?.latest?.details?.web_values||{},l=campusData?.latest||{},out={...d};for(const [name]of campusFields)if(out[name]==null&&l[name]!=null)out[name]=l[name];return out;}
function campusMapping(field){return campusFieldState[field]||campusData?.rule?.fields?.[field]||{mode:'disabled'};}
function campusManualValue(m){
 if(m.value==null||!Number.isFinite(Number(m.value)))return null;
 const cycle=campusCycle();if(m.reset_on_cycle&&m.cycle&&cycle&&m.cycle!==cycle)return null;
 return Number(m.value)*campusUnitFactor(m.unit||'B');
}
function campusManualExpired(m){const cycle=campusCycle();return Boolean(m.reset_on_cycle&&m.cycle&&cycle&&m.cycle!==cycle);}
function campusFormula(expression,lookup){
 const tokens=String(expression||'').match(/\d+(?:\.\d+)?|[A-Za-z_][A-Za-z0-9_]*|[()+\-*/,]/g)||[];
 if(tokens.join('')!==String(expression||'').replace(/\s+/g,''))throw new Error(tr('campus.formula_invalid'));
 let i=0;const take=value=>tokens[i]===value&&(++i,true);
 const primary=()=>{
  if(take('(')){const value=sum();if(!take(')'))throw new Error(tr('campus.formula_invalid'));return value;}
  const token=tokens[i++];if(token===undefined)throw new Error(tr('campus.formula_invalid'));
  if(/^\d/.test(token))return Number(token);
  if(take('(')){
   const args=[];if(!take(')')){do{args.push(sum());}while(take(','));if(!take(')'))throw new Error(tr('campus.formula_invalid'));}
   if(token==='max'&&args.length)return Math.max(...args);if(token==='min'&&args.length)return Math.min(...args);
   if(token==='abs'&&args.length===1)return Math.abs(args[0]);throw new Error(tr('campus.formula_invalid'));
  }
  if(token==='GiB')return GiB;if(token==='GB')return GB;if(token==='MiB')return MiB;if(token==='MB')return 1e6;
  const value=lookup(token);if(value==null||!Number.isFinite(value))throw new Error(tr('campus.formula_missing',{field:token}));return value;
 };
 const product=()=>{let value=primary();while(i<tokens.length){if(take('*'))value*=primary();else if(take('/')){const divisor=primary();if(!divisor)throw new Error(tr('campus.formula_zero'));value/=divisor;}else break;}return value;};
 const sum=()=>{let value=product();while(i<tokens.length){if(take('+'))value+=product();else if(take('-'))value-=product();else break;}return value;};
 const value=sum();if(i!==tokens.length||!Number.isFinite(value))throw new Error(tr('campus.formula_invalid'));return value;
}
function campusResolve(){
 const web=campusWebValues(),resolved={},sources={},visiting=new Set(),errors=[];
 const resolve=field=>{
  if(field in resolved)return resolved[field];if(visiting.has(field))throw new Error(tr('campus.formula_cycle',{field}));
  visiting.add(field);const m=campusMapping(field),mode=m.mode||(m.selector?'auto':'disabled');let value=null,source='unknown';
  try{
   if((mode==='auto'||mode==='selector')&&web[field]!=null){value=campusFieldKind[field]==='text'?String(web[field]):Number(web[field]);source='portal';}
   else if(mode==='manual'){value=campusFieldKind[field]==='text'?String(m.value??''):campusManualValue(m);source=campusManualExpired(m)?'cycle_reset':'manual';}
   else if(mode==='formula'){value=campusFormula(m.formula,name=>resolve(name));source='formula';}
  }catch(e){errors.push({field,message:String(e.message||e)});value=null;source='error';}
  visiting.delete(field);
  resolved[field]=campusFieldKind[field]==='text'?(typeof value==='string'&&value?value:null):(Number.isFinite(value)?value:null);
  if(resolved[field]==null&&source==='unknown')source=mode;sources[field]=source;return resolved[field];
 };
 for(const [field]of campusFields)resolve(field);
 const freeQuota=resolved.free_quota_bytes??resolved.quota_bytes,freeUsed=resolved.used_free_bytes??resolved.used_bytes;
 const calculatedFree=freeQuota!=null&&freeUsed!=null?Math.max(0,freeQuota-freeUsed):null;
 const freeRemaining=resolved.free_remaining_bytes??calculatedFree??resolved.remaining_bytes;
 const freeRemainingSource=resolved.free_remaining_bytes!=null?sources.free_remaining_bytes:calculatedFree!=null?'calculated':resolved.remaining_bytes!=null?sources.remaining_bytes:sources.free_remaining_bytes;
 const price=Number(state.config?.campus_unit_price||0),funded=price>0&&resolved.spendable_balance!=null?resolved.spendable_balance/price*GiB:null;
 const total=freeRemaining!=null&&funded!=null?freeRemaining+funded:null;
 const quota=freeQuota,used=freeUsed;
 const trusted=s=>s==='portal'||s==='manual';
 const quotaSource=resolved.free_quota_bytes!=null?sources.free_quota_bytes:sources.quota_bytes,usedSource=resolved.used_free_bytes!=null?sources.used_free_bytes:sources.used_bytes;
 const progress=trusted(quotaSource)&&trusted(usedSource)&&quota>0&&used!=null?Math.max(0,Math.min(1,used/quota)):null;
 const estimated=resolved.used_bytes==null||price<=0?null:resolved.used_bytes/GiB*price;
 return {resolved,sources,freeRemaining,freeRemainingSource,funded,total,progress,estimated,errors};
}
function campusLivePanel(compact=false){
 const c=campusResolve(),latest=campusData?.latest,cfg=state.config||{};
 const host=el('div',{class:'campus-live-host'});
 host.append(el('div',{class:'campus-live'},
  el('div',{},el('small',{},tr('campus.balance')),el('strong',{},campusMoney(c.resolved.balance)),el('small',{},tr('campus.source.'+c.sources.balance))),
  el('div',{},el('small',{},tr('campus.free_remaining')),el('strong',{},campusBytes(c.freeRemaining)),el('small',{},tr('campus.source.'+c.freeRemainingSource))),
  el('div',{},el('small',{},tr('campus.funded_bytes')),el('strong',{},campusBytes(c.funded)),el('small',{},cfg.campus_unit_price>0?tr('campus.calculated'):tr('campus.unavailable'))),
  el('div',{},el('small',{},tr('campus.total_available')),el('strong',{},campusBytes(c.total)),el('small',{},tr('campus.calculated')))));
 if(!compact){
  host.append(el('div',{class:'campus-quality'},el('span',{class:'quality-badge'},campusQuality()),el('span',{},tr('campus.last_sync')+' '+campusLatestText()),
   el('span',{},tr('campus.portal_fee')+' '+(c.resolved.fee==null?'—':campusMoney(c.resolved.fee))+' · '+tr('campus.source.'+c.sources.fee)),
   el('span',{},tr('campus.estimated_fee')+' '+(c.estimated==null?'—':campusMoney(c.estimated))),
   el('span',{},tr('campus.paid_used')+' '+campusBytes(c.resolved.paid_used_bytes)+' · '+tr('campus.source.'+c.sources.paid_used_bytes))));
  if(c.progress!=null)host.append(el('div',{class:'campus-progress','aria-label':tr('campus.progress')},el('i',{style:`width:${c.progress*100}%`}),el('span',{},(c.progress*100).toFixed(1)+'%')));
  else host.append(el('small',{class:'muted'},tr('campus.progress_unknown')));
  if(c.errors.length)host.append(el('p',{class:'capture-error'},c.errors.map(e=>tr('campus.field.'+e.field)+': '+e.message).join('; ')));
  if(campusData?.rule_error)host.append(el('p',{class:'capture-error'},campusData.rule_error));
 }
 return host;
}
function campusDashboardCard(){
 if(!state.config?.campus_enabled)return null;const c=campusResolve();
 return el('article',{class:'kpi kpi-campus'},el('div',{class:'kpi-head'},metricIcon('network'),el('span',{},tr('campus.title'))),
  el('div',{class:'campus-card-values'},el('strong',{},campusMoney(c.resolved.balance)),el('span',{},tr('campus.free_remaining')+' '+campusBytes(c.freeRemaining)),
   el('span',{},tr('campus.total_available')+' '+campusBytes(c.total))),el('div',{class:'kpi-foot'},campusQuality()+' · '+campusLatestText()));
}
function campusNetworkPanel(){if(!state.config?.campus_enabled)return null;return panel(tr('campus.title'),el('div',{class:'campus-live-target'},campusLivePanel(false)),el('p',{class:'muted'},tr('campus.basis')));}
function campusModeSelect(field,current,redraw){
 const select=el('select',{'aria-label':tr('campus.mode')+' '+tr('campus.field.'+field)});
 const kind=campusFieldKind[field];for(const mode of campusModes.filter(m=>kind!=='text'||m!=='formula'))select.append(el('option',{value:mode},tr('campus.mode.'+mode)));select.value=current.mode||(current.selector?'auto':'disabled');
 select.onchange=()=>{current.mode=select.value;redraw();};return select;
}
function campusCandidateSelect(field,current){
 const items=campusCandidates[field]||[],select=el('select',{'aria-label':tr('campus.field.'+field)});select.append(el('option',{value:''},tr('campus.choose_value')));
 items.forEach((item,index)=>select.append(el('option',{value:String(index)},`${item.raw||item.value||''} · ${Math.round(Number(item.confidence||0)*100)}%`)));
 const matched=items.findIndex(item=>item.selector===current.selector);if(matched>=0)select.value=String(matched);
 select.onchange=()=>{const item=items[Number(select.value)];if(!item)return;current.mode=current.mode==='selector'?'selector':'auto';current.selector=item.selector;current.kind=item.kind||campusFieldKind[field];current.unit=item.unit||'';current.confidence=Number(item.confidence||0);};
 return select;
}
function renderCampusSettings(host){
 campusRenderHost=host;const cfg=state.config||{},enabled=el('input',{type:'checkbox',checked:Boolean(cfg.campus_enabled)});
 const url=el('input',{type:'url',value:cfg.campus_portal_url||'',placeholder:'http://202.204.48.66',maxlength:2048});
 const refresh=el('input',{type:'number',min:5,max:1440,value:cfg.campus_refresh_minutes||5});
 const price=el('input',{type:'number',min:0,max:1000,step:'0.01',value:cfg.campus_unit_price||0});
 const origins=el('textarea',{rows:3,placeholder:'https://cas.example.edu.cn'});origins.value=(cfg.campus_allowed_origins||[]).join('\n');
 const feedback=el('p',{class:'muted',role:'status'},campusEventStatus),dataHost=el('div',{class:'campus-fields'}),autoHost=el('div',{class:'campus-candidates'}),advanced=el('div');
 campusFieldState=structuredClone(campusData?.rule?.fields||{});
 const saveBasic=async()=>{try{await rpc('set_config',{campus_enabled:enabled.checked,campus_portal_url:url.value.trim(),campus_allowed_origins:origins.value.split(/\r?\n/).map(v=>v.trim()).filter(Boolean),campus_refresh_minutes:Number(refresh.value),campus_unit_price:Number(price.value)});state=await rpc('get_status');campusData=await rpc('get_campus_status');feedback.textContent=tr('campus.saved');PulseCampus.renderLive();}catch(e){error(e);}};
 const saveFields=async()=>{try{const fields={};for(const [field]of campusFields){const m=campusFieldState[field];if(!m)continue;const mode=m.mode||'disabled',kind=campusFieldKind[field];if((mode==='auto'||mode==='selector')&&!m.selector)continue;if(mode==='manual'&&(m.value==null||(kind!=='text'&&!Number.isFinite(Number(m.value)))))continue;if(mode==='formula'&&(!m.formula||kind==='text'))continue;const next={...m,mode};if(mode==='manual'&&kind!=='text'&&m.reset_on_cycle!==false)next.cycle=campusCycle();for(const key of ['reset','raw','manual','field'])delete next[key];fields[field]=next;}campusFieldState=fields;await rpc('save_campus_mapping',{fields});campusData=await rpc('get_campus_status');feedback.textContent=tr('campus.mapping_saved');drawData();drawCandidates();PulseCampus.renderLive();}catch(e){error(e);}};
 const drawData=()=>{dataHost.replaceChildren();for(const [field,kind]of campusFields){const mapping=campusFieldState[field]||(campusFieldState[field]={mode:'disabled',kind});mapping.kind=mapping.kind||kind;
   const control=el('div',{class:'campus-mode-control'});
   if(mapping.mode==='manual'){if(kind==='text'){const input=el('input',{value:mapping.value==''?'':mapping.value??'',maxlength:128});input.oninput=()=>{mapping.value=input.value;mapping.unit='text';};control.append(input,el('small',{},tr('campus.manual_text')));}else{const unit=el('select',{},...(['currency'].includes(kind)?['CNY']:['B','KB','MB','GB','KiB','MiB','GiB']).map(v=>el('option',{value:v},v)));unit.value=mapping.unit||unit.options[0].value;const input=el('input',{type:'number',min:0,step:'any',value:mapping.value??''});input.oninput=()=>{mapping.value=input.value===''?null:Number(input.value);mapping.unit=unit.value;};unit.onchange=()=>mapping.unit=unit.value;const reset=el('input',{type:'checkbox',checked:mapping.reset_on_cycle!==false});reset.onchange=()=>{mapping.reset_on_cycle=reset.checked;mapping.cycle=campusCycle();};control.append(input,unit,el('label',{class:'checkbox'},reset,tr('campus.reset_cycle')));}}
   else if(mapping.mode==='formula'){const formula=el('input',{value:mapping.formula||'',placeholder:'free_quota_bytes - used_free_bytes'});formula.oninput=()=>mapping.formula=formula.value;control.append(formula,el('small',{},tr('campus.formula_help')));}
   else if(mapping.mode==='auto'||mapping.mode==='selector'){control.append(el('small',{},mapping.selector?tr('campus.selector_saved'):tr('campus.selector_missing')));}
   else control.append(el('small',{},tr('campus.mode.disabled')));
   const row=el('div',{class:'campus-field-row'},el('strong',{},tr('campus.field.'+field)),campusModeSelect(field,mapping,()=>{drawData();drawCandidates();}),control);dataHost.append(row);
  }dataHost.append(el('div',{class:'actions'},el('button',{onclick:saveFields},tr('campus.confirm_mapping'))));};
 const drawCandidates=()=>{autoHost.replaceChildren();for(const [field]of campusFields){const mapping=campusFieldState[field]||(campusFieldState[field]={mode:'disabled',kind:campusFieldKind[field]});if(!(campusCandidates[field]?.length)&&mapping.mode!=='auto'&&mapping.mode!=='selector')continue;
   autoHost.append(el('div',{class:'campus-candidate-row'},el('strong',{},tr('campus.field.'+field)),campusCandidateSelect(field,mapping),el('button',{class:'secondary',onclick:async()=>{try{campusPickerField=field;await rpc('campus_pick_field',{field});feedback.textContent=tr('campus.pick_hint',{field:tr('campus.field.'+field)});}catch(e){error(e);}}},tr('campus.pick'))));}
  autoHost.append(el('div',{class:'actions'},el('button',{onclick:saveFields},tr('campus.confirm_mapping')),el('button',{class:'secondary',onclick:async()=>{try{await rpc('campus_rescan');feedback.textContent=tr('campus.rescanning');}catch(e){error(e);}}},tr('campus.rescan'))));};
 drawData();drawCandidates();dataHost.redraw=drawData;autoHost.redraw=drawCandidates;
 const basic=el('div',{class:'campus-basic'},el('div',{class:'settings-grid'},el('label',{class:'checkbox'},enabled,tr('campus.enable')),el('label',{},el('strong',{},tr('campus.url')),url),el('label',{},el('strong',{},tr('campus.refresh')),refresh)),el('div',{class:'actions'},el('button',{onclick:async()=>{await saveBasic();try{await rpc('open_campus_portal');feedback.textContent=tr('campus.login_hint');}catch(e){error(e);}}},tr('campus.open')),el('button',{class:'secondary',onclick:saveBasic},tr('campus.save'))));
 advanced.append(el('div',{class:'settings-grid'},el('label',{},el('strong',{},tr('campus.allowed')),origins,el('small',{},tr('campus.allowed_hint'))),el('label',{},el('strong',{},tr('campus.price')),price,el('small',{},tr('campus.price_hint')))),el('p',{class:'muted'},tr('campus.privacy')),el('div',{class:'actions'},el('button',{class:'secondary',onclick:saveBasic},tr('campus.save')),el('button',{class:'danger',onclick:async()=>{try{await rpc('delete_campus_rule');campusFieldState={};campusCandidates={};campusData=await rpc('get_campus_status');drawData();drawCandidates();}catch(e){error(e);}}},tr('campus.delete_rule'))));
 host.append(el('details',{class:'campus-section',open:''},el('summary',{},tr('campus.basic')),basic),el('details',{class:'campus-section',open:''},el('summary',{},tr('campus.data_config')),dataHost),el('details',{class:'campus-section'},el('summary',{},tr('campus.auto_config')),autoHost),el('details',{class:'campus-section'},el('summary',{},tr('campus.advanced')),advanced),feedback,el('div',{class:'campus-live-target'},campusLivePanel(true)));
}
const PulseCampus={
 async refresh(){try{campusData=await rpc('get_campus_status');}catch(e){campusData={...(campusData||{}),error:String(e.message||e)};}return campusData;},
 renderSettings:renderCampusSettings,dashboardCard:campusDashboardCard,networkPanel:campusNetworkPanel,livePanel:campusLivePanel,
 renderLive(){document.querySelectorAll('.campus-live-target').forEach(node=>node.replaceChildren(campusLivePanel(false)));},
 ingest(event){
  if(!event||!event.type)return;campusEventStatus='';
  if(event.type==='campus_candidates'){for(const [field,items]of Object.entries(event.candidates||{}))if(Array.isArray(items))campusCandidates[field]=items;if(!campusData?.rule)campusEventStatus='low_confidence';}
  else if(event.type==='campus_candidate'&&event.field&&event.candidate){const items=campusCandidates[event.field]||[];items.unshift(event.candidate);campusCandidates[event.field]=items.slice(0,5);campusFieldState[event.field]={mode:'selector',selector:event.candidate.selector,kind:event.candidate.kind||campusFieldKind[event.field],unit:event.candidate.unit||'',confidence:Number(event.candidate.confidence||0)};campusEventStatus=tr('campus.manual_candidate');}
  else if(event.type==='campus_rule_stale')campusEventStatus='stale';
  else if(event.type==='snapshot_saved'&&event.snapshot){campusData=campusData||{};campusData.latest=event.snapshot;campusEventStatus='confirmed';}
  else if(event.type==='snapshot_failed'||event.type==='campus_error')campusEventStatus=event.error||event.message||tr('campus.sync_failed');
  if(page==='settings'){document.querySelector('.campus-fields')?.redraw?.();document.querySelector('.campus-candidates')?.redraw?.();}
  this.renderLive();
 }
};
