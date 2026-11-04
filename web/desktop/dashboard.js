'use strict';
let comparisonRows=[],comparisonRange='',comparisonFetched=0,liveRates=[],lastRateStamp=0;
const metricPaths={keyboard:'M3 5h18v14H3z M6 9h1m3 0h1m3 0h1m3 0h1M6 13h1m3 0h1m3 0h1m3 0h1M8 16h8',mouse:'M12 3a6 6 0 0 0-6 6v6a6 6 0 0 0 12 0V9a6 6 0 0 0-6-6z M12 3v7M6 10h12',wheel:'M12 3v18M8 7l4-4 4 4M8 17l4 4 4-4',time:'M21 12a9 9 0 1 0-18 0 9 9 0 0 0 18 0M12 7v5l3 2',network:'M7 3v18M3 7l4-4 4 4M17 21V3m-4 14 4 4 4-4',protocol:'M3 12h18M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18M12 3c-5 4-5 14 0 18M12 3c5 4 5 14 0 18'};
function metricIcon(kind){const svg=document.createElementNS('http://www.w3.org/2000/svg','svg'),path=document.createElementNS(svg.namespaceURI,'path');svg.setAttribute('viewBox','0 0 24 24');svg.setAttribute('aria-hidden','true');path.setAttribute('d',metricPaths[kind]);svg.append(path);return svg;}
function duration(ms){const m=Math.floor(ms/60000);return m>=60?Math.floor(m/60)+' h '+m%60+' m':m+' m';}
function watchCanvas(canvas,height,draw){const paint=()=>{if(!canvas.isConnected)return;const w=canvas.clientWidth;if(!w)return;const ratio=Math.min(devicePixelRatio||1,2);canvas.width=Math.round(w*ratio);canvas.height=Math.round(height*ratio);const ctx=canvas.getContext('2d');ctx.scale(ratio,ratio);draw(ctx,w,height,getComputedStyle(document.documentElement));};requestAnimationFrame(paint);const observer=new ResizeObserver(paint);observer.observe(canvas);chartObservers.push(observer);}
function sparkline(datasets){
 const canvas=el('canvas',{class:'sparkline','aria-label':'真实采样趋势'});
 if(!datasets.some(d=>d.points.length))return el('div',{class:'spark-empty'},'暂无趋势');
 watchCanvas(canvas,64,(ctx,w,h,style)=>{const all=datasets.flatMap(d=>d.points),x0=Math.min(...all.map(p=>p[0])),x1=Math.max(...all.map(p=>p[0])),max=Math.max(1,...all.map(p=>p[1]));
  for(const d of datasets){const color=style.getPropertyValue(d.color).trim();ctx.strokeStyle=color;ctx.fillStyle=color;ctx.lineWidth=2;ctx.lineJoin='round';ctx.setLineDash(d.dash||[]);let last=null;
   for(const p of d.points){const x=4+(p[0]-x0)*(w-8)/Math.max(d.step||1,x1-x0),y=h-8-p[1]/max*(h-18);if(!last||p[0]-last[0]>(d.step||1)*1.5){ctx.beginPath();ctx.moveTo(x,y);}else{ctx.lineTo(x,y);ctx.stroke();ctx.beginPath();ctx.moveTo(x,y);}ctx.beginPath();ctx.arc(x,y,1.7,0,Math.PI*2);ctx.fill();ctx.beginPath();ctx.moveTo(x,y);last=p;}
  }
 });return canvas;
}
function rememberRates(){const n=state.network||{},stamp=Number(state.heartbeat_ms||Date.now()),id=n.selected;
 if(liveRates.length&&liveRates[0].id!==id){liveRates=[];lastRateStamp=0;}
 if(!state.monitoring||state.network_error){lastRateStamp=0;return;}
 if(stamp<=lastRateStamp)return;lastRateStamp=stamp;
 liveRates.push({id,t:stamp/1000,up:Number(n.upload_bps||0),down:Number(n.download_bps||0)});if(liveRates.length>40)liveRates.shift();
}
function kpi(kind,title,value,side=null,foot=null){return el('article',{class:'kpi kpi-'+kind},el('div',{class:'kpi-head'},metricIcon(kind),el('span',{},title)),el('div',{class:'kpi-body'},el('div',{class:'kpi-number'},value),side),foot?el('div',{class:'kpi-foot'},foot):null);}
function inputActivityChart(){
 const points=PulseDashboard.inputs(rows,range.bucket),host=panel('输入活动趋势'),legend=el('div',{class:'chart-legend'},el('span',{},el('i',{class:'key-mark'}),'键盘'),el('span',{},el('i',{class:'mouse-mark'}),'鼠标'));
 host.classList.add('input-chart');host.prepend(el('div',{class:'chart-heading'},host.querySelector('h3'),legend));host.append(el('p',{class:'muted chart-unit'},'次数 / 桶 · 本地时间'));
 if(!points.length){host.append(el('div',{class:'chart-empty'},state.monitoring?'暂无数据':'已暂停 · 暂无数据'));return host;}
 const canvas=el('canvas',{'aria-label':'键盘与鼠标聚合次数柱状图'});host.append(canvas);
 watchCanvas(canvas,180,(ctx,w,h,style)=>{const first=points[0].minute,last=points.at(-1).minute,step=points[0].step,left=38,right=w-14,top=14,bottom=h-34,span=Math.max(step,last-first+step),max=Math.max(1,...points.flatMap(p=>[p.keyboard||0,p.mouse||0]));
  ctx.font='11px Segoe UI';ctx.fillStyle=style.getPropertyValue('--muted');ctx.strokeStyle=style.getPropertyValue('--chart-grid');
  for(let i=0;i<5;i++){const y=top+(bottom-top)*i/4;ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(right,y);ctx.stroke();ctx.fillText(fmt(Math.round(max*(4-i)/4)),0,y+4);}
  const slot=(right-left)*step/span,bw=Math.max(1,Math.min(10,slot*.29));
  for(const p of points){const x=left+(p.minute-first+step*.5)*(right-left)/span;
   for(const [key,color,offset]of [['keyboard','--data-keyboard',-bw-1],['mouse','--input-mouse',1]])if(p[key]!=null){const bh=p[key]/max*(bottom-top);ctx.fillStyle=style.getPropertyValue(color);ctx.fillRect(x+offset,bottom-bh,bw,bh);}}
  if(first===last){ctx.fillStyle=style.getPropertyValue('--muted');ctx.textAlign='center';ctx.fillText(new Date(first*60000).toLocaleTimeString('zh-CN',{hour:'2-digit',minute:'2-digit'}),(left+right)/2,h-9);return;}
  for(let i=0;i<4;i++){const minute=first+(last-first)*i/3,x=left+(minute-first)*(right-left)/span;ctx.fillStyle=style.getPropertyValue('--muted');ctx.textAlign=i===3?'right':'left';ctx.fillText(new Date(minute*60000).toLocaleTimeString('zh-CN',{hour:'2-digit',minute:'2-digit'}),i===3?right:x,h-9);}
 });return host;
}
function renderDashboard(c){
 rememberRates();const d=PulseDashboard,n=state.network||{},clicks=d.total(rows,'mouse',d.mouseKeys),up=d.total(rows,'mouse',['wheel_up_events']),down=d.total(rows,'mouse',['wheel_down_events']),wheel=up+down,coverage=d.total(rows,'coverage',['observed_ms']),ip=d.protocol(protocolSelection());
 const mouseValues=[d.total(rows,'mouse',['left']),d.total(rows,'mouse',['right']),d.total(rows,'mouse',['middle','mouse4','mouse5'])],ratios=d.distribution(mouseValues),wheelRatio=d.distribution([up,down]),change=d.comparison(rows,comparisonRows);
 const mouseSide=el('div',{class:'distribution'},el('div',{class:'stack-track'},...(ratios?ratios.map((r,i)=>{const part=el('i',{class:['mouse-left','mouse-right','mouse-other'][i]});part.style.width=r*100+'%';return part;}):[])),...['左键','右键','其他'].map((name,i)=>el('div',{class:'ratio-line'},el('span',{},el('i',{class:['mouse-left','mouse-right','mouse-other'][i]}),name),el('b',{},ratios?(ratios[i]*100).toFixed(0)+'%':'—'))));
 const wheelSide=el('div',{class:'wheel-ratios'},...['↑','↓'].map((arrow,i)=>{const track=el('div',{class:'ratio-track'},el('i'));if(wheelRatio)track.firstChild.style.width=wheelRatio[i]*100+'%';return el('div',{},el('span',{},arrow),track,el('b',{},wheelRatio?(wheelRatio[i]*100).toFixed(0)+'%':'—'));}));
 const keyboardSeries=d.series(rows,'keyboard'),keyboardSpark=sparkline([{points:PulseViz.aggregate(keyboardSeries,40,range.bucket),step:PulseViz.binWidth(keyboardSeries,40,range.bucket),color:'--data-keyboard'}]);
 const rateHistory=state.monitoring&&!state.network_error?liveRates:[];const rateSeries=rateHistory.map(r=>[r.t,r.up]),rateDown=rateHistory.map(r=>[r.t,r.down]),rateSpark=sparkline([{points:rateSeries,step:4.5,color:'--chart-upload'},{points:rateDown,step:4.5,color:'--chart-download',dash:[4,3]}]);
 const rateValue=el('div',{class:'network-values'},el('span',{},el('em',{},'↑'),state.network_error?'读取失败':state.monitoring?bytes(n.upload_bps||0)+'/s':'已暂停'),el('span',{},el('em',{},'↓'),state.network_error?'读取失败':state.monitoring?bytes(n.download_bps||0)+'/s':'已暂停'));
 const ipValues=el('div',{class:'ip-values'},el('span',{},el('i',{class:'ipv4-dot'}),'IPv4 ',el('b',{},ip.has?bytes(ip.bytes[0]):'—')),el('span',{},el('i',{class:'ipv6-dot'}),'IPv6 ',el('b',{},ip.has?bytes(ip.bytes[1]):'—')));
 let ring=null;if(ip.ratio){ring=el('div',{class:'ip-ring',role:'img','aria-label':'IPv4 观测占比 '+(ip.ratio[0]*100).toFixed(1)+'%'},el('div',{},el('b',{},(ip.ratio[0]*100).toFixed(0)+'%'),el('small',{},'IPv4 观测占比')));ring.style.setProperty('--ip-share',ip.ratio[0]*100+'%');}
 const comparison=change==null?'所选时段':el('span',{class:change>=0?'positive':'negative',title:'与等长上期、相近监测覆盖时长比较'},'较上期 '+(change>=0?'+':'')+(change*100).toFixed(1)+'%');
 c.append(el('div',{class:'kpi-grid'},
  kpi('keyboard','键盘按下次数',fmt(d.total(rows,'keyboard')),keyboardSpark,comparison),kpi('mouse','鼠标点击次数',fmt(clicks),mouseSide,'所选时段'),kpi('wheel','滚轮事件次数',fmt(wheel),wheelSide,'事件数 · 不含滚动量'),
  kpi('time','监测时长',duration(coverage),el('div',{class:'time-glyph'},metricIcon('time')),'监测覆盖 · 非活跃时长'),kpi('network','网络速率 · 实时',rateValue,rateSpark,'↑ 上传　↓ 下载'),kpi('protocol','IPv4 / IPv6 流量',ipValues,ring,el('span',{class:'quality-badge'},ip.quality+' · ETW 观测'))));
 if(state.network_error)c.append(el('div',{class:'capture-error',role:'alert'},'网络读取失败：'+state.network_error));
 c.append(el('div',{class:'dashboard-charts'},inputActivityChart(),protocolTrend('网络流量趋势')));
 const selected=(n.interfaces||[]).find(x=>x.id===n.selected),summary=el('div',{class:'daily-summary'},...[
  ['按键',fmt(d.total(rows,'keyboard'))],['点击',fmt(clicks)],['滚轮',fmt(wheel)],['IP 观测',ip.has?bytes(ip.bytes[0]+ip.bytes[1]):'—'],['监测时长',duration(coverage)]
 ].map(([label,value])=>el('div',{},el('span',{},label),el('strong',{},value))));
 c.append(el('div',{class:'dashboard-bottom'},panel('时段摘要',summary),panel('连接状态',el('div',{class:'connection-current'},el('i',{class:selected?.up?'connected':'disconnected'}),selected?.name||'未选择接口',el('span',{class:'muted'},selected?.up?'已连接':'未连接')),el('div',{class:'connection-history'},...(networkEvents.length?networkEvents.slice(0,2).map(e=>el('div',{},el('time',{},new Date(e.observed_ms).toLocaleTimeString('zh-CN',{hour:'2-digit',minute:'2-digit'})),el('span',{},e.name||e.interface_id),el('b',{},connectionLabel(e.kind)))):[el('p',{class:'muted'},'暂无切换记录')])))));
 const monthly=el('details',{class:'monthly-drawer'},el('summary',{},'月度累计'));renderMonthlyTraffic(monthly);c.append(monthly);
}
