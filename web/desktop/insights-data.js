'use strict';
globalThis.PulseInsights=(()=>{
 const defaultRatings={keyboard:50000000,mouse:20000000};
 const ratingKey='pulsetrace.insights.rated';
 const scrollStepMetres=.003;
 const trackMetres=400;
 const wasdTarget=.25,nightTarget=.30,minimumActions=100,songTarget=10;
 const keyboardLevels=[1000,10000,100000,1000000];
 const mouseLevels=[100,1000,10000,100000];
 const rate=(count,total)=>total>0?count/total:null;
 const validRating=value=>Number.isSafeInteger(value)&&value>0;
 function rating(device){
  try{
   const stored=JSON.parse(localStorage.getItem(ratingKey))?.[device];
   if(validRating(stored))return stored;
  }catch{}
  return defaultRatings[device];
 }
 function saveRatings(ratings){
  localStorage.setItem(ratingKey,JSON.stringify(ratings));
 }
 function keyboard(counts,total){
  const hits=ids=>ids.reduce((sum,id)=>sum+Number(counts[id]||0),0);
  const fingers=Object.entries(PulseKeyboard.fingerKeys).flatMap(([hand,groups])=>
   Object.entries(groups).map(([name,ids])=>({hand,name,count:hits(ids)})));
  const left=fingers.filter(finger=>finger.hand==='left').reduce((sum,finger)=>sum+finger.count,0);
  const right=fingers.reduce((sum,finger)=>sum+finger.count,0)-left;
  const space=hits([57]);
  const corrections=hits([14,339]);
  const typed=hits(PulseKeyboard.alphanumeric);
  return {fingers,left,right,space,unassigned:Math.max(0,total-left-right-space),total,
   corrections,typed,correctionRate:rate(corrections,typed)};
 }
 function observedDays(rows,selection,device){
  let first=Infinity,last=-Infinity;
  for(const {module,metric,value,minute} of rows){
   if(value<=0||!Number.isFinite(minute)||!(module===device||module==='coverage'&&metric==='observed_ms'))continue;
   first=Math.min(first,minute);
   last=Math.max(last,minute);
  }
  if(!Number.isFinite(first))return 0;
  const start=Math.max(first,selection.from);
  const lastDay=selection.bucket===1440?PulseDate.calendarStart(last,'today',selection.time_zone,-1):last+selection.bucket;
  return Math.max(0,Math.min(lastDay,selection.to)-start)/1440;
 }
 function wear(count,rated,days){
  if(count==null)return {share:null,years:null};
  const share=count/rated;
  const remaining=Math.max(0,rated-count);
  const years=remaining===0?0:count>0&&days>=7?remaining/count*days/365.25:null;
  return {share,years};
 }
 function wheel(rows){
  const buckets=new Map();
  let observed=false;
  for(const {module,metric,value,minute} of rows){
   if(module==='coverage'&&metric==='observed_ms'&&value>0)observed=true;
   if(module!=='mouse')continue;
   observed=true;
   if(!['wheel_up_delta','wheel_down_delta','wheel_up_events','wheel_down_events'].includes(metric))continue;
   const key=minute+'/'+metric.split('_')[1];
   if(!buckets.has(key))buckets.set(key,{delta:null,events:0});
   const bucket=buckets.get(key);
   if(metric.endsWith('_delta'))bucket.delta=(bucket.delta??0)+Math.abs(Number(value));
   else bucket.events+=Number(value);
  }
  let fallback=false,steps=0;
  for(const {delta,events} of buckets.values()){
   if(delta==null&&events>0)fallback=true;
   steps+=delta==null?events:delta/120;
  }
  return observed?{metres:steps*scrollStepMetres,fallback}:null;
 }
 function milestone(count,levels){
  const level=levels.filter(target=>count>=target).length;
  return {count,level,levels:levels.length,target:levels[Math.min(level,levels.length-1)]};
 }
 function achievements(rows,selection){
  const counts=new Map(),nightHours=new Map();
  const timed=selection.bucket===1||selection.bucket===60;
  let presses=0,clicks=0,nightActions=0;
  for(const {minute,module,metric,value} of rows){
   const count=Number(value);
   if(count<=0)continue;
   if(module==='keyboard'){
    const key=Number(metric);
    counts.set(key,(counts.get(key)||0)+count);presses+=count;
   }else if(module==='mouse'&&PulseDashboard.mouseKeys.includes(metric)){
    clicks+=count;
   }else continue;
   if(timed){
    if(!nightHours.has(minute))nightHours.set(minute,PulseDate.parts(minute,selection.time_zone).hour<4);
    if(nightHours.get(minute))nightActions+=count;
   }
  }
  const keyCount=PulseKeyboard.keys.filter(key=>counts.has(key.id)).length;
  const wasd=[17,30,31,32].reduce((sum,id)=>sum+(counts.get(id)||0),0);
  return {keyCount,physicalKeys:PulseKeyboard.keys.length,wasd:rate(wasd,presses),
   night:timed?rate(nightActions,presses+clicks):null,timed,actions:presses+clicks,
   presses:milestone(presses,keyboardLevels),clicks:milestone(clicks,mouseLevels)};
 }
 function songPlays(rows){
  const plays=new Map();
  let most=0;
  for(const {key,player,plays:count} of rows){
   const identity=JSON.stringify([player,key]);
   const total=(plays.get(identity)||0)+Number(count);
   plays.set(identity,total);
   most=Math.max(most,total);
  }
  return most;
 }
 return {rating,validRating,saveRatings,keyboard,observedDays,wear,wheel,achievements,songPlays,
  scrollStepMetres,trackMetres,wasdTarget,nightTarget,minimumActions,songTarget};
})();
