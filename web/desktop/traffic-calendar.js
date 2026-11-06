'use strict';
globalThis.PulseCalendar=(()=>{
 function cells(year,month){const offset=(new Date(Date.UTC(year,month-1,1)).getUTCDay()+6)%7,count=new Date(Date.UTC(year,month,0)).getUTCDate();return Array.from({length:count},(_,i)=>({day:i+1,column:Math.floor((offset+i)/7),row:(offset+i)%7}));}
 function value(day,family,direction){
  if(!day)return {state:'missing',bytes:null};const ip=family!=='all',has=ip?day.has_ip:day.has_totals;
  if(ip&&day.quality==='incomparable_bases')return {state:'partial',bytes:null,reason:'incomparable_bases'};
  if(!has)return {state:'missing',bytes:null};const up=ip?day['ipv'+family+'_up']:day.upload_bytes,down=ip?day['ipv'+family+'_down']:day.download_bytes;
  const bytes=direction==='up'?up:direction==='down'?down:up+down,minutes=ip?day.ip_minutes:day.observed_minutes;
  const partial=minutes<day.expected_minutes||(ip&&day.quality==='incomplete_observation');
  return {bytes,state:partial?'partial':bytes===0?'zero':'observed'};
 }
 function color(value,max,dark){const a=dark?[23,54,76]:[212,233,247],b=dark?[139,196,234]:[25,96,144],t=max>0?Math.sqrt(Math.min(1,Math.max(0,value/max))):0;return a.map((n,i)=>Math.round(n+(b[i]-n)*t));}
 return {cells,value,color};
})();
const calendarView={month:'',family:'all',direction:'total',result:null,key:'',root:null};
function calendarMonth(){const d=new Date();return d.getFullYear()+'-'+String(d.getMonth()+1).padStart(2,'0');}
async function refreshTrafficCalendar(){const v=calendarView,month=v.month||calendarMonth(),key=month+'/'+state.last_flush_ms;if(v.key===key)return;v.result=await rpc('get_network_calendar',{month});v.key=key;}
function renderTrafficCalendar(container){
 const v=calendarView,month=v.month||calendarMonth(),[year,number]=month.split('-').map(Number),data=new Map((v.result?.days||[]).map(d=>[d.date,d])),host=panel(tr('calendar.title'));v.root=host;
 const choose=el('select',{'aria-label':tr('calendar.month')},...[...new Set([month,calendarMonth(),...(monthlyTraffic?.months||[])])].sort().reverse().map(m=>el('option',{value:m},m)));choose.value=month;
 const change=()=>{viewGeneration++;v.key='';renderKey='';refresh(true);};choose.onchange=()=>{v.month=choose.value;change();};
 const move=delta=>{const d=new Date(Date.UTC(year,number-1+delta,1));if(d.getUTCFullYear()<1970||d.getUTCFullYear()>9998)return;v.month=d.getUTCFullYear()+'-'+String(d.getUTCMonth()+1).padStart(2,'0');change();};
 const family=el('select',{'aria-label':tr('calendar.family')},...['all','4','6'].map(id=>el('option',{value:id},id==='all'?tr('range.all'):'IPv'+id))),direction=el('select',{'aria-label':tr('calendar.direction')},...['total','up','down'].map(id=>el('option',{value:id},tr('calendar.'+id))));family.value=v.family;direction.value=v.direction;
 const drawing=el('div',{class:'traffic-calendar-body'});const draw=()=>{
  drawing.replaceChildren();const cells=PulseCalendar.cells(year,number),records=cells.map(c=>({...c,date:month+'-'+String(c.day).padStart(2,'0')})).map(c=>({...c,...PulseCalendar.value(data.get(c.date),v.family,v.direction)})),maximum=Math.max(0,...records.map(c=>c.bytes||0)),grid=el('div',{class:'traffic-day-grid',role:'group','aria-label':tr('calendar.title')}),weekdays=el('div',{class:'traffic-weekdays'}),dark=document.documentElement.dataset.theme==='dark';
  grid.style.setProperty('--weeks',String(Math.max(...cells.map(c=>c.column))+1));for(let i=0;i<7;i++)weekdays.append(el('small',{},new Intl.DateTimeFormat(PulseI18n.locale,{weekday:'short',timeZone:'UTC'}).format(new Date(Date.UTC(2024,0,1+i)))));
  for(const c of records){const status=c.reason==='incomparable_bases'?tr('ui.013c99479178'):tr('calendar.'+c.state),label=c.date+' · '+(c.bytes===null?status:fmt(c.bytes)+' B · '+status),cell=el('button',{type:'button',class:'traffic-day '+c.state,title:label,'aria-label':label},String(c.day));cell.style.gridColumn=String(c.column+1);cell.style.gridRow=String(c.row+1);if(c.bytes!==null)cell.style.backgroundColor=PulseViz.css(PulseCalendar.color(c.bytes,maximum,dark));if(c.bytes!==null)cell.style.color=PulseViz.foreground(PulseCalendar.color(c.bytes,maximum,dark));grid.append(cell);}
  const legend=el('div',{class:'traffic-calendar-legend'},...['missing','zero','partial'].map(k=>el('span',{},el('i',{class:k}),tr('calendar.'+k))),el('span',{},tr('calendar.max')+' '+(records.some(c=>c.bytes!==null)?bytes(maximum):'—'))),notes=el('div',{class:'traffic-calendar-notes'},el('strong',{},month),el('p',{},tr(v.family==='all'?'calendar.octets':'calendar.ip')),el('p',{class:'muted'},tr('calendar.persisted')),el('small',{},v.result?.windows_timezone||''),legend);
  drawing.append(el('div',{class:'traffic-calendar-matrix'},weekdays,grid),notes);
 };
 family.onchange=()=>{v.family=family.value;draw();};direction.onchange=()=>{v.direction=direction.value;draw();};host.append(el('div',{class:'actions'},el('button',{class:'secondary',onclick:()=>move(-1),'aria-label':tr('ui.f9e727223456')},'‹'),choose,el('button',{class:'secondary',onclick:()=>move(1),'aria-label':tr('ui.3e0b634699d4')},'›'),family,direction),drawing);draw();container.append(host);
}
