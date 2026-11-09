'use strict';
let campusData=null,campusCandidates={},campusEventStatus='',campusLastEventType='',campusPickerField='',campusPickerMessage='',campusFieldState={},campusRenderHost=null,campusStateOrigin="";
const GiB=1024**3,GB=1e9,MiB=1024**2;
const campusFields=[
 ['balance','currency'],['free_quota_bytes','bytes'],['free_remaining_bytes','bytes'],['used_free_bytes','bytes'],
 ['spendable_balance','currency'],['paid_used_bytes','bytes'],['used_bytes','bytes'],['remaining_bytes','bytes'],
 ['quota_bytes','bytes'],['fee','currency'],['billing_cycle','text']
];
const campusFieldKind=Object.fromEntries(campusFields);
const campusPrimaryFields=['balance','used_bytes'];
function campusRadix(){return state.config?.campus_traffic_radix===1000?1000:1024;}
function campusBytes(value){return value==null?'—':(Number(value)/campusRadix()**3).toLocaleString(PulseI18n.locale,{minimumFractionDigits:2,maximumFractionDigits:2})+' GB';}
function campusPortalBytes(field,value,source){
 return campusBytes(value);
}
function campusConversion(value){const raw=campusReadings().used_bytes;return raw?tr('campus.raw_reading',{value:raw.value+' '+raw.unit}):tr('campus.needs_confirmation');}
function campusUnitNote(){return tr('campus.radix_note',{radix:campusRadix()});}
function campusMoney(value){return value==null?'—':'¥'+Number(value).toFixed(2);}
function campusLatestText(){return campusData?.latest?.observed_ms?new Date(campusData.latest.observed_ms).toLocaleString(PulseI18n.locale):tr('campus.never_synced');}
function campusQuality(){const runtime={campus_syncing:'campus.syncing',campus_auth_required:'campus.auth_required',campus_rule_stale:'campus.rule_stale',campus_error:'campus.sync_failed',snapshot_failed:'campus.sync_failed',snapshot_saved:'campus.confirmed',campus_candidates:'campus.needs_confirmation'};const s=campusEventStatus||(runtime[campusData?.runtime_status]?tr(runtime[campusData.runtime_status]):campusData?.latest?.quality)||'';if(s&&!['confirmed','auto_recognized','low_confidence','stale','unknown'].includes(s))return s;return s==='confirmed'?tr('campus.confirmed'):s==='auto_recognized'?tr('campus.auto_short'):s==='low_confidence'?tr('campus.needs_confirmation'):s==='stale'?tr('campus.rule_stale'):s==='unknown'?tr('campus.unknown'):tr('campus.never_synced');}
function campusUnitFactor(unit,radix=campusRadix()){return ({B:1,KB:radix,MB:radix**2,GB:radix**3,TB:radix**4,KiB:1024,MiB:1024**2,GiB:1024**3,TiB:1024**4,CNY:1})[unit]||1;}
function campusPriceFactor(cfg=state.config||{}){return cfg.campus_price_basis==='school'?campusRadix()**3:cfg.campus_price_unit==='GB'?1e9:GiB;}
function campusReadings(){
 const latest=campusData?.latest||{},measurement=latest.measurement;if(measurement)return measurement.raw_readings||{};
 const values=latest.details?.web_values||{},out={};
 for(const [field,unit]of Object.entries(latest.details?.web_units||{}))if(['B','KB','MB','GB','TB','KiB','MiB','GiB','TiB'].includes(unit)&&Number.isFinite(values[field]??latest[field]))out[field]={value:(values[field]??latest[field])/campusUnitFactor(unit,1000),unit};
 return out;
}
function campusPeriod(mapping,when=new Date()){
 if(!Number.isFinite(when.getTime())||!['monthly','yearly'].includes(mapping?.period)||!/^\d{4}-\d{2}-\d{2}$/.test(mapping.start_date||''))return '';
 const [year,month,day]=mapping.start_date.split('-').map(Number),start=new Date(year,month-1,day);
 if(start.getFullYear()!==year||start.getMonth()!==month-1||start.getDate()!==day||when<start)return '';
 const boundary=(y,m)=>new Date(y,m,Math.min(day,new Date(y,m+1,0).getDate()));
 let date=mapping.period==='monthly'?boundary(when.getFullYear(),when.getMonth()):boundary(when.getFullYear(),month-1);
 if(when<date)date=mapping.period==='monthly'?boundary(when.getFullYear(),when.getMonth()-1):boundary(when.getFullYear()-1,month-1);
 return date.getFullYear()+'-'+String(date.getMonth()+1).padStart(2,'0')+'-'+String(date.getDate()).padStart(2,'0')+'/'+mapping.period;
}
function campusCycle(confirmed=false){
 const mapping=(!confirmed&&campusFieldState.billing_cycle)||campusData?.rule?.fields?.billing_cycle;
 if(mapping?.period)return campusPeriod(mapping);
 if(mapping?.mode==='manual'&&mapping.kind==='text'&&mapping.value)return String(mapping.value);
 return String(campusData?.latest?.cycle_text||'');
}
function campusWebValues(){const d=campusData?.latest?.details?.web_values||{},l=campusData?.latest||{},out={...d},raw=campusReadings();for(const [name,kind]of campusFields){if(out[name]==null&&l[name]!=null)out[name]=l[name];if(kind==='bytes')out[name]=raw[name]?Number(raw[name].value)*campusUnitFactor(raw[name].unit):null;}return out;}
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
 const saved=campusData?.rule?.fields||{},billing=(!confirmed&&campusFieldState.billing_cycle)||saved.billing_cycle;
 const cycle=campusCycle(confirmed);
 const resolve=field=>{
  if(field in resolved)return resolved[field];if(visiting.has(field))throw new Error(tr('campus.formula_cycle',{field}));
  visiting.add(field);const m=confirmed?(saved[field]||{mode:'disabled'}):campusMapping(field),mode=m.mode||(m.selector?'auto':'disabled');let value=null,source='unknown';
  try{
   if((mode==='auto'||mode==='selector')&&web[field]!=null){value=campusFieldKind[field]==='text'?String(web[field]):Number(web[field]);source='portal';}
   else if(mode==='manual'){value=campusFieldKind[field]==='text'?String(m.value??''):campusManualValue(m,cycle);source=campusManualExpired(m,cycle)?'cycle_reset':'manual';}
   else if(mode==='formula'){
    const builtin=['min(used_bytes,free_quota_bytes)','max(0,used_bytes-free_quota_bytes)'].includes(String(m.formula).replace(/\s/g,''));
    if(builtin&&!campusAccountingValid(m,cycle,billing))throw new Error(tr('campus.accounting_unknown'));
    value=campusFormula(m.formula,name=>resolve(name));source='formula';
   }
  }catch(e){errors.push({field,message:String(e.message||e)});value=null;source='error';}
  visiting.delete(field);
  resolved[field]=campusFieldKind[field]==='text'?(typeof value==='string'&&value?value:null):(Number.isFinite(value)&&value>=0?value:null);
  if(resolved[field]==null&&mode==='disabled')source='disabled';sources[field]=source;return resolved[field];
 };
 for(const [field]of campusFields)resolve(field);
 // Generic plan totals and remaining data are not necessarily free allowances.
 const freeQuota=resolved.free_quota_bytes,freeUsed=resolved.used_free_bytes;
 const cycleMatches=billing?.period||!billing||billing.mode!=='manual'||!campusData?.latest?.cycle_text||String(billing.value)===campusData.latest.cycle_text;
 const billingValid=Boolean(cycleMatches&&campusBillingCycleValid(cycle,billing));
 const calculatedFree=billingValid&&freeQuota!=null&&freeUsed!=null?Math.max(0,freeQuota-freeUsed):null;
 const active=confirmed?saved:campusFieldState;
 const mapping=field=>active[field]||saved[field];
 const formula=(field,text)=>mapping(field)?.mode==='formula'&&String(mapping(field).formula).replace(/\s/g,'')===text;
 const trusted=field=>resolved[field]!=null&&['portal','manual'].includes(sources[field]);
 const usedConfirmed=trusted('used_free_bytes')||formula('used_free_bytes','min(used_bytes,free_quota_bytes)')&&campusAccountingValid(mapping('used_free_bytes'),cycle,billing)&&trusted('used_bytes')&&trusted('free_quota_bytes');
 const freeFormula=(formula('free_remaining_bytes','free_quota_bytes-used_free_bytes')||formula('free_remaining_bytes','max(0,free_quota_bytes-used_free_bytes)'))&&billingValid&&trusted('free_quota_bytes')&&usedConfirmed;
 const freeRemaining=trusted('free_remaining_bytes')||freeFormula?resolved.free_remaining_bytes:usedConfirmed&&trusted('free_quota_bytes')?calculatedFree:null;
 const freeRemainingSource=freeRemaining==null?'unknown':trusted('free_remaining_bytes')||freeFormula?sources.free_remaining_bytes:'calculated';
 const spendable=trusted('spendable_balance')||formula('spendable_balance','balance')&&trusted('balance');
 const price=Number(state.config?.campus_unit_price||0),priceBytes=campusPriceFactor(),funded=state.config?.campus_linear_tariff===true&&Number.isFinite(price)&&price>0&&spendable?resolved.spendable_balance/price*priceBytes:null;
 const total=billingValid&&freeRemaining!=null&&funded!=null?freeRemaining+funded:null;
 const quota=freeQuota,used=freeUsed;
 const coherent=quota>0&&used!=null&&used<=quota&&freeRemaining!=null&&Math.abs(Math.max(0,quota-used)-freeRemaining)<=Math.max(1,quota*1e-9);
 const progress=billingValid&&trusted('free_quota_bytes')&&usedConfirmed&&coherent?Math.max(0,Math.min(1,used/quota)):null;
 // Paid usage has already excluded the free allowance. Never charge all usage again.
 const paidConfirmed=trusted('paid_used_bytes')||formula('paid_used_bytes','max(0,used_bytes-free_quota_bytes)')&&campusAccountingValid(mapping('paid_used_bytes'),cycle,billing)&&trusted('used_bytes')&&trusted('free_quota_bytes');
 const estimated=state.config?.campus_linear_tariff!==true||!billingValid||!paidConfirmed||!Number.isFinite(price)||price<=0?null:resolved.paid_used_bytes/priceBytes*price;
 return {resolved,sources,freeRemaining,freeRemainingSource,funded,total,progress,estimated,errors,billingValid};
}
function campusBillingCycleValid(cycle,billing=campusFieldState.billing_cycle||campusData?.rule?.fields?.billing_cycle){
 if(!cycle)return false;const now=new Date(),month=now.getFullYear()+'-'+String(now.getMonth()+1).padStart(2,'0');
 const observed=campusData?.latest?.observed_ms;if(!observed)return false;const date=new Date(observed);
 if(billing?.period)return cycle===campusPeriod(billing,now)&&cycle===campusPeriod(billing,date);
 if(date.getFullYear()!==now.getFullYear()||date.getMonth()!==now.getMonth())return false;
 if(/^\d{4}-\d{2}$/.test(cycle))return cycle===month;
 return cycle===campusData?.latest?.cycle_text&&Boolean(campusData?.latest?.observed_ms)&&new Date(campusData.latest.observed_ms).toDateString()===now.toDateString();
}
function campusAccountingValid(mapping,cycle,billing=campusFieldState.billing_cycle||campusData?.rule?.fields?.billing_cycle){
 if(!mapping?.accounting_confirmed||!mapping.cycle||mapping.cycle!==cycle)return false;
 if(billing?.period)return campusBillingCycleValid(cycle,billing);
 const observed=campusData?.latest?.observed_ms;if(!observed||!mapping.confirmed_month)return false;
 const month=d=>d.getFullYear()+'-'+String(d.getMonth()+1).padStart(2,'0');
 return mapping.confirmed_month===month(new Date(observed))&&mapping.confirmed_month===month(new Date());
}
function campusLivePanel(compact=false){
 const c=campusResolve(),latest=campusData?.latest,cfg=state.config||{};
 const host=el('div',{class:'campus-live-host'});
 host.append(el('div',{class:'campus-live'},
  el('div',{},el('small',{},tr('campus.balance')),el('strong',{},campusMoney(c.resolved.balance)),el('small',{},tr('campus.source.'+c.sources.balance))),
  el('div',{},el('small',{},tr('campus.used')),el('strong',{},campusPortalBytes('used_bytes',c.resolved.used_bytes,c.sources.used_bytes)),el('small',{},tr('campus.source.'+c.sources.used_bytes)),c.sources.used_bytes==='portal'&&c.resolved.used_bytes!=null?el('small',{},campusConversion(c.resolved.used_bytes)):null),
  el('div',{},el('small',{},tr('campus.free_remaining')),el('strong',{},c.freeRemaining==null?tr('campus.unavailable'):campusPortalBytes('free_remaining_bytes',c.freeRemaining,c.freeRemainingSource)),el('small',{},tr('campus.source.'+c.freeRemainingSource))),
  el('div',{},el('small',{},tr('campus.funded_bytes')),el('strong',{},c.funded==null?tr('campus.unavailable'):campusBytes(c.funded)),el('small',{},c.funded!=null?tr('campus.calculated'):tr('campus.unavailable'))),
  el('div',{},el('small',{},tr('campus.total_available')),el('strong',{},c.total==null?tr('campus.unavailable'):campusBytes(c.total)),el('small',{},c.total!=null?tr('campus.calculated'):tr('campus.unavailable')))));
 if(!compact){
  host.append(el('div',{class:'campus-quality'},el('span',{class:'quality-badge'},campusQuality()),el('span',{},tr('campus.last_sync')+' '+campusLatestText()),el('span',{},tr('campus.data_source')+' '+(campusData?.latest?.portal_url||'—')),
   el('span',{},tr('campus.portal_fee')+' '+(c.resolved.fee==null?'—':campusMoney(c.resolved.fee))+' · '+tr('campus.source.'+c.sources.fee)),
   el('span',{},tr('campus.estimated_fee')+' '+(c.estimated==null?'—':campusMoney(c.estimated))),
   el('span',{},tr('campus.paid_used')+' '+campusPortalBytes('paid_used_bytes',c.resolved.paid_used_bytes,c.sources.paid_used_bytes)+' · '+tr('campus.source.'+c.sources.paid_used_bytes))));
  host.append(el('p',{class:'muted campus-unit-note'},campusUnitNote()));
  if(c.progress!=null){const bar=el('i');bar.style.width=(c.progress*100)+'%';host.append(el('div',{class:'campus-progress','aria-label':tr('campus.progress')},bar,el('span',{},(c.progress*100).toFixed(2)+'%')));}
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
 const free=c.freeRemaining;
 const spendable=raw('spendable_balance')||formula('spendable_balance','balance')&&raw('balance');
 const price=Number(cfg.campus_unit_price),funded=spendable&&Number.isFinite(price)&&price>0&&
  (cfg.campus_price_unit==null||['GB','GiB'].includes(cfg.campus_price_unit))&&Number.isFinite(c.funded)?c.funded:null;
 const total=c.billingValid&&free!=null&&funded!=null&&Number.isFinite(free+funded)?free+funded:null;
 const values={balance:raw('balance')?c.resolved.balance:null,used:raw('used_bytes')?c.resolved.used_bytes:null,free,funded,total};
 const runtime=campusLastEventType||campusData?.runtime_status;
 let status=!cfg.campus_enabled?'disabled':runtime==='campus_auth_required'?'auth':
  campusData?.error||campusData?.rule_error||['campus_error','snapshot_failed'].includes(runtime)?'error':
  runtime==='campus_rule_stale'?'stale':campusData?.latest?.measurement?.status==='needs_confirmation'?'unconfirmed':
  campusData?.latest&&!c.billingValid?'billing':Object.values(values).every(v=>v==null)?'empty':
  Object.values(values).some(v=>v==null)?'partial':'ready';
 const percent=!['disabled','auth','error','stale','unconfirmed','billing'].includes(status)?c.progress:null;
 // The track is exclusively the free allowance: consumed + remaining. Paid
 // purchasing capacity and total observed traffic have different denominators.
 const quota=c.resolved.free_quota_bytes,consumed=c.resolved.used_free_bytes;
 const coherent=quota>0&&consumed!=null&&free!=null&&Math.abs(Math.max(0,quota-consumed)-free)<=Math.max(1,quota*1e-9);
 const segments=percent!=null&&coherent?
  [{key:'used',value:Math.min(consumed,quota)},{key:'free',value:free}].filter(s=>s.value>0):[];
 const sum=segments.reduce((sum,item)=>sum+item.value,0);
 if(!Number.isFinite(sum))segments.length=0;
 return {values,status,percent,segments,sum,freeUsed:consumed,radix:campusRadix(),raw:campusReadings(),usedSource:c.sources.used_bytes,units:campusData?.latest?.details?.web_units||{},freeSource:raw('free_remaining_bytes')?c.sources.free_remaining_bytes:'calculated',balanceSource:c.sources.balance,
  syncing:runtime==='campus_syncing',observed:campusData?.latest?.observed_ms||null,
  source:campusData?.latest?.portal_url||campusData?.portal_url||cfg.campus_portal_url||''};
}
function campusOverviewContent(host,m){
 const detailsOpen=host.querySelector?.('.campus-overview-details')?.open||false;
 const value=(key,money=false)=>key==='free_used'?(m.freeUsed==null?tr('campus.unavailable'):campusBytes(m.freeUsed)):m.values[key]==null?tr('campus.unavailable'):money?campusMoney(m.values[key]):
  key==='used'?campusPortalBytes('used_bytes',m.values[key],m.usedSource):key==='free'?campusPortalBytes('free_remaining_bytes',m.values[key],m.freeSource):campusBytes(m.values[key]);
 const statusKey={disabled:'campus.card.disabled',auth:'campus.auth_required',error:'campus.sync_failed',stale:'campus.rule_stale',unconfirmed:'campus.needs_confirmation',billing:'campus.accounting_unknown',empty:'campus.card.empty',partial:'campus.card.partial',ready:'campus.card.ready'}[m.status];
 const header=el('header',{class:'campus-overview-header'},el('div',{class:'campus-overview-heading'},metricIcon('network'),el('h3',{},tr('campus.title')),
  el('span',{class:'campus-sync-state',role:'status',title:tr(statusKey)},tr(m.syncing&&m.status!=='disabled'?'campus.syncing':m.status==='billing'?'campus.accounting_pending':statusKey))),
  el('span',{class:'campus-sync-time'},m.observed&&m.status!=='disabled'?tr('campus.card.synced',{time:new Date(m.observed).toLocaleString(PulseI18n.locale)}):tr('campus.never_synced')));
 host.dataset.status=m.status;host.replaceChildren(header);
 if(m.status==='disabled'){
  host.append(el('div',{class:'campus-disabled'},el('p',{},tr('campus.card.disabled_hint')),
   el('button',{class:'secondary',onclick:()=>navigate('settings')},tr('campus.card.configure'))));return;
 }
 const metric=(key,title,kind,source)=>el('div',{class:'campus-metric campus-metric-'+key,title:tr(source)},
  el('div',{class:'campus-metric-label'},metricIcon(kind),el('span',{},tr(title))),
  el('strong',{'data-campus-value':key,class:m.values[key]==null?'campus-value-missing':''},value(key,key==='balance')),
  key==='total'&&m.values[key]!=null?el('small',{},tr('campus.card.estimate')):null);
 const metrics=el('div',{class:'campus-metrics'},metric('balance','campus.balance','wallet',m.balanceSource==='portal'?'campus.card.portal':'campus.source.manual'),
  metric('free','campus.free_remaining','network','campus.source.'+m.freeSource),metric('total','campus.total_available','protocol','campus.card.estimate'));
 const legendNames={used:'campus.used',free_used:'campus.field.used_free_bytes',free:'campus.free_remaining',funded:'campus.card.funded'};
 const chart=el('section',{class:'campus-usage'},el('div',{class:'campus-usage-heading'},el('h4',{},tr('campus.card.usage')),
  m.percent==null?null:el('span',{class:'campus-free-percent'},tr('campus.card.free_percent',{percent:(m.percent*100).toFixed(2)}))));
 const track=el('div',{class:'campus-segment-track',role:'img','aria-label':tr('campus.free_track')+'; '+['free_used','free'].map(key=>tr(legendNames[key])+': '+value(key)).join('; ')});
 for(const item of m.segments){
  const segment=el('i',{class:'campus-segment campus-tone-'+item.key,'aria-hidden':'true'});
  // CSSOM properties work under the desktop CSP; style attributes are blocked.
  segment.style.width=(item.value/m.sum*100)+'%';track.append(segment);
 }
 track.title=tr('campus.free_track');track.tabIndex=0;
 chart.append(track,...(m.segments.length?[]:[el('p',{class:'campus-chart-caption'},tr('campus.progress_unknown'))]),
  el('div',{class:'campus-legend'},...Object.entries(legendNames).map(([key,label])=>el('div',{'data-campus-part':key},
   el('span',{},key==='used'?null:el('i',{class:'campus-swatch campus-tone-'+(key==='free_used'?'used':key),'aria-hidden':'true'}),tr(label)),el('strong',{},value(key))))),
  el('div',{class:'campus-available-total',title:tr('campus.card.estimate')},el('span',{},tr('campus.total_available')),el('strong',{},value('total'))));
 host.append(el('div',{class:'campus-overview-body'},metrics,chart));
 if(['auth','error','stale'].includes(m.status))host.append(el('p',{class:'campus-overview-notice'},tr(Object.values(m.values).some(v=>v!=null)?'campus.card.retained':'campus.card.empty_hint')));
 else if(['empty','partial','billing','unconfirmed'].includes(m.status))host.append(el('p',{class:'campus-overview-notice'},tr(m.status==='empty'?'campus.card.empty_hint':'campus.card.partial_hint')));
 const details=el('details',{class:'campus-overview-details'},el('summary',{},tr('ui.details')+' · '+tr('campus.radix_short',{radix:m.radix})),
  el('p',{class:'campus-conversion'},campusConversion(m.values.used)),el('p',{},campusUnitNote()),
  el('p',{},tr('campus.free_track')),el('p',{},tr('campus.card.total_basis')),el('p',{},tr(statusKey)),
  el('footer',{class:'campus-overview-source'},tr('campus.data_source')+' · '+(m.source||'—')));
 details.open=detailsOpen;host.append(details);
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
  if(!campusPrimaryFields.includes(field)||!items.length||campusFieldState[field]||campusData?.rule?.fields?.[field])continue;
  const item=items[0];if(item.value==null||!item.selector)continue;
  campusFieldState[field]={mode:'auto',selector:item.selector,kind:campusFieldKind[field],unit:item.unit||'',confidence:Number(item.confidence||0),...(item.scope?{scope:item.scope}:{})};
 }
}
function campusCandidateSelect(field,current){
 const items=campusCandidates[field]||[],select=el('select',{'aria-label':tr('campus.field.'+field)});
 select.append(el('option',{value:''},current.selector?tr('campus.selector_saved'):tr('campus.not_mapped')));
 items.forEach((item,index)=>select.append(el('option',{value:String(index)},`${item.raw||item.value||''} · ${Math.round(Number(item.confidence||0)*100)}%`)));
 const matched=items.findIndex(item=>item.selector===current.selector);if(matched>=0)select.value=String(matched);
 select.onchange=()=>{if(select.value==='')return;const item=items[Number(select.value)];if(item?.value==null)return;campusFieldState[field]={mode:'selector',selector:item.selector,kind:campusFieldKind[field],unit:item.unit||'',confidence:Number(item.confidence||0),...(item.scope?{scope:item.scope}:{})};};
 return select;
}
function campusPickerFeedback(){document.querySelectorAll('.campus-pick-feedback').forEach(node=>node.textContent=node.dataset.field===campusPickerField?campusPickerMessage:'');}
function campusComputedPanel(){
 // Draft controls may survive a failed save. Never present their calculations
 // as persisted results while Dashboard/Network still use the confirmed rule.
 const c=campusResolve(true),host=el('div',{class:'campus-live'}),current=!['disabled','auth','error','stale','unconfirmed','billing'].includes(campusOverviewModel().status);
 for(const [key,value,money]of [['campus.free_remaining',c.freeRemaining],['campus.funded_bytes',c.funded],['campus.total_available',c.total],['campus.estimated_fee',c.estimated,true]])
  host.append(el('div',{title:tr(value==null?'campus.rules_required':'campus.calculated')},el('small',{},tr(key)),el('strong',{},value==null?tr('campus.unavailable'):money?campusMoney(value):campusBytes(value)),key==='campus.free_remaining'||value==null?null:el('small',{},tr('campus.card.estimate'))));
 const section=el('div',{},host);
 if(current&&c.progress!=null){const bar=el('i');bar.style.width=(c.progress*100)+'%';section.append(el('div',{class:'campus-progress','aria-label':tr('campus.progress')},bar,el('span',{},(c.progress*100).toFixed(2)+'%')));}
 else section.append(el('p',{class:'muted'},tr('campus.progress_unknown')));
 return section;
}
function renderCampusSettings(host){
 campusRenderHost=host;host.classList.add('campus-settings');const cfg=state.config||{};
 if(campusStateOrigin!==campusData?.origin){campusFieldState=structuredClone(campusData?.rule?.fields||{});campusStateOrigin=campusData?.origin||'';}
 campusSuggest();
 const input=(type,value,attrs={})=>el('input',{type,value,...attrs});
 const enabled=input('checkbox','',{checked:Boolean(cfg.campus_enabled)}),url=input('url',cfg.campus_portal_url||'',{placeholder:'https://portal.example.edu.cn',maxlength:2048});
 const refresh=input('number',cfg.campus_refresh_minutes||5,{min:5,max:1440}),price=input('number',cfg.campus_unit_price||0,{min:0,max:1000,step:'any'});
 const radix=el('select',{'aria-label':tr('campus.radix')},...[1024,1000].map(n=>el('option',{value:String(n)},tr('campus.radix_option',{radix:n}))));radix.value=String(campusRadix());
 const priceUnit=el('span',{},tr('campus.yuan_per_gb'));
 price.value=Number(cfg.campus_unit_price||0)*campusRadix()**3/campusPriceFactor(cfg);
 let priceEdited=false;
 const linear=input('checkbox','',{checked:cfg.campus_linear_tariff===true});
 const origins=el('textarea',{rows:2,placeholder:'https://cas.example.edu.cn'});origins.value=(cfg.campus_allowed_origins||[]).join('\n');
 const feedback=el('p',{class:'campus-feedback muted',role:'status'},campusEventStatus),rulesHost=el('div',{class:'campus-candidates'}),preview=el('p',{class:'muted campus-preview'}),webHost=el('div',{class:'campus-web-data'});
 const label=(key,...children)=>el('label',{},el('strong',{},tr(key)),...children);
 const configPayload=()=>({campus_enabled:enabled.checked,campus_portal_url:url.value.trim(),campus_allowed_origins:origins.value.split(/\r?\n/).map(v=>v.trim()).filter(Boolean),campus_refresh_minutes:Number(refresh.value),campus_traffic_radix:Number(radix.value),campus_linear_tariff:linear.checked,...(priceEdited?{campus_unit_price:Number(price.value),campus_price_unit:'GB',campus_price_basis:'school'}:{})});
 let saves=Promise.resolve();
 const persist=task=>{saves=saves.catch(()=>{}).then(async()=>{feedback.textContent=tr('campus.saving');try{await task();const next=await rpc('get_campus_status');if(campusData?.origin!==next.origin){campusFieldState={};campusCandidates={};campusStateOrigin='';}campusData=next;feedback.textContent=tr('campus.saved');PulseCampus.renderLive();}catch(e){feedback.textContent=String(e.message||e);throw e;}});return saves;};
 const saveBasic=()=>{const payload=configPayload();return persist(async()=>{if(!url.checkValidity()||!refresh.checkValidity()||priceEdited&&!price.checkValidity())throw Error(tr('campus.invalid_settings'));await rpc('set_config',payload);state=await rpc('get_status');});};
 const saveFields=(allowEmpty=false)=>{const fields=structuredClone(campusFieldState);
  // Billing edits must not implicitly confirm new page candidates.
  if(allowEmpty)for(const [field,m]of Object.entries(fields))if(m.mode==='auto'||m.mode==='selector'){
   const saved=campusData?.rule?.fields?.[field];if(saved)fields[field]=structuredClone(saved);else delete fields[field];
  }
  return persist(async()=>{if(!allowEmpty&&!Object.keys(fields).length)throw Error(tr('campus.no_selection'));await rpc('save_campus_mapping',{fields});try{await rpc('campus_rescan');}catch{/* Rule remains saved if the portal is closed. */}});};
 for(const control of [enabled,url,refresh,linear,origins])control.onchange=()=>saveBasic().catch(()=>{});
 price.onchange=()=>{priceEdited=true;return saveBasic().catch(()=>{});};
 const manualControls=[];
 radix.onchange=async()=>{try{await saveBasic();if(!priceEdited)price.value=Number(state.config?.campus_unit_price||0)*campusRadix()**3/campusPriceFactor();manualControls.forEach(update=>update());}catch{radix.value=String(campusRadix());}};
 const pickControl=field=>{
   const localStatus=el('small',{class:'campus-pick-feedback','data-field':field,role:'status'},field===campusPickerField?campusPickerMessage:'');
   const pick=el('button',{class:'secondary',onclick:async()=>{
    campusPickerField=field;campusPickerMessage=tr('campus.pick_opening');localStatus.textContent=campusPickerMessage;campusPickerFeedback();pick.disabled=true;
    try{await saves;await rpc('campus_pick_field',{field});}catch(e){campusPickerMessage=String(e.message||e);localStatus.textContent=campusPickerMessage;feedback.textContent=campusPickerMessage;}
    finally{pick.disabled=false;campusPickerFeedback();}
   }},tr('campus.reselect'));
   return [pick,localStatus];
 };
 const webRows={};
 for(const field of campusPrimaryFields){
  const title=el('strong',{},tr('campus.field.'+field)),value=el('span',{class:'campus-web-reading'});
  webRows[field]={title,value};webHost.append(el('div',{class:'campus-web-row'},title,value,...pickControl(field)));
 }
 const sync=el('p',{class:'campus-quality',role:'status'});webHost.append(sync);
 webHost.redraw=()=>{
  const c=campusResolve(),recognized=[];
  for(const field of campusPrimaryFields){const mapping=campusMapping(field),item=(campusCandidates[field]||[]).find(candidate=>candidate.selector===mapping.selector);
   const setText=(node,text)=>{if(node.textContent!==text)node.textContent=text;};
   setText(webRows[field].title,tr(field==='used_bytes'&&(mapping.scope||item?.scope)==='ipv4'?'campus.ipv4_used':'campus.field.'+field));
   const saved=campusData?.rule?.fields?.[field],pending=item&&(!saved||saved.selector!==mapping.selector);
   setText(webRows[field].value,pending?String(item.raw||item.value)+' · '+tr('campus.needs_confirmation'):c.resolved[field]==null?tr('campus.pending_reading'):field==='balance'?campusMoney(c.resolved[field]):campusBytes(c.resolved[field]));
   if(pending)recognized.push(tr('campus.field.'+field));
  }
  const status=campusQuality()+(campusData?.latest?.observed_ms?' · '+tr('campus.last_sync')+' '+campusLatestText():'');if(sync.textContent!==status)sync.textContent=status;
  preview.textContent=recognized.length?tr('campus.review_hint')+' '+recognized.join(' · '):campusData?.rule?tr('campus.auto_saved'):tr('campus.login_hint');
 };
 const extended=new Set();
 const drawRules=()=>{
  rulesHost.replaceChildren();campusSuggest();
  // Only available portal candidates or existing selectors belong in this list.
  const fields=campusFields.filter(([field])=>campusPrimaryFields.includes(field)||extended.has(field)||campusCandidates[field]?.length||['auto','selector'].includes(campusMapping(field).mode));
  for(const [field]of fields){const current=campusMapping(field);
   rulesHost.append(el('div',{class:'campus-candidate-row'},el('strong',{},tr('campus.field.'+field)),campusCandidateSelect(field,current),...pickControl(field)));
  }
  webHost.redraw();
 };
 rulesHost.redraw=drawRules;drawRules();
 const manualParam=field=>{const mapping=campusMapping(field),manual=mapping.mode==='manual';
  const value=input('number',manual?Number(mapping.value)*campusUnitFactor(mapping.unit)/campusRadix()**3:'',{min:0,step:'any',placeholder:tr('campus.optional')});
  manualControls.push(()=>{const m=campusMapping(field);value.value=m.mode==='manual'?Number(m.value)*campusUnitFactor(m.unit)/campusRadix()**3:'';});
  value.onchange=()=>{
   if(!value.checkValidity())return;
   if(!campusCycle()&&period.value!=='legacy'){const m={mode:'manual',kind:'text',unit:'text',period:period.value,start_date:start.value},key=campusPeriod(m);if(key)campusFieldState.billing_cycle={...m,value:key};}
   const cycle=campusCycle();if(field==='used_free_bytes'&&value.value!==''&&!cycle){feedback.textContent=tr('campus.accounting_unknown');return;}
   if(value.value===''){if(campusFieldState[field]?.mode==='manual')delete campusFieldState[field];}
   else campusFieldState[field]={mode:'manual',kind:'bytes',value:Number(value.value),unit:'GB',traffic_radix:campusRadix(),reset_on_cycle:field==='used_free_bytes',...(field==='used_free_bytes'?{cycle}:{})};
   if(field==='used_free_bytes'){freeFirst.checked=false;if(campusFieldState.paid_used_bytes?.accounting_confirmed)delete campusFieldState.paid_used_bytes;}
   return saveFields(true).catch(()=>{});
  };
  return label('campus.field.'+field,el('div',{class:'campus-param'},value,el('span',{},'GB')));
 };
 const billingMapping=campusMapping('billing_cycle'),period=el('select',{'aria-label':tr('campus.period')},...['monthly','yearly',...(!billingMapping.period&&billingMapping.mode!=='disabled'?['legacy']:[])].map(p=>el('option',{value:p},tr('campus.period.'+p))));
 period.value=billingMapping.period|| (billingMapping.mode!=='disabled'?'legacy':'monthly');
 const now=new Date(),defaultStart=now.getFullYear()+'-'+String(now.getMonth()+1).padStart(2,'0')+'-01';
 const start=input('date',billingMapping.start_date||defaultStart,{required:true});
 const legacyCycle=input('text',billingMapping.mode==='manual'?billingMapping.value:'',{maxlength:128});
 const setPeriod=()=>{
  if(period.value==='legacy'){campusFieldState.billing_cycle=structuredClone(billingMapping);freeFirst.checked=campusAccountingValid(campusMapping('used_free_bytes'),campusCycle());return saveFields(true).catch(()=>{});}
  if(!start.checkValidity()||!start.value)return;
  const mapping={mode:'manual',kind:'text',unit:'text',period:period.value,start_date:start.value};
  const cycle=campusPeriod(mapping);if(!cycle){feedback.textContent=tr('campus.invalid_settings');return;}
  mapping.value=cycle;campusFieldState.billing_cycle=mapping;
  freeFirst.checked=campusAccountingValid(campusMapping('used_free_bytes'),cycle,mapping);
  return saveFields(true).catch(()=>{});
 };
 period.onchange=()=>{start.disabled=period.value==='legacy';legacyLabel.hidden=period.value!=='legacy';return setPeriod();};start.disabled=period.value==='legacy';start.onchange=setPeriod;
 const legacyLabel=label('campus.field.billing_cycle',legacyCycle);legacyLabel.hidden=period.value!=='legacy';
 legacyCycle.onchange=()=>{if(legacyCycle.value)campusFieldState.billing_cycle={mode:'manual',kind:'text',unit:'text',value:legacyCycle.value};else delete campusFieldState.billing_cycle;return saveFields(true).catch(()=>{});};
 const balance=el('input',{type:'checkbox',checked:campusFieldState.spendable_balance?.mode==='formula'&&campusFieldState.spendable_balance?.formula==='balance'});
 balance.onchange=()=>{if(balance.checked)campusFieldState.spendable_balance={mode:'formula',kind:'currency',formula:'balance'};else delete campusFieldState.spendable_balance;saveFields(true).catch(()=>{});};
 const freeFirst=el('input',{type:'checkbox',checked:campusAccountingValid(campusFieldState.used_free_bytes,campusCycle())});
 freeFirst.onchange=()=>{if(freeFirst.checked){
  if(!campusCycle()&&period.value!=='legacy'){const m={mode:'manual',kind:'text',unit:'text',period:period.value,start_date:start.value};const key=campusPeriod(m);if(key)campusFieldState.billing_cycle={...m,value:key};}
  const cycle=campusCycle();if(!cycle||!campusBillingCycleValid(cycle)){freeFirst.checked=false;feedback.textContent=tr('campus.accounting_unknown');return;}
  const confirmedAt=new Date(),confirmation={accounting_confirmed:true,cycle,confirmed_month:confirmedAt.getFullYear()+'-'+String(confirmedAt.getMonth()+1).padStart(2,'0')};campusFieldState.used_free_bytes={mode:'formula',kind:'bytes',formula:'min(used_bytes, free_quota_bytes)',...confirmation};campusFieldState.paid_used_bytes={mode:'formula',kind:'bytes',formula:'max(0, used_bytes - free_quota_bytes)',...confirmation};
 }else{for(const field of ['used_free_bytes','paid_used_bytes'])if(campusFieldState[field]?.accounting_confirmed)delete campusFieldState[field];}return saveFields(true).then(()=>{manualControls.forEach(update=>update());}).catch(()=>{});};
 const basic=el('section',{class:'campus-section'},el('h4',{},tr('campus.web_data')),el('label',{class:'checkbox'},enabled,tr('campus.enable')),el('div',{class:'settings-grid'},label('campus.url',url),label('campus.refresh',refresh)),el('div',{class:'actions'},el('button',{onclick:async()=>{try{await saveBasic();await rpc('open_campus_portal');feedback.textContent=tr('campus.login_hint');}catch(e){feedback.textContent=String(e.message||e);}}},tr('campus.open'))),webHost,preview,el('div',{class:'actions'},el('button',{onclick:()=>saveFields().catch(()=>{})},tr('campus.confirm_results'))));
 const billing=el('section',{class:'campus-section'},el('h4',{},tr('campus.manual_config')),el('div',{class:'settings-grid'},manualParam('free_quota_bytes'),label('campus.price',el('div',{class:'campus-param'},price,priceUnit)),label('campus.period',period),label('campus.period_start',start),label('campus.radix',radix)),el('label',{class:'checkbox'},freeFirst,tr('campus.free_first')),el('label',{class:'checkbox'},linear,tr('campus.linear_tariff')),el('label',{class:'checkbox'},balance,tr('campus.balance_for_data')),el('details',{},el('summary',{},tr('campus.manual_free_used')),el('p',{class:'muted'},tr('campus.manual_free_hint')),manualParam('used_free_bytes')),help(tr('ui.details'),el('p',{},campusUnitNote()),el('p',{},tr('campus.billing_hint'))),...(cfg.campus_price_basis==='legacy_bytes'?[el('p',{class:'muted'},tr('campus.legacy_price',{value:cfg.campus_unit_price,unit:cfg.campus_price_unit}))]:[]));
 if(billingMapping.mode==='manual'&&!billingMapping.period)billing.append(legacyLabel);
 const computed=el('section',{class:'campus-section'},el('h4',{},tr('campus.auto_calculate')),el('div',{class:'campus-computed-target'},campusComputedPanel()));
 const extension=el('select',{'aria-label':tr('campus.extend_field')},el('option',{value:''},tr('campus.extend_field')),...campusFields.filter(([field])=>!campusPrimaryFields.includes(field)).map(([field])=>el('option',{value:field},tr('campus.field.'+field))));
 extension.onchange=()=>{if(campusFieldKind[extension.value]){extended.add(extension.value);drawRules();extension.value='';}};
 const advanced=el('details',{class:'campus-section'},el('summary',{},tr('campus.advanced')),label('campus.allowed',origins,el('small',{},tr('campus.allowed_hint'))),rulesHost,el('p',{class:'muted'},tr('campus.privacy')),el('div',{class:'actions'},el('button',{class:'secondary',onclick:async()=>{try{await rpc('campus_rescan');feedback.textContent=tr('campus.rescanning');}catch(e){feedback.textContent=String(e.message||e);}}},tr('campus.rescan')),el('button',{class:'danger',onclick:async()=>{try{await rpc('delete_campus_rule');campusFieldState={};campusCandidates={};campusData=await rpc('get_campus_status');drawRules();PulseCampus.renderLive();}catch(e){feedback.textContent=String(e.message||e);}}},tr('campus.delete_rule'))));
 advanced.append(label('campus.extend_field',extension));
 basic.append(el('p',{class:'muted'},tr('campus.background_hint')));host.append(basic,billing,computed,advanced,feedback);
}
const PulseCampus={
 async refresh(){try{const next=await rpc('get_campus_status');if(campusData?.origin!==next.origin){campusFieldState={};campusCandidates={};campusStateOrigin='';campusLastEventType='';}campusData=next;if(next.runtime_status){campusEventStatus='';campusLastEventType=next.runtime_status;}}catch(e){campusData={...(campusData||{}),error:String(e.message||e)};}this.renderLive();return campusData;},
 renderSettings:renderCampusSettings,dashboardCard:campusDashboardCard,networkPanel:campusNetworkPanel,livePanel:campusLivePanel,
 renderLive(){document.querySelectorAll('.campus-web-data').forEach(node=>node.redraw?.());for(const selector of ['.campus-live-target','.campus-computed-target'])document.querySelectorAll(selector).forEach(node=>{const computed=selector==='.campus-computed-target',key=JSON.stringify([campusResolve(computed),campusOverviewModel().status,campusQuality(),campusLatestText(),PulseI18n.locale,campusRadix()]);if(node._campusKey!==key){node.replaceChildren(computed?campusComputedPanel():campusLivePanel(false));node._campusKey=key;}});const hosts=document.querySelectorAll('.campus-overview');if(hosts.length){const m=campusOverviewModel();hosts.forEach(node=>campusUpdateOverview(node,m));}},
 ingest(event){
  if(!event||!event.type||event.portal_origin&&event.portal_origin!==campusData?.origin)return;campusEventStatus='';
  if(['campus_picker_pending','campus_picker_ready','campus_picker_failed','campus_picker_cancelled'].includes(event.type)){
   campusPickerField=event.field||campusPickerField;
   campusPickerMessage=tr(event.type==='campus_picker_ready'?'campus.pick_hint':event.type==='campus_picker_failed'?'campus.pick_failed':event.type==='campus_picker_cancelled'?'campus.pick_cancelled':event.auth_required?'campus.pick_login':'campus.pick_opening',{field:tr('campus.field.'+campusPickerField)});
   campusPickerFeedback();return;
  }
  campusLastEventType=event.type;
  if(event.type==='campus_candidates'){campusCandidates={};for(const [field,items]of Object.entries(event.candidates||{}))if(campusFieldKind[field]&&Array.isArray(items))campusCandidates[field]=items;campusSuggest();if(!campusData?.rule)campusEventStatus='low_confidence';}
  else if(event.type==='campus_candidate'&&event.field&&event.candidate){const items=campusCandidates[event.field]||[];items.unshift(event.candidate);campusCandidates[event.field]=items.slice(0,5);campusFieldState[event.field]={mode:'selector',selector:event.candidate.selector,kind:event.candidate.kind||campusFieldKind[event.field],unit:event.candidate.unit||'',confidence:Number(event.candidate.confidence||0),...(event.candidate.scope?{scope:event.candidate.scope}:{})};campusEventStatus=tr('campus.manual_candidate');campusPickerMessage=campusEventStatus;}
  else if(event.type==='campus_syncing')campusEventStatus=tr('campus.syncing');
  else if(event.type==='campus_auth_required')campusEventStatus=tr('campus.auth_required');
  else if(event.type==='campus_rule_stale')campusEventStatus='stale';
  else if(event.type==='snapshot_saved'&&event.snapshot){campusData=campusData||{};campusData.latest=event.snapshot;campusEventStatus='confirmed';}
  else if(event.type==='snapshot_failed'||event.type==='campus_error')campusEventStatus=event.error||event.message||tr('campus.sync_failed');
  if(page==='settings'){document.querySelector('.campus-candidates')?.redraw?.();}
  this.renderLive();
 }
};
