'use strict';
let campusData=null,campusCandidates={},campusEventStatus='',campusLastEventType='',campusPickerField='',campusPickerMessage='',campusFieldState={},campusRenderHost=null,campusStateOrigin="";
const GiB=1024**3,GB=1e9,MiB=1024**2;
const campusFields=[
 ['balance','currency'],['free_quota_bytes','bytes'],['free_remaining_bytes','bytes'],['used_free_bytes','bytes'],
 ['spendable_balance','currency'],['paid_used_bytes','bytes'],['used_bytes','bytes'],['remaining_bytes','bytes'],
 ['quota_bytes','bytes'],['fee','currency'],['billing_cycle','text']
];
const campusFieldKind=Object.fromEntries(campusFields);
function campusBytes(value){return value==null?'—':bytes(Number(value));}
function campusMoney(value){return value==null?'—':'¥'+Number(value).toFixed(2);}
function campusLatestText(){return campusData?.latest?.observed_ms?new Date(campusData.latest.observed_ms).toLocaleString(PulseI18n.locale):tr('campus.never_synced');}
function campusQuality(){const runtime={campus_syncing:'campus.syncing',campus_auth_required:'campus.auth_required',campus_rule_stale:'campus.rule_stale',campus_error:'campus.sync_failed',snapshot_failed:'campus.sync_failed',snapshot_saved:'campus.confirmed',campus_candidates:'campus.needs_confirmation'};const s=campusEventStatus||(runtime[campusData?.runtime_status]?tr(runtime[campusData.runtime_status]):campusData?.latest?.quality)||'';if(s&&!['confirmed','auto_recognized','low_confidence','stale','unknown'].includes(s))return s;return s==='confirmed'?tr('campus.confirmed'):s==='auto_recognized'?tr('campus.auto_short'):s==='low_confidence'?tr('campus.needs_confirmation'):s==='stale'?tr('campus.rule_stale'):s==='unknown'?tr('campus.unknown'):tr('campus.never_synced');}
function campusUnitFactor(unit){return ({B:1,KB:1e3,MB:1e6,GB:1e9,TB:1e12,KiB:1024,MiB:1024**2,GiB:1024**3,TiB:1024**4,CNY:1})[unit]||1;}
function campusCycle(){
 const mapping=campusFieldState.billing_cycle||campusData?.rule?.fields?.billing_cycle;
 if(mapping?.mode==='manual'&&mapping.kind==='text'&&mapping.value)return String(mapping.value);
 return String(campusData?.latest?.cycle_text||'');
}
function campusWebValues(){const d=campusData?.latest?.details?.web_values||{},l=campusData?.latest||{},out={...d};for(const [name]of campusFields)if(out[name]==null&&l[name]!=null)out[name]=l[name];return out;}
function campusMapping(field){return campusFieldState[field]||campusData?.rule?.fields?.[field]||{mode:'disabled'};}
function campusManualValue(m,cycle=campusCycle()){
 if(m.value==null||!Number.isFinite(Number(m.value)))return null;
 if(m.reset_on_cycle&&(!m.cycle||!cycle||m.cycle!==cycle))return null;
 return Number(m.value)*campusUnitFactor(m.unit||'B');
}
function campusManualExpired(m,cycle=campusCycle()){return Boolean(m.reset_on_cycle&&(!m.cycle||!cycle||m.cycle!==cycle));}
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
function campusResolve(confirmed=false){
 const web=campusWebValues(),resolved={},sources={},visiting=new Set(),errors=[];
 const saved=campusData?.rule?.fields||{},billing=saved.billing_cycle;
 const cycle=confirmed?(billing?.mode==='manual'?String(billing.value||''):String(campusData?.latest?.cycle_text||'')):campusCycle();
 const resolve=field=>{
  if(field in resolved)return resolved[field];if(visiting.has(field))throw new Error(tr('campus.formula_cycle',{field}));
  visiting.add(field);const m=confirmed?(saved[field]||{mode:'disabled'}):campusMapping(field),mode=m.mode||(m.selector?'auto':'disabled');let value=null,source='unknown';
  try{
   if((mode==='auto'||mode==='selector')&&web[field]!=null){value=campusFieldKind[field]==='text'?String(web[field]):Number(web[field]);source='portal';}
   else if(mode==='manual'){value=campusFieldKind[field]==='text'?String(m.value??''):campusManualValue(m,cycle);source=campusManualExpired(m,cycle)?'cycle_reset':'manual';}
   else if(mode==='formula'){value=campusFormula(m.formula,name=>resolve(name));source='formula';}
  }catch(e){errors.push({field,message:String(e.message||e)});value=null;source='error';}
  visiting.delete(field);
  resolved[field]=campusFieldKind[field]==='text'?(typeof value==='string'&&value?value:null):(Number.isFinite(value)&&value>=0?value:null);
  if(resolved[field]==null&&mode==='disabled')source='disabled';sources[field]=source;return resolved[field];
 };
 for(const [field]of campusFields)resolve(field);
 // Generic plan totals and remaining data are not necessarily free allowances.
 const freeQuota=resolved.free_quota_bytes,freeUsed=resolved.used_free_bytes;
 const calculatedFree=freeQuota!=null&&freeUsed!=null?Math.max(0,freeQuota-freeUsed):null;
 const freeRemaining=resolved.free_remaining_bytes??calculatedFree;
 const freeRemainingSource=resolved.free_remaining_bytes!=null?sources.free_remaining_bytes:calculatedFree!=null?'calculated':'unknown';
 const price=Number(state.config?.campus_unit_price||0),priceBytes=state.config?.campus_price_unit==='GB'?GB:GiB,funded=price>0&&resolved.spendable_balance!=null?resolved.spendable_balance/price*priceBytes:null;
 const total=freeRemaining!=null&&funded!=null?freeRemaining+funded:null;
 const quota=freeQuota,used=freeUsed;
 const trusted=s=>s==='portal'||s==='manual';
 const quotaSource=resolved.free_quota_bytes!=null?sources.free_quota_bytes:sources.quota_bytes,usedSource=resolved.used_free_bytes!=null?sources.used_free_bytes:sources.used_bytes;
 const progress=trusted(quotaSource)&&trusted(usedSource)&&quota>0&&used!=null?Math.max(0,Math.min(1,used/quota)):null;
 // Paid usage has already excluded the free allowance. Never charge all usage again.
 const estimated=resolved.paid_used_bytes==null||price<=0?null:resolved.paid_used_bytes/priceBytes*price;
 return {resolved,sources,freeRemaining,freeRemainingSource,funded,total,progress,estimated,errors};
}
function campusLivePanel(compact=false){
 const c=campusResolve(),latest=campusData?.latest,cfg=state.config||{};
 const host=el('div',{class:'campus-live-host'});
 host.append(el('div',{class:'campus-live'},
  el('div',{},el('small',{},tr('campus.balance')),el('strong',{},campusMoney(c.resolved.balance)),el('small',{},tr('campus.source.'+c.sources.balance))),
  el('div',{},el('small',{},tr('campus.used')),el('strong',{},campusBytes(c.resolved.used_bytes)),el('small',{},tr('campus.source.'+c.sources.used_bytes))),
  el('div',{},el('small',{},tr('campus.free_remaining')),el('strong',{},c.freeRemaining==null?tr('campus.unavailable'):campusBytes(c.freeRemaining)),el('small',{},tr('campus.source.'+c.freeRemainingSource))),
  el('div',{},el('small',{},tr('campus.funded_bytes')),el('strong',{},c.funded==null?tr('campus.unavailable'):campusBytes(c.funded)),el('small',{},c.funded!=null?tr('campus.calculated'):tr('campus.unavailable'))),
  el('div',{},el('small',{},tr('campus.total_available')),el('strong',{},c.total==null?tr('campus.unavailable'):campusBytes(c.total)),el('small',{},c.total!=null?tr('campus.calculated'):tr('campus.unavailable')))));
 if(!compact){
  host.append(el('div',{class:'campus-quality'},el('span',{class:'quality-badge'},campusQuality()),el('span',{},tr('campus.last_sync')+' '+campusLatestText()),el('span',{},tr('campus.data_source')+' '+(campusData?.latest?.portal_url||'—')),
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
function campusOverviewModel(){
 const c=campusResolve(true),fields=campusData?.rule?.fields||{},cfg=state.config||{};
 const raw=field=>c.resolved[field]!=null&&['portal','manual'].includes(c.sources[field]);
 // Accept the two built-in billing choices, not arbitrary legacy formulas.
 const formula=(field,text)=>fields[field]?.mode==='formula'&&String(fields[field].formula).replace(/\s/g,'')===text;
 const usedFree=raw('used_free_bytes')||formula('used_free_bytes','min(used_bytes,free_quota_bytes)')&&raw('used_bytes')&&raw('free_quota_bytes');
 const free=raw('free_remaining_bytes')?c.resolved.free_remaining_bytes:
  c.resolved.free_remaining_bytes==null&&raw('free_quota_bytes')&&usedFree?c.freeRemaining:null;
 const spendable=raw('spendable_balance')||formula('spendable_balance','balance')&&raw('balance');
 const price=Number(cfg.campus_unit_price),funded=spendable&&Number.isFinite(price)&&price>0&&
  (cfg.campus_price_unit==null||['GB','GiB'].includes(cfg.campus_price_unit))&&Number.isFinite(c.funded)?c.funded:null;
 const total=free!=null&&funded!=null&&Number.isFinite(free+funded)?free+funded:null;
 const values={balance:raw('balance')?c.resolved.balance:null,used:raw('used_bytes')?c.resolved.used_bytes:null,free,funded,total};
 const runtime=campusLastEventType||campusData?.runtime_status;
 let status=!cfg.campus_enabled?'disabled':runtime==='campus_auth_required'?'auth':
  campusData?.error||campusData?.rule_error||['campus_error','snapshot_failed'].includes(runtime)?'error':
  runtime==='campus_rule_stale'?'stale':Object.values(values).every(v=>v==null)?'empty':
  Object.values(values).some(v=>v==null)?'partial':'ready';
 const percent=!['disabled','auth','error','stale'].includes(status)&&raw('free_quota_bytes')&&raw('used_free_bytes')&&
  c.resolved.free_quota_bytes>0&&c.resolved.used_free_bytes<=c.resolved.free_quota_bytes?c.progress:null;
 const segments=['used','free','funded'].filter(key=>values[key]!=null&&values[key]>0).map(key=>({key,value:values[key]}));
 const sum=segments.reduce((sum,item)=>sum+item.value,0);
 if(!Number.isFinite(sum))segments.length=0;
 return {values,status,percent,segments,sum,freeSource:raw('free_remaining_bytes')?c.sources.free_remaining_bytes:'calculated',balanceSource:c.sources.balance,
  syncing:runtime==='campus_syncing',observed:campusData?.latest?.observed_ms||null,
  source:campusData?.latest?.portal_url||campusData?.portal_url||cfg.campus_portal_url||''};
}
function campusOverviewContent(host,m){
 const value=(key,money=false)=>m.values[key]==null?tr('campus.unavailable'):money?campusMoney(m.values[key]):campusBytes(m.values[key]);
 const statusKey={disabled:'campus.card.disabled',auth:'campus.auth_required',error:'campus.sync_failed',stale:'campus.rule_stale',empty:'campus.card.empty',partial:'campus.card.partial',ready:'campus.card.ready'}[m.status];
 const header=el('header',{class:'campus-overview-header'},el('div',{class:'campus-overview-heading'},metricIcon('network'),el('h3',{},tr('campus.title')),
  el('span',{class:'campus-sync-state',role:'status'},tr(m.syncing&&m.status!=='disabled'?'campus.syncing':statusKey))),
  el('span',{class:'campus-sync-time'},m.observed&&m.status!=='disabled'?tr('campus.card.synced',{time:new Date(m.observed).toLocaleString(PulseI18n.locale)}):tr('campus.never_synced')));
 host.dataset.status=m.status;host.replaceChildren(header);
 if(m.status==='disabled'){
  host.append(el('div',{class:'campus-disabled'},el('p',{},tr('campus.card.disabled_hint')),
   el('button',{class:'secondary',onclick:()=>navigate('settings')},tr('campus.card.configure'))));return;
 }
 const metric=(key,title,kind,source)=>el('div',{class:'campus-metric campus-metric-'+key},
  el('div',{class:'campus-metric-label'},metricIcon(kind),el('span',{},tr(title))),
  el('strong',{'data-campus-value':key,class:m.values[key]==null?'campus-value-missing':''},value(key,key==='balance')),
  el('small',{},m.values[key]==null?'':tr(source)));
 const metrics=el('div',{class:'campus-metrics'},metric('balance','campus.balance','wallet',m.balanceSource==='portal'?'campus.card.portal':'campus.source.manual'),
  metric('free','campus.free_remaining','network','campus.source.'+m.freeSource),metric('total','campus.total_available','protocol','campus.card.estimate'));
 const legendNames={used:'campus.used',free:'campus.free_remaining',funded:'campus.card.funded'};
 const chart=el('section',{class:'campus-usage'},el('div',{class:'campus-usage-heading'},el('h4',{},tr('campus.card.usage')),
  m.percent==null?null:el('span',{class:'campus-free-percent'},tr('campus.card.free_percent',{percent:(m.percent*100).toFixed(1)}))));
 const track=el('div',{class:'campus-segment-track',role:'img','aria-label':tr('campus.card.known_mix')+'; '+Object.entries(legendNames).map(([key,label])=>tr(label)+': '+value(key)).join('; ')});
 for(const item of m.segments){
  const segment=el('i',{class:'campus-segment campus-tone-'+item.key,'aria-hidden':'true'});
  // CSSOM properties work under the desktop CSP; style attributes are blocked.
  segment.style.width=(item.value/m.sum*100)+'%';track.append(segment);
 }
 chart.append(track,el('p',{class:'campus-chart-caption'},tr(['used','free','funded'].some(key=>m.values[key]!=null)?'campus.card.known_mix':'campus.progress_unknown')),
  el('div',{class:'campus-legend'},...Object.entries(legendNames).map(([key,label])=>el('div',{'data-campus-part':key},
   el('span',{},el('i',{class:'campus-swatch campus-tone-'+key,'aria-hidden':'true'}),tr(label)),el('strong',{},value(key))))),
  el('div',{class:'campus-available-total'},el('span',{},tr('campus.total_available')),el('strong',{},value('total'))),
  el('small',{class:'campus-total-basis'},tr('campus.card.total_basis')));
 host.append(el('div',{class:'campus-overview-body'},metrics,chart));
 if(['auth','error','stale'].includes(m.status))host.append(el('p',{class:'campus-overview-notice'},tr(Object.values(m.values).some(v=>v!=null)?'campus.card.retained':'campus.card.empty_hint')));
 else if(m.status==='empty'||m.status==='partial')host.append(el('p',{class:'campus-overview-notice'},tr(m.status==='empty'?'campus.card.empty_hint':'campus.card.partial_hint')));
 host.append(el('footer',{class:'campus-overview-source'},tr('campus.data_source')+' · '+(m.source||'—')));
}
function campusUpdateOverview(host,m){
 const key=JSON.stringify([m,PulseI18n.locale]);if(host._campusKey===key)return;
 campusOverviewContent(host,m);host._campusKey=key;
}
function campusOverviewCard(){const host=el('article',{class:'campus-overview','aria-label':tr('campus.title')});campusUpdateOverview(host,campusOverviewModel());return host;}
function campusDashboardCard(){return campusOverviewCard();}
function campusNetworkPanel(){return campusOverviewCard();}
function campusSuggest(){
 for(const [field,items]of Object.entries(campusCandidates)){
  if(!campusFieldKind[field]||!items.length||campusFieldState[field]||campusData?.rule?.fields?.[field])continue;
  const item=items[0];if(item.value==null||!item.selector)continue;
  campusFieldState[field]={mode:'auto',selector:item.selector,kind:campusFieldKind[field],unit:item.unit||'',confidence:Number(item.confidence||0)};
 }
}
function campusCandidateSelect(field,current){
 const items=campusCandidates[field]||[],select=el('select',{'aria-label':tr('campus.field.'+field)});
 select.append(el('option',{value:''},current.selector?tr('campus.selector_saved'):tr('campus.not_mapped')));
 items.forEach((item,index)=>select.append(el('option',{value:String(index)},`${item.raw||item.value||''} · ${Math.round(Number(item.confidence||0)*100)}%`)));
 const matched=items.findIndex(item=>item.selector===current.selector);if(matched>=0)select.value=String(matched);
 select.onchange=()=>{if(select.value==='')return;const item=items[Number(select.value)];if(item?.value==null)return;campusFieldState[field]={mode:'selector',selector:item.selector,kind:campusFieldKind[field],unit:item.unit||'',confidence:Number(item.confidence||0)};};
 return select;
}
function campusPickerFeedback(){document.querySelectorAll('.campus-pick-feedback').forEach(node=>node.textContent=node.dataset.field===campusPickerField?campusPickerMessage:'');}
function renderCampusSettings(host){
 campusRenderHost=host;host.classList.add('campus-settings');const cfg=state.config||{};
 if(campusStateOrigin!==campusData?.origin){campusFieldState=structuredClone(campusData?.rule?.fields||{});campusStateOrigin=campusData?.origin||'';}
 campusSuggest();
 const input=(type,value,attrs={})=>el('input',{type,value,...attrs});
 const enabled=input('checkbox','',{checked:Boolean(cfg.campus_enabled)}),url=input('url',cfg.campus_portal_url||'',{placeholder:'https://portal.example.edu.cn',maxlength:2048});
 const refresh=input('number',cfg.campus_refresh_minutes||5,{min:5,max:1440}),price=input('number',cfg.campus_unit_price||0,{min:0,max:1000,step:'any'});
 const priceUnit=el('select',{'aria-label':tr('campus.price_unit')},el('option',{value:'GB'},'GB'),el('option',{value:'GiB'},'GiB'));priceUnit.value=cfg.campus_price_unit||'GiB';
 const origins=el('textarea',{rows:2,placeholder:'https://cas.example.edu.cn'});origins.value=(cfg.campus_allowed_origins||[]).join('\n');
 const feedback=el('p',{class:'campus-feedback muted',role:'status'},campusEventStatus),rulesHost=el('div',{class:'campus-candidates'}),preview=el('p',{class:'muted campus-preview'});
 const label=(key,...children)=>el('label',{},el('strong',{},tr(key)),...children);
 const configPayload=()=>({campus_enabled:enabled.checked,campus_portal_url:url.value.trim(),campus_allowed_origins:origins.value.split(/\r?\n/).map(v=>v.trim()).filter(Boolean),campus_refresh_minutes:Number(refresh.value),campus_unit_price:Number(price.value),campus_price_unit:priceUnit.value});
 let saves=Promise.resolve();
 const persist=task=>{saves=saves.catch(()=>{}).then(async()=>{feedback.textContent=tr('campus.saving');try{await task();const next=await rpc('get_campus_status');if(campusData?.origin!==next.origin){campusFieldState={};campusCandidates={};campusStateOrigin='';}campusData=next;feedback.textContent=tr('campus.saved');PulseCampus.renderLive();}catch(e){feedback.textContent=String(e.message||e);throw e;}});return saves;};
 const saveBasic=()=>{const payload=configPayload();return persist(async()=>{if(!url.checkValidity()||!refresh.checkValidity()||!price.checkValidity())throw Error(tr('campus.invalid_settings'));await rpc('set_config',payload);state=await rpc('get_status');});};
 const saveFields=(allowEmpty=false)=>{const fields=structuredClone(campusFieldState);
  // Billing edits must not implicitly confirm new page candidates.
  if(allowEmpty)for(const [field,m]of Object.entries(fields))if(m.mode==='auto'||m.mode==='selector'){
   const saved=campusData?.rule?.fields?.[field];if(saved)fields[field]=structuredClone(saved);else delete fields[field];
  }
  return persist(async()=>{if(!allowEmpty&&!Object.keys(fields).length)throw Error(tr('campus.no_selection'));await rpc('save_campus_mapping',{fields});try{await rpc('campus_rescan');}catch{/* Rule remains saved if the portal is closed. */}});};
 for(const control of [enabled,url,refresh,price,priceUnit,origins])control.onchange=()=>saveBasic().catch(()=>{});
 const drawRules=()=>{
  rulesHost.replaceChildren();campusSuggest();const recognized=[];
  for(const [field]of campusFields){const current=campusFieldState[field]||campusData?.rule?.fields?.[field]||{mode:'disabled'},items=campusCandidates[field]||[];
   const selected=items.find(item=>item.selector===current.selector);if(selected)recognized.push(tr('campus.field.'+field)+': '+selected.raw);
   const localStatus=el('small',{class:'campus-pick-feedback','data-field':field,role:'status'},field===campusPickerField?campusPickerMessage:'');
   const pick=el('button',{class:'secondary',onclick:async()=>{
    campusPickerField=field;campusPickerMessage=tr('campus.pick_opening');localStatus.textContent=campusPickerMessage;campusPickerFeedback();pick.disabled=true;
    try{await saves;await rpc('campus_pick_field',{field});}catch(e){campusPickerMessage=String(e.message||e);localStatus.textContent=campusPickerMessage;feedback.textContent=campusPickerMessage;}
    finally{pick.disabled=false;campusPickerFeedback();}
   }},tr('campus.pick'));
   rulesHost.append(el('div',{class:'campus-candidate-row'},el('strong',{},tr('campus.field.'+field)),campusCandidateSelect(field,current),pick,localStatus));
  }
  preview.textContent=recognized.length?tr('campus.review_hint')+' '+recognized.join(' · '):tr('campus.login_hint');
 };
 rulesHost.redraw=drawRules;drawRules();
 const manualParam=(field,kind)=>{const mapping=campusFieldState[field],manual=mapping?.mode==='manual';
  const value=input(kind==='text'?'text':'number',manual?mapping.value:'',{min:0,step:'any',placeholder:tr('campus.automatic'),maxlength:128});
  const unit=kind==='bytes'?el('select',{'aria-label':tr('campus.field.'+field)+' '+tr('campus.unit')},...['GB','GiB'].map(v=>el('option',{value:v},v))):null;
  if(unit)unit.value=manual&&['GB','GiB'].includes(mapping.unit)?mapping.unit:'GiB';
  if(unit&&manual&&!['GB','GiB'].includes(mapping.unit))value.value=Number(mapping.value)*campusUnitFactor(mapping.unit)/GiB;
  const update=()=>{if(!value.checkValidity())return;if(value.value===''){delete campusFieldState[field];}else campusFieldState[field]={mode:'manual',kind,value:kind==='text'?value.value:Number(value.value),unit:unit?.value||'text',reset_on_cycle:false};saveFields(true).catch(()=>{});};
  value.onchange=update;if(unit)unit.onchange=update;return label('campus.field.'+field,el('div',{class:'campus-param'},value,unit));
 };
 const balance=el('input',{type:'checkbox',checked:campusFieldState.spendable_balance?.mode==='formula'&&campusFieldState.spendable_balance?.formula==='balance'});
 balance.onchange=()=>{if(balance.checked)campusFieldState.spendable_balance={mode:'formula',kind:'currency',formula:'balance'};else delete campusFieldState.spendable_balance;saveFields(true).catch(()=>{});};
 const freeFirst=el('input',{type:'checkbox',checked:campusFieldState.used_free_bytes?.formula==='min(used_bytes, free_quota_bytes)'});
 freeFirst.onchange=()=>{if(freeFirst.checked){campusFieldState.used_free_bytes={mode:'formula',kind:'bytes',formula:'min(used_bytes, free_quota_bytes)'};campusFieldState.paid_used_bytes={mode:'formula',kind:'bytes',formula:'max(0, used_bytes - free_quota_bytes)'};}else{for(const field of ['used_free_bytes','paid_used_bytes'])if(campusFieldState[field]?.mode==='formula')delete campusFieldState[field];}saveFields(true).catch(()=>{});};
 const basic=el('section',{class:'campus-section'},el('h4',{},tr('campus.basic')),el('label',{class:'checkbox'},enabled,tr('campus.enable')),el('div',{class:'settings-grid'},label('campus.url',url),label('campus.refresh',refresh)),el('div',{class:'actions'},el('button',{onclick:async()=>{try{await saveBasic();await rpc('open_campus_portal');feedback.textContent=tr('campus.login_hint');}catch(e){feedback.textContent=String(e.message||e);}}},tr('campus.open'))));
 const overview=el('section',{class:'campus-section'},el('h4',{},tr('campus.overview')),el('div',{class:'campus-live-target'},campusLivePanel(false)),preview,el('div',{class:'actions'},el('button',{onclick:()=>saveFields().catch(()=>{})},tr('campus.confirm_results'))));
 const billing=el('section',{class:'campus-section'},el('h4',{},tr('campus.billing')),el('div',{class:'settings-grid'},manualParam('free_quota_bytes','bytes'),label('campus.price',el('div',{class:'campus-param'},price,priceUnit)),manualParam('billing_cycle','text')),el('label',{class:'checkbox'},balance,tr('campus.balance_for_data')),el('label',{class:'checkbox'},freeFirst,tr('campus.free_first')),el('p',{class:'muted'},tr('campus.billing_hint')));
 const advanced=el('details',{class:'campus-section'},el('summary',{},tr('campus.advanced')),label('campus.allowed',origins,el('small',{},tr('campus.allowed_hint'))),rulesHost,el('p',{class:'muted'},tr('campus.privacy')),el('div',{class:'actions'},el('button',{class:'secondary',onclick:async()=>{try{await rpc('campus_rescan');feedback.textContent=tr('campus.rescanning');}catch(e){feedback.textContent=String(e.message||e);}}},tr('campus.rescan')),el('button',{class:'danger',onclick:async()=>{try{await rpc('delete_campus_rule');campusFieldState={};campusCandidates={};campusData=await rpc('get_campus_status');drawRules();PulseCampus.renderLive();}catch(e){feedback.textContent=String(e.message||e);}}},tr('campus.delete_rule'))));
 basic.append(el('p',{class:'muted'},tr('campus.background_hint')));host.append(basic,overview,billing,advanced,feedback);
}
const PulseCampus={
 async refresh(){try{const next=await rpc('get_campus_status');if(campusData?.origin!==next.origin){campusFieldState={};campusCandidates={};campusStateOrigin='';campusLastEventType='';}campusData=next;if(next.runtime_status){campusEventStatus='';campusLastEventType=next.runtime_status;}}catch(e){campusData={...(campusData||{}),error:String(e.message||e)};}this.renderLive();return campusData;},
 renderSettings:renderCampusSettings,dashboardCard:campusDashboardCard,networkPanel:campusNetworkPanel,livePanel:campusLivePanel,
 renderLive(){document.querySelectorAll('.campus-live-target').forEach(node=>node.replaceChildren(campusLivePanel(false)));const hosts=document.querySelectorAll('.campus-overview');if(hosts.length){const m=campusOverviewModel();hosts.forEach(node=>campusUpdateOverview(node,m));}},
 ingest(event){
  if(!event||!event.type||event.portal_origin&&event.portal_origin!==campusData?.origin)return;campusEventStatus='';
  if(['campus_picker_pending','campus_picker_ready','campus_picker_failed','campus_picker_cancelled'].includes(event.type)){
   campusPickerField=event.field||campusPickerField;
   campusPickerMessage=tr(event.type==='campus_picker_ready'?'campus.pick_hint':event.type==='campus_picker_failed'?'campus.pick_failed':event.type==='campus_picker_cancelled'?'campus.pick_cancelled':event.auth_required?'campus.pick_login':'campus.pick_opening',{field:tr('campus.field.'+campusPickerField)});
   campusPickerFeedback();return;
  }
  campusLastEventType=event.type;
  if(event.type==='campus_candidates'){campusCandidates={};for(const [field,items]of Object.entries(event.candidates||{}))if(campusFieldKind[field]&&Array.isArray(items))campusCandidates[field]=items;campusSuggest();if(!campusData?.rule)campusEventStatus='low_confidence';}
  else if(event.type==='campus_candidate'&&event.field&&event.candidate){const items=campusCandidates[event.field]||[];items.unshift(event.candidate);campusCandidates[event.field]=items.slice(0,5);campusFieldState[event.field]={mode:'selector',selector:event.candidate.selector,kind:event.candidate.kind||campusFieldKind[event.field],unit:event.candidate.unit||'',confidence:Number(event.candidate.confidence||0)};campusEventStatus=tr('campus.manual_candidate');campusPickerMessage=campusEventStatus;}
  else if(event.type==='campus_syncing')campusEventStatus=tr('campus.syncing');
  else if(event.type==='campus_auth_required')campusEventStatus=tr('campus.auth_required');
  else if(event.type==='campus_rule_stale')campusEventStatus='stale';
  else if(event.type==='snapshot_saved'&&event.snapshot){campusData=campusData||{};campusData.latest=event.snapshot;campusEventStatus='confirmed';}
  else if(event.type==='snapshot_failed'||event.type==='campus_error')campusEventStatus=event.error||event.message||tr('campus.sync_failed');
  if(page==='settings'){document.querySelector('.campus-candidates')?.redraw?.();}
  this.renderLive();
 }
};
