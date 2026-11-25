'use strict';
globalThis.PulseMusicInsights=(()=>{
 const hasPlayback=rows=>rows.some(row=>row.played_ms>0);
 function singleDay({from,to,bucket,time_zone:zone}){
  return from>0&&to>from&&bucket<1440&&to-from<=26*60&&
   PulseDate.calendarStart(from,'today',zone)===PulseDate.calendarStart(to-1,'today',zone);
 }
 function playbackMinutes(rows,{from,to}){
  const minutes=new Map();
  for(const {minute,title,artist,played_ms} of rows){
   if(played_ms<=0||minute<from||minute>=to)continue;
   if(!minutes.has(minute))minutes.set(minute,new Set());
   minutes.get(minute).add([title,artist].filter(Boolean).join(' · '));
  }
  const bands=[];
  let previous;
  for(const minute of [...minutes.keys()].sort((a,b)=>a-b)){
   const signature=JSON.stringify([...minutes.get(minute)].sort());
   const end=bands.at(-1);
   if(end?.to===minute&&previous===signature)end.to++;
   else bands.push({from:minute,to:minute+1});
   previous=signature;
  }
  return {minutes,bands};
 }
 return {hasPlayback,singleDay,playbackMinutes};
})();

const bgmText=(key,params)=>tr('bgm.'+key,params);

function musicInsightTrend(){
 const canvas=el('canvas',{class:'bgm-chart',role:'img','aria-label':bgmText('trend')});
 const toggle=el('input',{type:'checkbox'});
 const cursor=el('input',{type:'range',step:1,'aria-label':bgmText('inspect')});
 const readout=el('p',{class:'bgm-readout muted',role:'status'});
 const inspector=el('div',{class:'bgm-inspector',hidden:true},cursor,readout);
 const noInput=el('p',{class:'muted'},bgmText('no_input'));
 const host=el('section',{class:'bgm-timeline',hidden:true},
  el('h4',{},bgmText('trend')),el('label',{class:'checkbox'},toggle,bgmText('show_marks')),
  noInput,canvas,inspector);
 let data, left=46, right=46;
 const tick=minute=>PulseDate.format(minute,{hour:'2-digit',minute:'2-digit',hourCycle:'h23'},data.scope.time_zone);
 function inspect(minute){
  const labels=data.playback.minutes.get(minute);
  const label=labels?.size?[...labels].filter(Boolean).join(' / ')||bgmText('no_track'):bgmText('no_track');
  const text=bgmText('track_at',{time:tick(minute),track:label});
  cursor.value=minute;cursor.setAttribute('aria-valuetext',text);
  if(readout.textContent!==text)readout.textContent=text;
 }
 const observer=PulseRender.observe(canvas,()=>{
  const width=canvas.clientWidth;
  if(!data||host.hidden||!canvas.closest('details')?.open||width<=60)return;
  const height=190,baseline=151,dpr=Math.min(devicePixelRatio||1,3);
  canvas.width=Math.round(width*dpr);canvas.height=Math.round(height*dpr);
  const ctx=canvas.getContext('2d'),css=getComputedStyle(document.documentElement);
  const {scope,points,playback}=data,span=scope.to-scope.from;
  right=width-12;
  const x=minute=>left+(minute-scope.from)/span*(right-left);
  const maximum=Math.max(1,...points.map(row=>row.presses));
  ctx.scale(dpr,dpr);
  if(toggle.checked){
   ctx.fillStyle=css.getPropertyValue('--series-comparison');
   ctx.globalAlpha=.25;
   for(const band of playback.bands)ctx.fillRect(x(band.from),20,Math.max(1,x(band.to)-x(band.from)-1),baseline-20);
   ctx.globalAlpha=1;
  }
  ctx.font='11px Segoe UI';
  ctx.fillStyle=css.getPropertyValue('--muted');
  ctx.fillText(fmt(maximum),0,18);
  ctx.strokeStyle=css.getPropertyValue('--chart-grid');
  ctx.beginPath();ctx.moveTo(left,baseline);ctx.lineTo(right,baseline);ctx.stroke();
  ctx.strokeStyle=ctx.fillStyle=css.getPropertyValue('--series-primary');
  ctx.lineWidth=2;ctx.beginPath();
  let previous;
  for(const {minute,presses} of points){
   const px=x(Math.max(scope.from,minute)),py=baseline-presses/maximum*120;
   if(previous==null||minute-previous>scope.bucket)ctx.moveTo(px,py);
   else ctx.lineTo(px,py);
   previous=minute;
  }
  ctx.stroke();
  for(const {minute,presses} of points)ctx.fillRect(x(Math.max(scope.from,minute))-1,baseline-presses/maximum*120-1,3,3);
  ctx.fillStyle=css.getPropertyValue('--muted');
  ctx.fillText(tick(scope.from),left,174);
  ctx.textAlign='right';ctx.fillText(tick(scope.to-1),right,174);
 });
 chartObservers.push(observer);
 toggle.onchange=()=>{
  inspector.hidden=!toggle.checked;
  if(data&&toggle.checked)inspect(Number(cursor.value));
  observer.schedule?.();
 };
 cursor.oninput=()=>{if(data)inspect(Number(cursor.value));};
 canvas.addEventListener('pointermove',event=>{
  if(!data||!toggle.checked||right<=left)return;
  const {from,to}=data.scope;
  const minute=from+Math.floor((event.offsetX-left)/(right-left)*(to-from));
  if(minute>=from&&minute<to)inspect(minute);
 });
 return {
  host,
  update(scope,points,values){
   if(!scope||!PulseMusicInsights.singleDay(scope)){host.hidden=true;data=null;return;}
   data={scope,points,playback:PulseMusicInsights.playbackMinutes(values,scope)};
   host.hidden=false;noInput.hidden=points.length>0;
   cursor.min=scope.from;cursor.max=scope.to-1;
   const current=Number(cursor.value);
   cursor.value=current>=scope.from&&current<scope.to?current:(data.playback.bands[0]?.from??scope.from);
   if(toggle.checked)inspect(Number(cursor.value));
   observer.schedule?.();
  }
 };
}

function musicInsights(root){
 const group=el('select',{'aria-label':bgmText('group')},
  el('option',{value:'songs'},bgmText('songs')),el('option',{value:'artists'},bgmText('artists')));
 const refresh=el('button',{class:'secondary',type:'button'},bgmText('refresh'));
 const status=el('p',{class:'muted',role:'status'});
 const high=el('div',{class:'bgm-results'}),pairs=el('div',{class:'bgm-results'});
 const trend=musicInsightTrend();
 const body=el('div',{class:'bgm-body'},el('div',{class:'actions'},group,refresh),status,
  el('p',{class:'muted'},bgmText('basis')),
  el('div',{class:'bgm-columns'},el('section',{},el('h4',{},bgmText('leaderboard')),high),
   el('section',{},el('h4',{},bgmText('soundtracks')),pairs)),trend.host);
 const details=el('details',{class:'panel bgm-panel',hidden:true},el('summary',{},bgmText('title')),body);
 const decimals=new Intl.NumberFormat(PulseI18n.locale,{maximumFractionDigits:1});
 let snapshot=[],key='',revision=0;
 const label=item=>group.value==='artists'?item.artist:
  [item.title||musicText('unknown'),item.artist].filter(Boolean).join(' · ');
 const heading=item=>el('div',{class:'bgm-item'},
  el('strong',{},label(item)),item.player?el('small',{class:'muted'},musicText(item.player)):null);
 function active(){
  return details.open&&!details.hidden&&root.isConnected&&page==='music'&&nativeVisible&&!document.hidden&&
   musicView.generation===viewGeneration;
 }
 function render(result,scope,played){
  high.replaceChildren(...(result.leaderboard.length?result.leaderboard.map(item=>el('div',{class:'bgm-row'},
   heading(item),el('div',{class:'bgm-value'},el('strong',{},bgmText('kpm',{value:decimals.format(item.kpm)})),
    el('small',{class:'muted'},bgmText('matched',{duration:usageDuration(item.observed_ms)}))))):
   [el('p',{class:'muted'},bgmText('no_kpm'))]));
  const soundtracks=result.soundtracks.filter(item=>item.apps.length);
  pairs.replaceChildren(...(soundtracks.length?soundtracks.map(item=>el('div',{class:'bgm-row bgm-pair'},
   heading(item),el('ul',{},...item.apps.map(app=>el('li',{},el('span',{},app.name||bgmText('unknown_app')),
    el('span',{class:'muted'},usageDuration(app.overlap_ms))))))):[el('p',{class:'muted'},bgmText('no_apps'))]));
  trend.update(scope,result.trend,played);
 }
 async function load(force=false){
  if(!active())return;
  const scope={...range},mode=group.value,player=musicView.player,merge=musicView.merge;
  const boundary=['today','week','month','custom'].includes(appliedRangePreset)?scope.from:null;
  const next=JSON.stringify([viewGeneration,boundary,scope.bucket,scope.time_zone,PulseMusicInsights.singleDay(scope),player,merge,mode]);
  if(!force&&key===next)return;
  const id=++revision,played=snapshot;
  key=next;refresh.disabled=true;
  status.textContent=bgmText('loading');
  high.replaceChildren();pairs.replaceChildren();trend.update(null);
  try{
   const result=await rpc('get_music_insights',{...scope,player,merge,group:mode});
   if(id!==revision)return;
   if(!active()){key='';return;}
   render(result,scope,played);
   status.textContent=bgmText('as_of',{time:PulseDate.label(scope.to-1,scope.time_zone)});
  }catch{
   if(id===revision){
    if(active())status.textContent=bgmText('unavailable');
    else key='';
   }
  }finally{
   if(id===revision)refresh.disabled=false;
  }
 }
 details.addEventListener('toggle',()=>{if(details.open)load();});
 group.onchange=()=>load();
 refresh.onclick=()=>load(true);
 return {
  details,load,
  update(values,enabled){
   snapshot=values;
   const visible=enabled&&PulseMusicInsights.hasPlayback(values);
   if(!visible&&!details.hidden){
    ++revision;key='';details.open=false;refresh.disabled=false;
    high.replaceChildren();pairs.replaceChildren();status.textContent='';trend.update(null);
   }
   details.hidden=!visible;
   if(visible&&details.open)queueMicrotask(()=>load());
  }
 };
}
