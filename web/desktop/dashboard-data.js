'use strict';
globalThis.PulseDashboard=(()=>{
 const mouseKeys=['left','right','middle','mouse4','mouse5'];
 const total=(rows,module,metrics=null)=>rows.reduce((n,r)=>n+(r.module===module&&(!metrics||metrics.includes(r.metric))?Number(r.value):0),0);
 function series(rows,module,metrics=null){const map=new Map();for(const r of rows)if(r.module===module&&(!metrics||metrics.includes(r.metric)))map.set(Number(r.minute),(map.get(Number(r.minute))||0)+Number(r.value));return [...map].sort((a,b)=>a[0]-b[0]);}
 function inputs(rows,bucket=60,limit=96,aligned=false){
  if(!Number.isInteger(bucket)||bucket<1||!Number.isInteger(limit)||limit<2)throw new RangeError('Invalid chart aggregation');
  const all=rows.filter(r=>r.module==='keyboard'||r.module==='coverage'||r.module==='mouse'&&mouseKeys.includes(r.metric));
  if(!all.length)return [];
  let first=Infinity,last=-Infinity;
  for(const {minute} of all){first=Math.min(first,minute);last=Math.max(last,minute);}
  let step=Math.max(bucket,Math.ceil((last-first+bucket)/(limit*bucket))*bucket);
  while(!aligned&&Math.floor(last/step)-Math.floor(first/step)+1>limit)step+=bucket;
  const bins=new Map();
  for(const r of all){const m=aligned?(step===bucket?r.minute:Math.floor((r.minute-first)/step)):Math.floor(r.minute/step)*step;if(!bins.has(m))bins.set(m,{minute:aligned?r.minute:m,keyboard:null,mouse:null,coverage:0,step});const b=bins.get(m);
   if(aligned)b.minute=Math.min(b.minute,r.minute);
   if(r.module==='coverage')b.coverage+=Number(r.value);else b[r.module]=(b[r.module]||0)+Number(r.value);
  }
  return [...bins.values()].sort((a,b)=>a.minute-b.minute).map(b=>({...b,keyboard:b.keyboard??(b.coverage>0?0:null),mouse:b.mouse??(b.coverage>0?0:null)}));
 }
 function distribution(values){const sum=values.reduce((n,v)=>n+v,0);return sum>0?values.map(v=>v/sum):null;}
 function protocol(records){const valid=records.filter(r=>r.accepted>0),same=valid.every(r=>typeof r.source==='string'&&r.source.length>0&&typeof r.basis==='string'&&r.basis.length>0)&&new Set(valid.map(r=>JSON.stringify([r.source,r.basis]))).size<=1;
  const bytes=[0,0];if(same)for(const r of valid){bytes[0]+=Number(r.ipv4_up)+Number(r.ipv4_down);bytes[1]+=Number(r.ipv6_up)+Number(r.ipv6_down);}
  const has=valid.length>0&&same,quality=records.some(r=>r.quality==='incomplete_observation')?tr("ui.3f955ae5b843"):has?tr("ui.76a2e875d985"):tr("ui.497c85690c4c");
  return {has,bytes,ratio:has?distribution(bytes):null,quality:same?quality:tr("ui.013c99479178")};
 }
 function protocolRows(records,interfaceId){
  const selected=records.filter(r=>r.interface_id===interfaceId);
  if(!protocol(selected).has)return [];
  return selected.filter(r=>r.accepted>0).flatMap(r=>['ipv4_up','ipv4_down','ipv6_up','ipv6_down'].map(key=>({minute:r.minute,module:'network_ip',metric:interfaceId+'/'+key,value:Number(r[key])})));
 }
 function previousPeriod({from,to,time_zone},preset){
  const span=to-from;
  if(!from||span<=0||preset==='all')return null;
  let start=from-span,end=from;
  if(['today','week','month'].includes(preset)){
   start=PulseDate.calendarStart(from-1,preset,time_zone);
   const a=PulseDate.parts(from,time_zone),b=PulseDate.parts(to-1,time_zone);
   const days=(Date.UTC(b.year,b.month-1,b.day)-Date.UTC(a.year,a.month-1,a.day))/86400000;
   const prior=PulseDate.parts(start,time_zone);
   const fields=[prior.year,prior.month-1,prior.day+days,b.hour,b.minute];
   const last=(time_zone==='UTC'?Date.UTC(...fields):new Date(...fields).getTime())/60000;
   end=Math.min(from,last+1);
  }
  return start>=0&&end>start?{from:start,to:end,time_zone}:null;
 }
 function periodTotals(rows){
  const totals={keyboard:null,mouse:null,wheel:null,coverage:null,network:null};
  for(const {module,metric,value} of rows){
   let key;
   if(module==='keyboard')key='keyboard';
   else if(module==='mouse'&&mouseKeys.includes(metric))key='mouse';
   else if(module==='mouse'&&['wheel_up_events','wheel_down_events'].includes(metric))key='wheel';
   else if(module==='coverage'&&metric==='observed_ms')key='coverage';
   else if(module==='network'&&['interface_upload_bytes','interface_download_bytes'].includes(metric))key='network';
   if(key)totals[key]=(totals[key]??0)+Number(value);
  }
  if(totals.coverage>0)for(const key of ['keyboard','mouse','wheel'])totals[key]??=0;
  return totals;
 }
 const change=(current,previous)=>current!=null&&previous>0?(current-previous)/previous:null;
 function peakInput(rows){
  const minutes=new Map();
  let covered=false,kpm=0,apm=0;
  for(const {minute,module,metric,value} of rows){
   if(module==='coverage'&&metric==='observed_ms'&&value>0)covered=true;
   const keyboard=module==='keyboard';
   if(!keyboard&&!(module==='mouse'&&mouseKeys.includes(metric)))continue;
   if(!minutes.has(minute))minutes.set(minute,{keys:0,actions:0});
   const bin=minutes.get(minute),count=Number(value);
   if(keyboard)bin.keys+=count;
   bin.actions+=count;kpm=Math.max(kpm,bin.keys);apm=Math.max(apm,bin.actions);
  }
  return covered||minutes.size?{kpm,apm}:null;
 }
 return {mouseKeys,total,series,inputs,distribution,protocol,protocolRows,previousPeriod,periodTotals,change,peakInput};
})();
