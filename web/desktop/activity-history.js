'use strict';
globalThis.PulseActivity=(()=>{
 const dayMs=86400000;
 const civilDay=({year,month,day})=>Date.UTC(year,month-1,day);
 const weekday=date=>(new Date(date).getUTCDay()+6)%7;
 function rhythm(rows,zone){
  const cells=Array.from({length:168},(_,i)=>({day:Math.floor(i/24),hour:i%24,total:0,samples:0}));
  for(const {minute,value} of rows){
   const parts=PulseDate.parts(minute,zone);
   const cell=cells[weekday(civilDay(parts))*24+parts.hour];
   cell.total+=value;
   cell.samples++;
  }
  return cells.map(cell=>({...cell,value:cell.samples?cell.total/cell.samples:null}));
 }
 function yearRange(year,zone,now){
  const start=y=>(zone==='UTC'?Date.UTC(y,0,1):new Date(y,0,1).getTime())/60000;
  return {from:Math.max(0,start(year)),to:Math.min(now+1,start(year+1)),time_zone:zone};
 }
 function yearCells(year,rows,zone,now){
  const first=Date.UTC(year,0,1),end=Date.UTC(year+1,0,1);
  const today=civilDay(PulseDate.parts(now,zone)),startDay=weekday(first);
  const values=new Map(rows.map(({minute,value})=>[civilDay(PulseDate.parts(minute,zone)),value]));
  const cells=[];
  for(let date=first,index=startDay;date<end;date+=dayMs,index++){
   cells.push({date,day:index%7,week:Math.floor(index/7),value:values.get(date)??null,future:date>today});
  }
  return cells;
 }
 function queryWindows(request,now){
  const {from,to,view,time_zone:zone}=request;
  // Only cache finished buckets outside the collector's two-hour live overlay.
  const stable=now-120;
  const boundary=view==='year'?PulseDate.calendarStart(stable,'today',zone):PulseDate.hourStart(stable,zone);
  const cut=Math.min(to,Math.max(from,boundary));
  return {
   history:cut>from?{...request,to:cut}:null,
   recent:cut<to?{...request,from:cut}:null
  };
 }
 return {rhythm,yearRange,yearCells,queryWindows};
})();

let activityHistory=null;
function resetActivityHistory(){activityHistory=null;}
const activityText=(key,params)=>tr('activity.'+key,params);

function activityGrid(columns,headers,weekdays,cells,kind){
 const grid=el('div',{class:'activity-grid activity-'+kind,role:'group','aria-label':activityText(kind)});
 grid.style.setProperty('--columns',columns);
 const at=(node,column,row)=>{
  node.style.gridColumn=String(column+2);
  node.style.gridRow=String(row+2);
  grid.append(node);
  return node;
 };
 for(const [column,label] of headers)at(el('span',{class:'activity-axis','aria-hidden':'true'},label),column,-1);
 weekdays.forEach((label,day)=>at(el('span',{class:'activity-axis','aria-hidden':'true'},label),-1,day));
 const detail=el('p',{class:'activity-detail muted',role:'status'},activityText('inspect'));
 const buttons=new Map(),maximum=Math.max(0,...cells.map(cell=>cell.value??0));
 let current;
 function select(button,focus=false){
  current.tabIndex=-1;current=button;current.tabIndex=0;
  detail.textContent=button.title;
  if(focus)button.focus();
 }
 for(const {column,day,value,label,future} of cells){
  const status=future?'future':value==null?'missing':value===0?'zero':'measured';
  const button=at(el('button',{type:'button',class:'activity-cell','data-status':status,
   title:label,'aria-label':label,tabindex:current?-1:0}),column,day);
  if(value>0)button.style.setProperty('--fill',((.18+.82*Math.sqrt(value/maximum))*100)+'%');
  button.dataset.column=column;button.dataset.row=day;
  current??=button;buttons.set(column+'/'+day,button);
 }
 const target=event=>event.target.closest('.activity-cell');
 grid.addEventListener('focusin',event=>{const button=target(event);if(button)select(button);});
 grid.addEventListener('pointerover',event=>{const button=target(event);if(button)detail.textContent=button.title;});
 grid.addEventListener('click',event=>{const button=target(event);if(button)select(button,true);});
 grid.addEventListener('keydown',event=>{
  const offsets={ArrowUp:[0,-1],ArrowDown:[0,1],ArrowLeft:[-1,0],ArrowRight:[1,0]};
  const step=offsets[event.key],button=target(event);
  if(!step||!button)return;
  event.preventDefault();
  const next=buttons.get((Number(button.dataset.column)+step[0])+'/'+(Number(button.dataset.row)+step[1]));
  if(next)select(next,true);
 });
 const legend=el('div',{class:'activity-legend activity-'+kind+' muted'},...['missing','zero','measured'].map(status=>
  el('span',{},el('i',{class:'activity-cell','data-status':status,'aria-hidden':'true'}),activityText(status))));
 return [el('div',{class:'activity-scroll'},grid),legend,detail];
}

function createActivityHistory(){
 const labels=Array.from({length:7},(_,n)=>PulseDate.format(Date.UTC(2024,0,1+n)/60000,{weekday:'short'},'UTC'));
 const number=new Intl.NumberFormat(PulseI18n.locale,{maximumFractionDigits:1});
 const monthLabel=new Intl.DateTimeFormat(PulseI18n.locale,{month:'short',timeZone:'UTC'});
 const dateLabel=new Intl.DateTimeFormat(PulseI18n.locale,{year:'numeric',month:'short',day:'numeric',timeZone:'UTC'});
 const rhythmHost=el('div'),yearHost=el('div');
 const rhythm=panel(activityText('rhythm'),el('p',{class:'muted'},activityText('rhythm_basis')),rhythmHost);
 const year=el('select',{'aria-label':activityText('year_choice')});
 const metric=el('select',{'aria-label':activityText('metric')});
 const music=state.config?.music_enabled===true;
 const metrics=['keyboard','mouse','active','network',...(music?['music']:[])];
 for(const value of metrics)metric.append(el('option',{value},activityText(value)));
 const wall=panel(activityText('wall'),el('div',{class:'activity-options'},year,metric),
  el('p',{class:'muted'},activityText('wall_basis')),yearHost);
 const root=el('div',{class:'activity-history'},rhythm,wall);
 const live=()=>page==='history'&&root.isConnected&&!document.hidden&&nativeVisible;

 function load(host,draw){
  let key='',drawKey='',revision=0,pending,historyKey='',historyReading;
  return (request,showLoading)=>{
   const next=JSON.stringify(request);
   if(next===key)return pending;
   key=next;
   const id=++revision;
   if(showLoading||!host.childElementCount){
    drawKey='';
    host.replaceChildren(el('p',{class:'muted',role:'status'},activityText('loading')));
   }
   const {history,recent}=PulseActivity.queryWindows(request,Math.floor(Date.now()/60000));
   const older=JSON.stringify(history);
   if(older!==historyKey){
    historyKey=older;
    historyReading=history&&rpc('get_activity',history).catch(failure=>{
     if(historyKey===older)historyKey='';
     throw failure;
    });
   }
   pending=Promise.all([historyReading,recent&&rpc('get_activity',recent)]).then(([before,after])=>{
    if(id!==revision)return;
    if(!live()){key='';return;}
    const rows=[...(before?.rows||[]),...(after?.rows||[])];
    const day=PulseDate.parts(request.to-1,request.time_zone).day;
    const signature=JSON.stringify([request.time_zone,request.from,day,request.metric,rows]);
    if(signature===drawKey)return;
    const focus=host.contains(document.activeElement)?document.activeElement.dataset:null;
    host.replaceChildren(...draw(rows,request));
    drawKey=signature;
    if(focus)host.querySelector(`.activity-cell[data-column="${focus.column}"][data-row="${focus.row}"]`)?.focus({preventScroll:true});
   }).catch(()=>{
    if(id===revision&&live()){
     drawKey='';
     host.replaceChildren(el('p',{class:'muted',role:'status'},activityText('unavailable')));
    }
   });
   return pending;
  };
 }
 const loadRhythm=load(rhythmHost,(rows,request)=>{
  const headers=Array.from({length:8},(_,n)=>[n*3,String(n*3).padStart(2,'0')]);
  const cells=PulseActivity.rhythm(rows,request.time_zone).map(cell=>{
   const time=labels[cell.day]+' '+String(cell.hour).padStart(2,'0')+':00';
   const value=cell.value==null?activityText('missing'):activityText('rhythm_values',{
    average:number.format(cell.value),total:fmt(cell.total),hours:fmt(cell.samples)});
   return {...cell,column:cell.hour,label:activityText('cell',{time,value})};
  });
  return activityGrid(24,headers,labels,cells,'rhythm');
 });
 const loadYear=load(yearHost,(rows,request)=>{
  const selected=Number(year.value),now=Math.floor(Date.now()/60000),headers=[];
  const cells=PulseActivity.yearCells(selected,rows,request.time_zone,now).map(cell=>{
   if(new Date(cell.date).getUTCDate()===1)
    headers.push([cell.week,monthLabel.format(cell.date)]);
   const time=dateLabel.format(cell.date);
   const kind=request.metric;
   const value=cell.future?activityText('future'):cell.value==null?activityText('missing'):
    kind==='network'?bytes(cell.value):kind==='active'||kind==='music'?usageDuration(cell.value):fmt(cell.value);
   return {...cell,column:cell.week,label:activityText('cell',{time,value})};
  });
  return activityGrid(cells.at(-1).week+1,headers,labels,cells,'wall');
 });
 function updateYear(showLoading=false){
  if(!live())return;
  const now=Math.floor(Date.now()/60000),zone=range.time_zone;
  const last=PulseDate.parts(now,zone).year;
  const first=Math.max(1970,Math.min(last-1,PulseDate.parts(historyFirst,zone).year));
  if(year.dataset.bounds!==first+'/'+last){
   const selected=year.value;
   year.replaceChildren(...Array.from({length:last-first+1},(_,n)=>el('option',{value:last-n},String(last-n))));
   if(Number(selected)>=first&&Number(selected)<=last)year.value=selected;
   year.dataset.bounds=first+'/'+last;
  }
  return loadYear({...PulseActivity.yearRange(Number(year.value),zone,now),view:'year',metric:metric.value},showLoading);
 }
 year.addEventListener('change',()=>updateYear(true));
 metric.addEventListener('change',()=>updateYear(true));
 function update(){
  if(!live())return;
  const to=range.to,from=Math.max(range.from,PulseDate.calendarStart(to-1,'today',range.time_zone,365));
  return Promise.all([loadRhythm({from,to,time_zone:range.time_zone,view:'rhythm'}),updateYear()]);
 }
 return {root,update,locale:PulseI18n.locale,music};
}

function renderActivityHistory(container){
 if(!activityHistory||activityHistory.locale!==PulseI18n.locale||activityHistory.music!==(state.config?.music_enabled===true))
  activityHistory=createActivityHistory();
 const view=activityHistory;
 container.append(view.root);
 queueMicrotask(view.update);
}
