'use strict';
let heatmapName='arctic', selectedPhysicalKey=null;
function renderKeyboard(c){
 const counts={};for(const r of rows)if(r.module==='keyboard')counts[r.metric]=(counts[r.metric]||0)+r.value;
 const layout=PulseKeyboard.keys,maximum=Math.max(0,...layout.map(k=>counts[k.id]||0)),hasCoverage=sum('coverage','observed_ms')>0||Object.keys(counts).length>0;
 const top=Object.entries(counts).sort((a,b)=>b[1]-a[1])[0];
 c.append(el('div',{class:'cards dashboard-cards'},card('总按键次数',fmt(sum('keyboard')),'真实按下聚合 · 不含自动重复'),card('最高频按键',top?keyLabel(Number(top[0])):'—',top?fmt(top[1])+' 次':'所选范围暂无记录'),card('使用键位',fmt(Object.keys(counts).length),'标准 ANSI 104 · 其他扫描码保留排名')));
 const viewport=el('div',{class:'keyboard-heat keyboard-viewport','aria-label':'ANSI 104 物理键盘布局'}),stage=el('div',{class:'keyboard-stage'}),board=el('div',{class:'keyboard-board',role:'group','aria-label':'ANSI 104 键盘'}),detail=el('div',{class:'key-detail',role:'status'});
 const drawDetail=id=>{selectedPhysicalKey=id;const k=layout.find(k=>k.id===id),value=counts[id]||0;detail.replaceChildren(el('strong',{},k?.label||keyLabel(id)),el('span',{},hasCoverage?fmt(value)+' 次':'无监测记录'),el('span',{class:'muted'},'扫描码 ID '+id+' · '+(hasCoverage&&sum('keyboard')?(value/sum('keyboard')*100).toFixed(2)+'%':'—')+' · '+formatTime(range.from)+' 至 '+formatTime(range.to)));
  for(const key of board.children)key.setAttribute('aria-pressed',String(Number(key.dataset.key)===id));};
 for(const k of layout){const value=counts[k.id]||0,kind=PulseViz.heatState(hasCoverage,value),key=el('button',{class:'cap '+kind,'data-key':k.id,'data-group':k.group,type:'button',title:k.label+' · '+(hasCoverage?fmt(value)+' 次':'无采集数据'),'aria-label':k.label+'，'+(hasCoverage?fmt(value)+' 次':'无采集数据'),'aria-pressed':'false',onclick:()=>drawDetail(k.id)},el('span',{},k.label),el('small',{},hasCoverage?fmt(value):'—'));
  key.style.left=(16+k.x*46)+'px';key.style.top=(16+k.y*46)+'px';key.style.width=(k.w*46-4)+'px';key.style.height=(k.h*46-4)+'px';
  if(kind!=='missing'){const rgb=PulseViz.color(heatmapName,value,maximum);key.style.backgroundColor=PulseViz.css(rgb);key.style.color=PulseViz.foreground(rgb);}board.append(key);
 }
 stage.append(board);viewport.append(stage);const fit=()=>{if(!viewport.isConnected)return;const width=PulseKeyboard.width*46+32,height=PulseKeyboard.height*46+32,scale=Math.min(1,Math.max(.9,viewport.clientWidth/width));stage.style.width=(width*scale)+'px';stage.style.height=(height*scale)+'px';board.style.width=width+'px';board.style.height=height+'px';board.style.transform='scale('+scale+')';};
 requestAnimationFrame(fit);const observer=new ResizeObserver(fit);observer.observe(viewport);chartObservers.push(observer);
 const chooser=el('select',{'aria-label':'热力图色图'},...['arctic','viridis','inferno','magma','cividis'].map(name=>el('option',{value:name},name==='arctic'?'Arctic · Custom Colormap':name[0].toUpperCase()+name.slice(1))));chooser.value=heatmapName;chooser.onchange=()=>{heatmapName=chooser.value;renderKey='';refresh(true);};
 const legend=el('div',{class:'heat-legend arctic-legend'}),ramp=el('div',{class:'heat-ramp','aria-label':'连续颜色标尺'}),arctic=heatmapName==='arctic';
 const samples=Array.from({length:257},(_,i)=>arctic?PulseViz.arctic(i/256).map(Math.round):PulseViz.color(heatmapName,i,256));
 // Gradient is uniform in normalized t; count tick marks invert sqrt(t).
 ramp.style.background='linear-gradient(to right,'+samples.map(PulseViz.css).join(',')+')';
 const zero=el('i',{class:'zero-swatch'});zero.style.backgroundColor=PulseViz.css(PulseViz.color(heatmapName,0,maximum));
 legend.append(el('div',{class:'heat-scale'},el('span',{},'低频 · 0 次'),el('span',{},'中频 · '+(hasCoverage?fmt(maximum*(arctic?.25:.5))+' 次':'—')),el('span',{},'高频 · '+(hasCoverage?fmt(maximum)+' 次':'—'))),ramp,el('p',{class:'muted'},(arctic?'Arctic · Custom Colormap · OKLab 连续插值 · 平方根归一化 √(次数 / 最大次数)':heatmapName+' · 官方色图 · 线性归一化')+'；每次查询独立标尺。'),el('p',{class:'muted'},formatTime(range.from)+' 至 '+formatTime(range.to)+'（结束不含） · 当前 ANSI 104 最大次数 '+(hasCoverage?fmt(maximum):'—')),el('div',{class:'heat-states'},el('span',{},el('i',{class:'missing-swatch'}),'无采集记录（—）'),el('span',{},zero,'有覆盖但零次（0）')));
 const keyboard=panel('ANSI 104 · 键盘热力图',el('div',{class:'heat-toolbar'},chooser),viewport,legend,detail);
 c.append(keyboard);if(selectedPhysicalKey!==null&&layout.some(k=>k.id===selectedPhysicalKey))drawDetail(selectedPhysicalKey);else detail.append(el('span',{class:'muted'},'悬停查看次数，点击任意键查看该键在所选范围内的统计。'));
 c.append(notice('仅保存按下计数，不保存文本、按键顺序或逐键时间戳。左右修饰键及主区 / 小键盘 Enter 独立映射；布局是物理位置示意，标签不代表实际输入内容。'),trend('keyboard',null,'键盘按下趋势'),panel('键位排名',table(['键位','扫描码 ID','次数'],Object.entries(counts).sort((a,b)=>b[1]-a[1]).slice(0,50).map(([id,n])=>[keyLabel(Number(id)),id,fmt(n)]))));
}
