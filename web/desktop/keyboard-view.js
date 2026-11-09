'use strict';
let heatmapName='arctic', selectedPhysicalKey=null;
function keyboardRanking(counts,top){
 const live=document.querySelector('[data-disclosure="keyboard.ranking"]');
 if(live)disclosureState.set('keyboard.ranking',{...disclosureState.get('keyboard.ranking'),open:live.open});
 const ranking=disclosure('keyboard.ranking',tr("ui.afb1428869e9"),null,pagedTable('keyboard.ranking.rows',[tr("ui.ff669c9b40a3"),tr("ui.edbd5432c859"),tr("ui.05c518eb94a0")],Object.entries(counts).sort((a,b)=>b[1]-a[1]),([id,n])=>[keyLabel(Number(id)),id,fmt(n)]));
 ranking.querySelector('summary').append(el('small',{},top?keyLabel(Number(top[0]))+' · '+fmt(top[1]):'—'));
 return ranking;
}
function renderKeyboard(c){
 const counts={};for(const r of rows)if(r.module==='keyboard')counts[r.metric]=(counts[r.metric]||0)+r.value;
 const layout=PulseKeyboard.keys,maximum=Math.max(0,...layout.map(k=>counts[k.id]||0)),hasCoverage=sum('coverage','observed_ms')>0||Object.keys(counts).length>0;
 const top=Object.entries(counts).sort((a,b)=>b[1]-a[1])[0];
 c.append(el('div',{class:'cards dashboard-cards'},card(tr("ui.db4534c1cf62"),fmt(sum('keyboard')),tr("ui.25e86d15f9d4")),card(tr("ui.3f9edb5674d2"),top?keyLabel(Number(top[0])):'—',top?fmt(top[1])+tr("ui.f81526efce19"):tr("ui.46ca86da52b3")),card(tr("ui.4259deb9a5a6"),fmt(Object.keys(counts).length),'ANSI 104')));
 const viewport=el('div',{class:'keyboard-heat keyboard-viewport','aria-label':tr("ui.c7e7db0f02c7")}),stage=el('div',{class:'keyboard-stage'}),board=el('div',{class:'keyboard-board',role:'group','aria-label':tr("ui.63680f52c043")}),detail=el('div',{class:'key-detail',role:'status'});
 const drawDetail=id=>{selectedPhysicalKey=id;const k=layout.find(k=>k.id===id),value=counts[id]||0;detail.dataset.key=String(id);detail.title=tr("ui.cb639ef3b8a9")+id;detail.replaceChildren(el('strong',{},keyLabel(id)),el('span',{},hasCoverage?fmt(value)+tr("ui.f81526efce19"):tr("ui.46ca86da52b3")),el('span',{class:'muted'},hasCoverage&&sum('keyboard')?(value/sum('keyboard')*100).toFixed(2)+'%':'—'));
  for(const key of board.children)key.setAttribute('aria-pressed',String(Number(key.dataset.key)===id));};
 for(const k of layout){const value=counts[k.id]||0,kind=PulseViz.heatState(hasCoverage,value),key=el('button',{class:'cap '+kind,'data-key':k.id,'data-group':k.group,type:'button',title:keyLabel(k.id)+' · '+(hasCoverage?fmt(value)+tr("ui.f81526efce19"):tr("ui.da87c243aa0a")),'aria-label':keyLabel(k.id)+'，'+(hasCoverage?fmt(value)+tr("ui.f81526efce19"):tr("ui.da87c243aa0a")),'aria-pressed':'false',onclick:()=>drawDetail(k.id)},el('span',{},keyLabel(k.id)),el('small',{},hasCoverage?fmt(value):'—'));
  key.style.left=(16+k.x*46)+'px';key.style.top=(16+k.y*46)+'px';key.style.width=(k.w*46-4)+'px';key.style.height=(k.h*46-4)+'px';
  if(kind!=='missing'){const rgb=PulseViz.color(heatmapName,value,maximum);key.style.backgroundColor=PulseViz.css(rgb);key.style.color=PulseViz.foreground(rgb);}board.append(key);
 }
 stage.append(board);viewport.append(stage);const fit=()=>{if(!viewport.isConnected)return;const width=PulseKeyboard.width*46+32,height=PulseKeyboard.height*46+32,scale=Math.min(1,Math.max(.9,viewport.clientWidth/width));stage.style.width=(width*scale)+'px';stage.style.height=(height*scale)+'px';board.style.width=width+'px';board.style.height=height+'px';board.style.transform='scale('+scale+')';};
 chartObservers.push(PulseRender.observe(viewport,fit));
 const chooser=el('select',{'aria-label':tr("ui.70988398e6eb")},...['arctic','viridis','inferno','magma','cividis'].map(name=>el('option',{value:name},name==='arctic'?'Arctic · Custom Colormap':name[0].toUpperCase()+name.slice(1))));chooser.value=heatmapName;chooser.onchange=()=>{heatmapName=chooser.value;renderKey='';refresh(true);};
 const legend=el('div',{class:'heat-legend arctic-legend'}),ramp=el('div',{class:'heat-ramp','aria-label':tr("ui.9f7bd5bef4e5")}),arctic=heatmapName==='arctic';
 const samples=Array.from({length:257},(_,i)=>arctic?PulseViz.arctic(i/256).map(Math.round):PulseViz.color(heatmapName,i,256));
 // Gradient is uniform in normalized t; count tick marks invert sqrt(t).
 ramp.style.background='linear-gradient(to right,'+samples.map(PulseViz.css).join(',')+')';
 const zero=el('i',{class:'zero-swatch'});zero.style.backgroundColor=PulseViz.css(PulseViz.color(heatmapName,0,maximum));
 legend.tabIndex=0;legend.title=(arctic?tr("ui.821848ed2dda"):tr("ui.4202f660a3c8"))+tr("ui.236b68e2a080")+" · "+formatTime(range.from)+tr("ui.8727f227e2c6")+formatTime(range.to);
 legend.append(el('div',{class:'heat-scale'},el('span',{},tr("ui.cbcc08419d9e")),el('span',{},tr("ui.306b3910c5a7")+(hasCoverage?fmt(maximum*(arctic?.25:.5))+tr("ui.f81526efce19"):'—')),el('span',{},tr("ui.352fb34f4ae1")+(hasCoverage?fmt(maximum)+tr("ui.f81526efce19"):'—'))),ramp,el('div',{class:'heat-states'},el('span',{},el('i',{class:'missing-swatch'}),tr("ui.58a9ae1b8c01")),el('span',{},zero,tr("ui.0ce726400a2f"))));

 const keyboard=panel(tr("ui.48b68cef28ad"),el('div',{class:'heat-toolbar'},chooser),viewport,legend,detail);
 c.append(keyboard);if(selectedPhysicalKey!==null&&layout.some(k=>k.id===selectedPhysicalKey))drawDetail(selectedPhysicalKey);else detail.append(el('span',{class:'muted'},tr("ui.53cdabcd167c")));
 c.append(trend('keyboard',null,tr("ui.5ab2a33a8977")),keyboardRanking(counts,top));
}
