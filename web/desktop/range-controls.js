'use strict';
let rangeRoot=null,rangeDraft=null,rangeDirty=false,rangePreset='all',appliedRangePreset='all',rangeFields=null;
const rangeZonePreference='pulsetrace.range.zone';
const rangeZones=['local','UTC'];
function newStatisticsRange(){
 return {from:0,to:Math.floor(Date.now()/60000)+1,bucket:1440,time_zone:PulseDate.preference(rangeZonePreference,'local',rangeZones)};
}
function rangeControls(){
 if(rangeRoot)return rangeRoot;
 if(!rangeDraft)rangeDraft={...range};
 const from=el('button',{type:'button',class:'datetime-trigger','aria-label':tr('ui.6a9906c79f26')});
 const to=el('button',{type:'button',class:'datetime-trigger','aria-label':tr('ui.fce8bb894bc1')});
 const bucket=el('select',{'aria-label':tr('ui.7546c5ba1810')},...[['1','ui.bd957bc497aa'],['60','ui.3b6fefc50feb'],['1440','range.day']].map(([value,label])=>el('option',{value},tr(label))));
 const zone=el('select',{'aria-label':tr('range.zone')},el('option',{value:'local'},tr('range.local')),el('option',{value:'UTC'},tr('range.utc')));
 const draftZone=()=>rangeDraft.time_zone;
 const apply=()=>{
  if(rangeDraft.from<0||rangeDraft.to<=rangeDraft.from||rangePreset!=='all'&&rangeDraft.to-rangeDraft.from>527040){
   error(new Error(tr('ui.d400e986a59f')));return;
  }
  range={...rangeDraft};appliedRangePreset=rangePreset;rangeDirty=false;
  PulseDate.savePreference(rangeZonePreference,range.time_zone);
  labels();viewGeneration++;renderKey='';refresh(true);
 };
 const selectPreset=value=>{
  rangePreset=value;rangeDirty=true;
  const end=Math.floor(Date.now()/60000)+1;
  const first=PulseDate.presetStart(value,end,draftZone());
  rangeDraft={...rangeDraft,from:first,to:end,bucket:PulseRange.bucket(end-(value==='all'?historyFirst:first))};
  apply();
 };
 const shortcuts=[
  ['today','range.today'],['60','ui.b3aaa02ea7f3'],['1440','ui.ab2c18802e55'],['10080','range.last7'],
  ['week','usage.this_week'],['month','range.month'],['all','range.all_short']
 ].map(([value,label])=>[value,el('button',{type:'button',class:'secondary',
  'data-range-preset':value,onclick:()=>selectPreset(value)},tr(label))]);
 function labels(){
  from.textContent=rangePreset==='all'?tr('range.all'):PulseDate.label(rangeDraft.from,draftZone());
  to.textContent=PulseDate.label(rangeDraft.to,draftZone());
  for(const [value,button] of shortcuts)button.setAttribute('aria-pressed',String(!rangeDirty&&rangePreset===value));
  bucket.value=String(rangeDraft.bucket);bucket.disabled=rangePreset==='all';
  zone.value=draftZone();
 }
 const edit=(key,value)=>{
  if(rangePreset==='all')rangeDraft.from=historyFirst;
  rangeDraft[key]=value;rangeDirty=true;rangePreset='custom';labels();
 };
 new PulseDatePicker.Picker(from,()=>rangePreset==='all'?historyFirst:rangeDraft.from,value=>edit('from',value),draftZone);
 new PulseDatePicker.Picker(to,()=>rangeDraft.to,value=>edit('to',value),draftZone);
 bucket.onchange=()=>{rangeDraft.bucket=Number(bucket.value);rangeDirty=true;labels();};
 zone.onchange=()=>{
  rangeDraft.time_zone=zone.value;
  if(rangePreset!=='custom')rangeDraft.from=PulseDate.presetStart(rangePreset,rangeDraft.to,draftZone());
  apply();
 };
 const query=el('button',{type:'button',class:'secondary',onclick:apply},tr('ui.bcd6771e08ec'));
 const quick=el('div',{class:'range-shortcuts',role:'group','aria-label':tr('range.shortcuts')},shortcuts.map(([,button])=>button));
 const controls=el('div',{class:'range'},quick,from,el('span',{},tr('ui.65edbf9f1c67')),to,bucket,zone,query);
 rangeRoot=el('details',{class:'range-shell'},el('summary',{},tr('ui.941251c489ee')),controls);
 rangeRoot.open=filterExpanded;
 rangeRoot.addEventListener('toggle',()=>{filterExpanded=rangeRoot.open;});
 rangeFields={from,to,bucket,query,zone,shortcuts,labels};
 labels();return rangeRoot;
}
function advanceLiveRange(){
 if(rangeDirty||PulseDatePicker.current||rangePreset==='custom')return;
 const end=Math.floor(Date.now()/60000)+1;
 range={...range,from:PulseDate.presetStart(rangePreset,end,range.time_zone),to:end};
 rangeDraft={...range};rangeFields?.labels();
}
function mountRangeControls(){const host=$('range-host');host.hidden=page==='settings';if(page!=='settings'&&!rangeRoot?.isConnected)host.append(rangeControls());}
