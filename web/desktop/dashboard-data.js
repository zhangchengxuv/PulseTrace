'use strict';
globalThis.PulseDashboard=(()=>{
 const mouseKeys=['left','right','middle','mouse4','mouse5'];
 const total=(rows,module,metrics=null)=>rows.reduce((n,r)=>n+(r.module===module&&(!metrics||metrics.includes(r.metric))?Number(r.value):0),0);
 function series(rows,module,metrics=null){const map=new Map();for(const r of rows)if(r.module===module&&(!metrics||metrics.includes(r.metric)))map.set(Number(r.minute),(map.get(Number(r.minute))||0)+Number(r.value));return [...map].sort((a,b)=>a[0]-b[0]);}
 function inputs(rows,bucket=60,limit=96){
  if(!Number.isInteger(bucket)||bucket<1||!Number.isInteger(limit)||limit<2)throw new RangeError('Invalid chart aggregation');
  const all=rows.filter(r=>r.module==='keyboard'||r.module==='coverage'||r.module==='mouse'&&mouseKeys.includes(r.metric));
  if(!all.length)return [];
  const first=Math.min(...all.map(r=>r.minute)),last=Math.max(...all.map(r=>r.minute));
  let step=Math.max(bucket,Math.ceil((last-first+bucket)/(limit*bucket))*bucket);
  while(Math.floor(last/step)-Math.floor(first/step)+1>limit)step+=bucket;
  const bins=new Map();
  for(const r of all){const m=Math.floor(r.minute/step)*step;if(!bins.has(m))bins.set(m,{minute:m,keyboard:null,mouse:null,coverage:0,step});const b=bins.get(m);
   if(r.module==='coverage')b.coverage+=Number(r.value);else b[r.module]=(b[r.module]||0)+Number(r.value);
  }
  return [...bins.values()].sort((a,b)=>a.minute-b.minute).map(b=>({...b,keyboard:b.keyboard??(b.coverage>0?0:null),mouse:b.mouse??(b.coverage>0?0:null)}));
 }
 function distribution(values){const sum=values.reduce((n,v)=>n+v,0);return sum>0?values.map(v=>v/sum):null;}
 function protocol(records){const valid=records.filter(r=>r.accepted>0),same=new Set(valid.map(r=>r.source+'|'+r.basis)).size<=1;
  const bytes=[0,0];if(same)for(const r of valid){bytes[0]+=Number(r.ipv4_up)+Number(r.ipv4_down);bytes[1]+=Number(r.ipv6_up)+Number(r.ipv6_down);}
  const has=valid.length>0&&same,quality=records.some(r=>r.quality==='incomplete_observation')?tr("ui.3f955ae5b843"):has?tr("ui.76a2e875d985"):tr("ui.497c85690c4c");
  return {has,bytes,ratio:has?distribution(bytes):null,quality:same?quality:tr("ui.013c99479178")};
 }
 function comparison(current,previous){
  const a=total(current,'coverage',['observed_ms']),b=total(previous,'coverage',['observed_ms']);
  if(!a||!b||Math.abs(a-b)/Math.max(a,b)>.01)return null;
  const before=total(previous,'keyboard');return before>0?(total(current,'keyboard')-before)/before:null;
 }
 return {mouseKeys,total,series,inputs,distribution,protocol,comparison};
})();
