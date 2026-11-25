'use strict';
globalThis.PulseZen=(()=>{
 const span=8500,decay=1700,maxPulses=40;
 let ui,active=false,streaming=false,fullscreen=false,fullscreenPending=false;
 let frame=null,lastPaint=-Infinity,observer,palette,dirty=false;
 let previous,latest,baseline,dayRequest,attempt,visibilityEpoch=0,modeEpoch=0;
 let clockLocale,clockFormat,scrollTop=0;
 const pulses=[];
 const canRun=()=>active&&nativeVisible&&document.visibilityState==='visible';
 const text=(key,params)=>tr('zen.'+key,params);
 const callStream=enabled=>chrome.webview.postMessage({type:'set_zen_stream',request_id:0,params:{enabled}});
 const title=(button,label)=>{button.title=label;button.setAttribute('aria-label',label);};
 function relabel(){
  if(!ui)return;
  title(ui.enter,text('title'));
  title(ui.fullscreen,text(fullscreen?'restore':'fullscreen'));
  title(ui.exit,text('exit'));
  ui.fullscreen.setAttribute('aria-pressed',String(fullscreen));
 }
 function init(){
  ui={enter:$('zen-enter'),stage:$('zen-stage'),canvas:$('zen-canvas'),
   fullscreen:$('zen-fullscreen'),exit:$('zen-exit'),clock:$('zen-clock'),
   keys:$('zen-keys'),clicks:$('zen-clicks'),track:$('zen-track'),error:$('zen-error')};
  ui.enter.addEventListener('click',enter);
  ui.exit.addEventListener('click',exit);
  ui.canvas.addEventListener('dblclick',exit);
  ui.fullscreen.addEventListener('click',toggleFullscreen);
  document.addEventListener('keydown',event=>{
   if(active&&event.key==='Escape'){
    event.preventDefault();
    event.stopPropagation();
    exit();
   }
  },true);
  window.addEventListener('pagehide',pause,{once:true});
  relabel();
 }
 function enter(){
  if(active||!nativeVisible||document.visibilityState!=='visible')return;
  active=true;scrollTop=window.scrollY;++modeEpoch;++viewGeneration;
  PulseDatePicker.close();
  document.body.classList.add('zen-open');
  ui.stage.hidden=false;ui.error.hidden=true;
  palette=null;
  updateUiVisibility();
  ui.exit.focus({preventScroll:true});
 }
 function exit(){
  if(!active)return;
  active=false;++modeEpoch;++viewGeneration;
  pause();
  if(fullscreen||fullscreenPending)rpc('set_zen_fullscreen',{enabled:false},0).catch(()=>{});
  fullscreen=fullscreenPending=false;
  ui.fullscreen.disabled=false;
  ui.stage.hidden=true;
  document.body.classList.remove('zen-open');
  window.scrollTo({top:scrollTop,behavior:'instant'});
  relabel();
  updateUiVisibility();
  ui.enter.focus({preventScroll:true});
 }
 async function toggleFullscreen(){
  if(!canRun()||fullscreenPending)return;
  const epoch=modeEpoch;
  fullscreenPending=true;ui.fullscreen.disabled=true;ui.error.hidden=true;
  try{
   const result=await rpc('set_zen_fullscreen',{enabled:!fullscreen},0);
   if(epoch!==modeEpoch)return;
   fullscreen=result.fullscreen;
   relabel();
  }catch{
   if(epoch===modeEpoch){ui.error.textContent=text('fullscreen_error');ui.error.hidden=false;}
  }finally{
   if(epoch===modeEpoch){fullscreenPending=false;ui.fullscreen.disabled=false;}
  }
 }
 function syncVisibility(){
  if(!ui)return;
  if(!canRun()){pause();return;}
  if(streaming)return;
  streaming=true;
  observer=new ResizeObserver(invalidate);
  observer.observe(ui.canvas);
  invalidate();
  callStream(true);
 }
 function pause(){
  if(!streaming)return;
  streaming=false;++visibilityEpoch;
  if(frame!==null)cancelAnimationFrame(frame);
  frame=null;observer.disconnect();observer=null;
  pulses.length=0;previous=latest=baseline=dayRequest=attempt=null;
  palette=clockLocale=clockFormat=null;
  lastPaint=-Infinity;
  ui.canvas.width=ui.canvas.height=1;
  ui.clock.textContent=ui.keys.textContent=ui.clicks.textContent='—';
  ui.track.hidden=true;ui.track.textContent='';
  callStream(false);
 }
 function dayOf(ms){return PulseDate.calendarStart(Math.floor(ms/60000),'today','local');}
 function setValue(node,value){if(node.textContent!==value)node.textContent=value;}
 function updateTotals(){
  if(!latest)return;
  const day=dayOf(latest.time_ms);
  if(baseline?.day===day&&baseline.session===latest.session&&latest.keys>=baseline.keys&&latest.clicks>=baseline.clicks){
   setValue(ui.keys,fmt(baseline.day_keys+latest.keys-baseline.keys));
   setValue(ui.clicks,fmt(baseline.day_clicks+latest.clicks-baseline.clicks));
   return;
  }
  setValue(ui.keys,'—');setValue(ui.clicks,'—');
  if(dayRequest||latest.active&&latest.input_ms<day*60000)return;
  const key=day+'/'+latest.session;
  const now=performance.now();
  if(attempt?.key===key&&now-attempt.time<15000)return;
  const epoch=visibilityEpoch;
  attempt={key,time:now};
  // The stream's visibility epoch discards old replies; no timeout or polling timer is created.
  dayRequest=rpc('get_zen_day',{},0).then(result=>{
   if(epoch!==visibilityEpoch||!canRun())return;
   baseline=result;dayRequest=null;updateTotals();
  },()=>{if(epoch===visibilityEpoch)dayRequest=null;});
 }
 function accept(sample){
  if(!streaming||!canRun())return;
  latest=sample;
  if(clockLocale!==PulseI18n.locale){
   clockLocale=PulseI18n.locale;
   clockFormat=new Intl.DateTimeFormat(clockLocale,{hour:'2-digit',minute:'2-digit',second:'2-digit',hourCycle:'h23'});
  }
  setValue(ui.clock,clockFormat.format(sample.time_ms));
  ui.track.hidden=!sample.track?.title;
  setValue(ui.track,sample.track?.title?[sample.track.title,sample.track.artist].filter(Boolean).join(' · '):'');
  const sameSession=previous?.active&&previous.session===sample.session&&sample.active;
  const inputs=sameSession?Math.max(0,sample.keys-previous.keys)+Math.max(0,sample.clicks-previous.clicks):0;
  const input=Math.min(1,Math.log1p(inputs)/Math.log(65));
  const rate=sample.active?Math.max(0,sample.network_bps):0;
  const previousRate=sameSession?Math.max(0,previous.network_bps):0;
  const network=rate||previousRate?Math.min(1,.55*Math.log1p(rate/4096)/9+
   .45*Math.abs(Math.log1p(rate/4096)-Math.log1p(previousRate/4096))/5):0;
  previous=sample;
  if(input||network){
   pulses.push({at:performance.now(),input,network});
   if(pulses.length>maxPulses)pulses.shift();
  }
  updateTotals();
  invalidate();
 }
 function invalidate(){
  dirty=true;
  if(frame===null&&canRun())frame=requestAnimationFrame(tick);
 }
 function tick(now){
  frame=null;
  if(!streaming||!canRun())return;
  if(dirty||now-lastPaint>=32){
   dirty=false;lastPaint=now;
   while(pulses.length&&now-pulses[0].at>span+decay)pulses.shift();
   paint(now);
  }
  if(pulses.length&&canRun())frame=requestAnimationFrame(tick);
 }
 function paint(now){
  const {canvas,stage}=ui;
  const width=canvas.clientWidth,height=canvas.clientHeight,dpr=window.devicePixelRatio||1;
  if(!width||!height)return;
  const px=Math.max(1,Math.round(width*dpr)),py=Math.max(1,Math.round(height*dpr));
  if(canvas.width!==px)canvas.width=px;
  if(canvas.height!==py)canvas.height=py;
  const ctx=canvas.getContext('2d');
  ctx.setTransform(px/width,0,0,py/height,0,0);
  ctx.clearRect(0,0,width,height);
  if(!palette){
   const style=getComputedStyle(stage);
   palette=['--series-primary','--series-comparison'].map(name=>style.getPropertyValue(name).trim());
  }
  const segments=Math.max(100,Math.min(220,Math.round(width/6)));
  for(const [lane,signal] of ['input','network'].entries()){
   const center=height*(lane?.68:.32),color=palette[lane];
   const strength=Math.min(height*.23,112)*(lane?.66:1);
   const gradient=ctx.createLinearGradient(0,0,width,0);
   gradient.addColorStop(0,'transparent');
   gradient.addColorStop(.42,color+'55');
   gradient.addColorStop(1,color);
   ctx.beginPath();
   for(let i=0;i<=segments;i++){
    const t=now-span*(1-i/segments);
    let amplitude=0;
    for(const pulse of pulses){
     const lag=t-pulse.at;
     if(lag<0||lag>decay)continue;
     const envelope=Math.min(1,lag/65)*Math.exp(-lag/(lane?490:230));
     amplitude+=pulse[signal]*envelope*(lane?Math.sin(lag/100):
      Math.cos(lag/38)-.4*Math.sin(lag/23));
    }
    const x=width*i/segments,y=center-Math.max(-1,Math.min(1,amplitude))*strength;
    if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
   }
   ctx.strokeStyle=gradient;ctx.lineJoin='round';
   ctx.globalAlpha=.14;ctx.lineWidth=5;ctx.stroke();
   ctx.globalAlpha=1;ctx.lineWidth=1.65;ctx.stroke();
  }
 }
 return {init,enter,exit,relabel,syncVisibility,accept,get active(){return active;},get running(){return frame!==null;}};
})();
