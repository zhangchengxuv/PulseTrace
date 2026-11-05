'use strict';
const usageView={result:{rows:[]},recent:[],limit:10,sort:'active_ms',visible:100,selected:'',icons:new Map()};
const usageText=key=>tr('usage.'+key);
function usageDuration(ms){const minutes=Math.floor(ms/60000);if(ms<60000)return Math.floor(ms/1000)+' s';return minutes>=60?`${Math.floor(minutes/60)} h ${minutes%60} m`:`${minutes} m`;}
function groupUsage(values){const result=new Map();for(const r of values){if(!result.has(r.key))result.set(r.key,{...r,active_ms:0,foreground_ms:0});const v=result.get(r.key);v.active_ms+=r.active_ms;v.foreground_ms+=r.foreground_ms;}return [...result.values()];}
function usageBins(values,daily=false,metric='active_ms'){const bins=new Map();for(const r of values){const d=new Date(r.minute*60000);daily?d.setHours(0,0,0,0):d.setMinutes(0,0,0);const key=d.getTime()/60000;bins.set(key,(bins.get(key)||0)+r[metric]);}return [...bins].sort((a,b)=>a[0]-b[0]);}
async function refreshUsage(){
 const selected=await rpc('get_app_usage',{from:range.from,to:range.to});
 const d=new Date();d.setHours(0,0,0,0);d.setDate(d.getDate()-6);
 const recent=await rpc('get_app_usage',{from:d.getTime()/60000,to:Math.floor(Date.now()/60000)+1});
 usageView.result=selected;usageView.recent=recent.rows;
}
function usageChart(title,values,daily=false){
 const series=PulseViz.aggregate(usageBins(values,daily),96,daily?1440:60);
 const canvas=el('canvas',{'aria-label':title,role:'img'}),host=panel(title,el('p',{class:'muted'},usageText('chart_unit')));
 if(!series.length){host.append(el('div',{class:'chart-empty'},usageText('empty')));return host;}host.append(canvas);
 const draw=()=>{const width=canvas.clientWidth;if(!width)return;const height=180,dpr=devicePixelRatio||1;canvas.width=Math.round(width*dpr);canvas.height=Math.round(height*dpr);const ctx=canvas.getContext('2d');ctx.scale(dpr,dpr);const css=getComputedStyle(document.documentElement),max=Math.max(...series.map(r=>r[1]))||1,left=42,right=width-12,span=(right-left)/series.length;
  ctx.font='11px Segoe UI';ctx.fillStyle=css.getPropertyValue('--muted');ctx.fillText((max/60000).toFixed(1)+' m',0,16);ctx.strokeStyle=css.getPropertyValue('--chart-grid');ctx.beginPath();ctx.moveTo(left,150);ctx.lineTo(right,150);ctx.stroke();ctx.fillStyle=css.getPropertyValue('--data-time');for(let i=0;i<series.length;i++)ctx.fillRect(left+i*span+2,150-series[i][1]/max*120,Math.max(1,span-4),series[i][1]/max*120);
  ctx.fillStyle=css.getPropertyValue('--muted');ctx.textAlign='left';const label=m=>PulseI18n.date(m*60000,daily?{month:'short',day:'numeric'}:{month:'numeric',day:'numeric',hour:'2-digit',hourCycle:'h23'});ctx.fillText(label(series[0][0]),left,172);if(width>350&&series.length>1){ctx.textAlign='right';ctx.fillText(label(series.at(-1)[0]),right,172);}};
 requestAnimationFrame(draw);const observer=new ResizeObserver(draw);observer.observe(canvas);chartObservers.push(observer);return host;
}
function appUsage(c){
 const v=usageView,all=groupUsage(v.result.rows),todayRows=v.recent.filter(r=>r.minute>=today()),daily=groupUsage(todayRows),active=daily.reduce((n,r)=>n+r.active_ms,0),foreground=daily.reduce((n,r)=>n+r.foreground_ms,0),top=daily.sort((a,b)=>b.active_ms-a.active_ms||b.foreground_ms-a.foreground_ms)[0];
 c.append(el('div',{class:'cards'},card(usageText('today_active'),usageDuration(active)),card(usageText('today_foreground'),usageDuration(foreground)),card(usageText('most_used'),top?.name|| (top?usageText('unknown'):'—'))),notice(usageText('basis')));
 if(v.result.status?.error)c.append(notice(usageText('unavailable')));if(v.result.status?.gaps)c.append(notice(usageText('gaps')+': '+fmt(v.result.status.gaps)));
 const limit=el('select',{'aria-label':usageText('limit')},el('option',{value:10},'Top 10'),el('option',{value:0},usageText('all')));limit.value=String(v.limit);
 const sort=el('select',{'aria-label':usageText('sort')},el('option',{value:'active_ms'},usageText('active')),el('option',{value:'foreground_ms'},usageText('foreground')));sort.value=v.sort;
 const host=el('div',{class:'usage-ranking'}),draw=()=>{host.replaceChildren();const sorted=all.slice().sort((a,b)=>b[v.sort]-a[v.sort]||a.key.localeCompare(b.key)),total=all.reduce((n,r)=>n+r[v.sort],0);if(!sorted.length)host.append(el('p',{class:'muted'},usageText('empty')));
  for(const r of sorted.slice(0,v.limit||v.visible)){const share=total?r[v.sort]/total:0,img=el('img',{class:'usage-icon',alt:'',hidden:true}),name=el('button',{class:'usage-name',title:r.path||usageText('unknown'),onclick:()=>{v.selected=r.key;detailPage=0;details();}},r.name||usageText('unknown')),row=el('div',{class:'usage-row'},el('div',{class:'usage-row-head'},el('span',{},img,name),el('span',{},usageDuration(r[v.sort])+' · '+(total?(share*100).toFixed(1)+'%':'—'))),el('div',{class:'usage-track'},el('i')));row.querySelector('i').style.width=(share*100)+'%';row.title=usageText('active')+': '+usageDuration(r.active_ms)+' · '+usageText('foreground')+': '+usageDuration(r.foreground_ms);host.append(row);
   if(state.config?.usage_icons){const load=async()=>{if(!v.icons.has(r.key)){if(v.icons.size>=128)v.icons.clear();v.icons.set(r.key,(await rpc('get_usage_icon',{key:r.key,from:range.from,to:range.to})).icon);}const icon=v.icons.get(r.key);if(icon){img.src=icon;img.hidden=false;}};load().catch(()=>{});}
  }if(!v.limit&&sorted.length>v.visible)host.append(el('button',{class:'secondary',onclick:()=>{v.visible+=100;draw();}},tr('ranking.more')));
 };
 const detail=el('div');let detailPage=0;
 const details=()=>{detail.replaceChildren();if(!v.selected)return;const selected=v.result.rows.filter(r=>r.key===v.selected),name=selected[0]?.name||usageText('unknown');const contents=[];
  for(const daily of [true,false]){const active=usageBins(selected,daily),foreground=new Map(usageBins(selected,daily,'foreground_ms'));contents.push(table([usageText(daily?'day':'hour'),usageText('active'),usageText('foreground')],active.slice(detailPage*100,detailPage*100+100).map(([m,n])=>[daily?PulseI18n.date(m*60000,{year:'numeric',month:'2-digit',day:'2-digit'}):formatTime(m),usageDuration(n),usageDuration(foreground.get(m)||0)])));}
  const count=usageBins(selected).length;contents.push(el('div',{class:'actions'},el('button',{class:'secondary',disabled:detailPage===0,onclick:()=>{detailPage--;details();}},tr('ui.c9b9ae7a6144')),el('span',{},String(detailPage+1)),el('button',{class:'secondary',disabled:(detailPage+1)*100>=count,onclick:()=>{detailPage++;details();}},tr('ui.8a8542f69648'))));detail.append(panel(name,...contents));};
 limit.onchange=()=>{v.limit=Number(limit.value);draw();};sort.onchange=()=>{v.sort=sort.value;draw();};
 c.append(panel(usageText('ranking'),el('div',{class:'actions'},limit,sort,el('button',{class:'secondary',onclick:async()=>{try{await rpc('export_app_usage',{from:range.from,to:range.to});}catch(e){error(e);}}},usageText('export'))),host),detail,el('div',{class:'chart-grid'},usageChart(usageText('hourly'),v.result.rows),usageChart(usageText('weekly'),v.recent,true)));draw();details();
}
function renderUsageSettings(c){
 const cfg=state.config,enabled=el('input',{type:'checkbox',checked:cfg.usage_enabled}),idle=el('input',{type:'number',min:15,max:3600,value:cfg.usage_idle_seconds}),icons=el('input',{type:'checkbox',checked:cfg.usage_icons}),retention=el('input',{type:'number',min:1,max:365,value:cfg.usage_retention_days}),excluded=el('textarea',{rows:3,'aria-label':usageText('excluded')});excluded.value=(cfg.usage_exclusions||[]).join('\n');
 const form=el('div',{class:'settings-grid'}),feedback=el('p',{class:'muted',role:'status'});for(const [key,input]of [['enable',enabled],['idle',idle],['icons',icons],['retention',retention],['excluded',excluded]])form.append(el('label',{},el('strong',{},usageText(key)),input));
 form.append(el('button',{onclick:async()=>{try{await rpc('set_config',{usage_enabled:enabled.checked,usage_idle_seconds:Number(idle.value),usage_icons:icons.checked,usage_retention_days:Number(retention.value),usage_exclusions:excluded.value.split(/\r?\n/).map(s=>s.trim()).filter(Boolean)});state=await rpc('get_status');feedback.textContent=usageText('saved');}catch(e){error(e);}}},usageText('save')),feedback);
 c.append(panel(usageText('title'),form));
}
