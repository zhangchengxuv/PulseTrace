'use strict';
const summaryModules=['keyboard','mouse','active','network','music'];

function summaryTotals(records){
 const keys=new Map();
 let keyboard=0,clicks=0,network=0,hasNetwork=false,hasInput=false;
 for(const {module,metric,value} of records){
  if(module==='keyboard'){keyboard+=value;keys.set(metric,(keys.get(metric)||0)+value);hasInput=true;}
  if(module==='mouse'&&PulseDashboard.mouseKeys.includes(metric)){clicks+=value;hasInput=true;}
  if(module==='coverage'&&metric==='observed_ms'&&value>0)hasInput=true;
  if(module==='network'&&['interface_upload_bytes','interface_download_bytes'].includes(metric)){
   network+=value;hasNetwork=true;
  }
 }
 let topKey=null,largest=0;
 for(const [key,count] of keys)if(count>largest){topKey=key;largest=count;}
 return {keyboard:hasInput?keyboard:null,clicks:hasInput?clicks:null,network:hasNetwork?network:null,topKey};
}
function summaryMusic(values){
 const songs=musicGroups(values,false,true),artists=musicGroups(values.filter(row=>row.artist?.trim()),true,true);
 const clean=text=>String(text||'').replace(/[\p{Cc}\p{Cf}]+/gu,' ').replace(/\s+/g,' ').trim();
 return {song:clean(songs[0]?.title),artist:clean(artists[0]?.artist)};
}
function summaryMetrics(totals,active,music,modules){
 const metric=(label,value,comparison=false)=>({label:tr('summary.'+label),value:value??'—',comparison});
 return [
  ...(modules.has('keyboard')?[metric('key_count',totals.keyboard==null?null:fmt(totals.keyboard)),metric('top_key',totals.topKey==null?null:keyLabel(Number(totals.topKey)))]:[]),
  ...(modules.has('mouse')?[metric('mouse_count',totals.clicks==null?null:fmt(totals.clicks),true)]:[]),
  ...(modules.has('active')?[metric('active_time',active==null?null:usageDuration(active))]:[]),
  ...(modules.has('network')?[metric('network_total',totals.network==null?null:bytes(totals.network))]:[]),
  ...(modules.has('music')?[metric('song',music?.song||null,true),metric('artist',music?.artist||null,true)]:[])
 ];
}
function summaryPeriod(selection){
 if(selection.from===0)return tr('range.all');
 const format=minute=>PulseDate.format(minute,{year:'numeric',month:'short',day:'numeric'},selection.time_zone);
 const from=format(selection.from),to=format(selection.to-1);
 return from===to?from:from+' – '+to;
}
function drawSummaryCard(canvas,period,metrics,light){
 const palette=light?
  {bg:'#f0f5fb',panel:'#ffffff',line:'#d6e1ef',text:'#162842',muted:'#52637b',blue:'#0369a1',violet:'#6d28d9',accent:'#0284c7'}:
  {bg:'#091628',panel:'#11243b',line:'#263f5c',text:'#f0f7ff',muted:'#adbed6',blue:'#38bdf8',violet:'#a78bfa',accent:'#38bdf8'};
 const columns=metrics.length===1?1:2,padding=56,gutter=18,tileHeight=128,top=214;
 canvas.width=1160;canvas.height=top+Math.ceil(metrics.length/columns)*(tileHeight+gutter)+38;
 const w=canvas.width,h=canvas.height,ctx=canvas.getContext('2d');
 const fonts='"Segoe UI Variable","Segoe UI","Microsoft YaHei UI",sans-serif';
 const rect=(x,y,width,height,radius,fill,stroke)=>{
  ctx.beginPath();ctx.roundRect(x,y,width,height,radius);ctx.fillStyle=fill;ctx.fill();
  if(stroke){ctx.strokeStyle=stroke;ctx.lineWidth=2;ctx.stroke();}
 };
 const text=(value,x,y,max,size,weight,color)=>{
  ctx.font=weight+' '+size+'px '+fonts;ctx.fillStyle=color;
  let label=String(value);
  if(ctx.measureText(label).width>max){
   const points=Array.from(label);while(points.length&&ctx.measureText(points.join('')+'…').width>max)points.pop();
   label=points.join('')+'…';
  }
  ctx.fillText(label,x,y);
 };
 ctx.fillStyle=palette.bg;ctx.fillRect(0,0,w,h);
 const gradient=ctx.createLinearGradient(0,0,w,0);
 gradient.addColorStop(0,palette.accent);gradient.addColorStop(1,palette.violet);
 ctx.fillStyle=gradient;ctx.fillRect(0,0,w,9);
 rect(padding,48,42,42,11,palette.panel,palette.line);
 ctx.strokeStyle=palette.blue;ctx.lineWidth=2.5;ctx.lineJoin='round';ctx.beginPath();
 for(const [index,[x,y]] of [[8,22],[15,22],[19,12],[24,31],[28,20],[34,20]].entries()){
  if(index)ctx.lineTo(padding+x,48+y);else ctx.moveTo(padding+x,48+y);
 }
 ctx.stroke();
 text('PulseTrace',padding+56,77,250,24,650,palette.blue);
 text(tr('summary.title'),padding,143,w-2*padding,49,700,palette.text);
 text(period,padding,182,w-2*padding,24,500,palette.muted);
 const tileWidth=(w-2*padding-(columns-1)*gutter)/columns;
 for(const [index,metric] of metrics.entries()){
  const x=padding+(index%columns)*(tileWidth+gutter),y=top+Math.floor(index/columns)*(tileHeight+gutter);
  const cardWidth=index===metrics.length-1&&index%columns===0?w-2*padding:tileWidth;
  const color=metric.comparison?palette.violet:palette.blue;
  rect(x,y,cardWidth,tileHeight,19,palette.panel,palette.line);
  rect(x+22,y+23,5,21,2.5,color);
  text(metric.label,x+38,y+42,cardWidth-64,22,600,palette.muted);
  text(metric.value,x+25,y+99,cardWidth-50,38,650,palette.text);
 }
 canvas.setAttribute('aria-label',[tr('summary.title'),period,...metrics.flatMap(item=>[item.label,item.value])].join(' · '));
}
function saveSummaryPNG(canvas){
 const image=canvas.toDataURL('image/png');
 const prefix='data:image/png;base64,';
 if(!image.startsWith(prefix))throw Error(tr('summary.save_failed'));
 return rpc('save_summary_png',{png:image.slice(prefix.length)},0);
}
function openSummaryCard(records,selection){
 const totals=summaryTotals(records),period=summaryPeriod(selection),params={from:selection.from,to:selection.to};
 const musicEnabled=state.config?.music_enabled===true;
 const dialog=el('dialog',{class:'summary-dialog','aria-label':tr('summary.title')});
 const preview=el('canvas',{class:'summary-preview',role:'img'});
 const feedback=el('p',{class:'muted',role:'status'});
 const options=el('fieldset',{class:'summary-options'},el('legend',{},tr('summary.modules')));
 const save=el('button',{type:'button'},tr('summary.save'));
 const close=el('button',{class:'secondary',type:'button',onclick:()=>dialog.close()},tr('ui.2cd0f3be8738'));
 const activeModules=new Set(summaryModules.filter(key=>key!=='music'));
 const required=['active','music'];
 const data={active:null,music:null},requests={},failed=new Set();
 let saving=false;
 const paint=()=>{
  if(!dialog.open||document.hidden||!nativeVisible)return;
  const loading=required.some(key=>activeModules.has(key)&&requests[key]?.busy);
  const errors=required.some(key=>activeModules.has(key)&&failed.has(key));
  save.disabled=saving||loading||!activeModules.size;
  preview.hidden=!activeModules.size;
  feedback.textContent=loading?tr('summary.loading'):errors?tr('summary.partial'):'';
  if(activeModules.size)drawSummaryCard(preview,period,summaryMetrics(totals,data.active,data.music,activeModules),document.documentElement.dataset.theme==='light');
 };
 const fetchModule=key=>{
  if(!required.includes(key)||requests[key])return;
  const query=key==='active'?'get_active_duration':'get_music';
  const request={busy:true};requests[key]=request;
  rpc(query,params).then(result=>{data[key]=key==='active'?result.active_ms:summaryMusic(result.rows);})
   .catch(()=>failed.add(key)).finally(()=>{request.busy=false;paint();});
 };
 for(const key of summaryModules){
  if(key==='music'&&!musicEnabled)continue;
  const input=el('input',{type:'checkbox',checked:activeModules.has(key)});
  input.onchange=()=>{
   if(input.checked){activeModules.add(key);fetchModule(key);}else activeModules.delete(key);
   paint();
  };
  options.append(el('label',{class:'checkbox'},input,tr('summary.module_'+key)));
 }
 save.onclick=async()=>{
  saving=true;paint();
  try{if((await saveSummaryPNG(preview)).saved)feedback.textContent=tr('summary.saved');}
  catch{feedback.textContent=tr('summary.save_failed');}
  finally{saving=false;save.disabled=!activeModules.size;}
 };
 document.addEventListener('visibilitychange',paint);
 dialog.addEventListener('close',()=>{document.removeEventListener('visibilitychange',paint);dialog.remove();},{once:true});
 dialog.append(el('h3',{},tr('summary.title')),options,el('p',{class:'muted summary-privacy'},tr('summary.privacy')),preview,feedback,el('div',{class:'actions'},save,close));
 document.body.append(dialog);dialog.showModal();
 fetchModule('active');paint();
}
