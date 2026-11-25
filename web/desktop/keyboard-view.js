'use strict';
let heatmapName='arctic', selectedPhysicalKey=null;
const keyboardScalePreference='pulsetrace.keyboard.scale';
const keyboardLayoutPreference='pulsetrace.keyboard.layout';

function keyboardRanking(counts,top){
 const live=document.querySelector('[data-disclosure="keyboard.ranking"]');
 if(live)disclosureState.set('keyboard.ranking',{...disclosureState.get('keyboard.ranking'),open:live.open});
 const ranking=disclosure('keyboard.ranking',tr("ui.afb1428869e9"),null,pagedTable('keyboard.ranking.rows',[tr("ui.ff669c9b40a3"),tr("ui.edbd5432c859"),tr("ui.05c518eb94a0")],Object.entries(counts).sort((a,b)=>b[1]-a[1]),([id,n])=>[keyLabel(Number(id)),id,fmt(n)]));
 ranking.querySelector('summary').append(el('small',{},top?keyLabel(Number(top[0]))+' · '+fmt(top[1]):'—'));
 return ranking;
}
function renderKeyboard(c){
 const counts={};for(const r of rows)if(r.module==='keyboard')counts[r.metric]=(counts[r.metric]||0)+r.value;
 const total=sum('keyboard'),maximum=Math.max(0,...PulseKeyboard.keys.map(k=>counts[k.id]||0));
 const hasCoverage=sum('coverage','observed_ms')>0||Object.keys(counts).length>0;
 const top=Object.entries(counts).sort((a,b)=>b[1]-a[1])[0];
 const unique=card(tr('ui.4259deb9a5a6'),fmt(Object.keys(counts).length));
 c.append(el('div',{class:'cards dashboard-cards'},card(tr("ui.db4534c1cf62"),fmt(total),tr("ui.25e86d15f9d4")),card(tr("ui.3f9edb5674d2"),top?keyLabel(Number(top[0])):'—',top?fmt(top[1])+tr("ui.f81526efce19"):tr("ui.46ca86da52b3")),unique));
 const viewport=el('div',{class:'keyboard-heat keyboard-viewport'}),stage=el('div',{class:'keyboard-stage'}),board=el('div',{class:'keyboard-board',role:'group'}),detail=el('div',{class:'key-detail',role:'status'});
 const countLabel=value=>hasCoverage?fmt(value)+tr('ui.f81526efce19'):tr('ui.da87c243aa0a');
 const drawDetail=id=>{
  selectedPhysicalKey=id;
  const value=counts[id]||0;
  detail.dataset.key=String(id);detail.title=tr("ui.cb639ef3b8a9")+id;
  detail.replaceChildren(el('strong',{},keyLabel(id)),el('span',{},hasCoverage?countLabel(value):tr("ui.46ca86da52b3")),el('span',{class:'muted'},hasCoverage&&total?(value/total*100).toFixed(2)+'%':'—'));
  for(const key of board.children)key.setAttribute('aria-pressed',String(Number(key.dataset.key)===id));
 };
 const drawBoard=(layout,mode,light)=>{
  board.replaceChildren();
  for(const k of layout.keys){
   const value=counts[k.id]||0,kind=PulseViz.heatState(hasCoverage,value),label=keyLabel(k.id);
   const key=el('button',{class:'cap '+kind,'data-key':k.id,'data-group':k.group,type:'button',title:label+' · '+countLabel(value),'aria-label':label+' · '+countLabel(value),'aria-pressed':'false',onclick:()=>drawDetail(k.id)},el('span',{},label),el('small',{},hasCoverage?fmt(value):'—'));
   key.style.left=(16+k.x*46)+'px';key.style.top=(16+k.y*46)+'px';key.style.width=(k.w*46-4)+'px';key.style.height=(k.h*46-4)+'px';
   if(kind!=='missing'){const rgb=PulseViz.color(heatmapName,value,maximum,mode,light);key.style.backgroundColor=PulseViz.css(rgb);key.style.color=PulseViz.foreground(rgb);}
   board.append(key);
  }
 };
 stage.append(board);viewport.append(stage);
 const layoutOptions=['ansi','tkl'],scaleOptions=['linear','log'];
 const select=(label,values,option)=>el('select',{'aria-label':tr(label)},values.map(value=>el('option',{value},option(value))));
 const chooser=select('ui.70988398e6eb',['arctic','viridis','inferno','magma','cividis'],name=>name==='arctic'?tr('keyboard.arctic'):name[0].toUpperCase()+name.slice(1));
 const scale=select('keyboard.scale',scaleOptions,value=>tr('keyboard.'+value));
 const layout=select('keyboard.layout',layoutOptions,value=>tr('keyboard.'+value));
 chooser.value=heatmapName;
 scale.value=PulseDate.preference(keyboardScalePreference,'linear',scaleOptions);
 layout.value=PulseDate.preference(keyboardLayoutPreference,'ansi',layoutOptions);
 const legend=el('div',{class:'heat-legend arctic-legend',tabindex:'0'});
 const fit=()=>{
  if(!viewport.isConnected)return;
  const width=PulseKeyboard.layouts[layout.value].width*46+32,height=PulseKeyboard.height*46+32;
  const ratio=Math.min(1,Math.max(.9,viewport.clientWidth/width));
  stage.style.width=(width*ratio)+'px';stage.style.height=(height*ratio)+'px';
  board.style.width=width+'px';board.style.height=height+'px';board.style.transform='scale('+ratio+')';
 };
 const redraw=()=>{
  const light=document.documentElement.dataset.theme==='light',mode=scale.value,current=PulseKeyboard.layouts[layout.value];
  const layoutName=tr('keyboard.'+layout.value);
  unique.querySelector('small').textContent=layoutName;
  board.setAttribute('aria-label',layoutName);
  drawBoard(current,mode,light);fit();
  if(selectedPhysicalKey!==null&&current.keys.some(key=>key.id===selectedPhysicalKey))drawDetail(selectedPhysicalKey);
  else {detail.removeAttribute('data-key');detail.removeAttribute('title');detail.replaceChildren(el('span',{class:'muted'},tr('ui.53cdabcd167c')));}
  const ramp=el('div',{class:'heat-ramp','aria-label':tr('ui.9f7bd5bef4e5')});
  const samples=Array.from({length:257},(_,i)=>PulseViz.css(PulseViz.rampColor(heatmapName,i/256,light)));
  ramp.style.background='linear-gradient(to right,'+samples.join(',')+')';
  const zero=el('i',{class:'zero-swatch'});
  zero.style.backgroundColor=PulseViz.css(PulseViz.color(heatmapName,0,maximum,mode,light));
  const tick=t=>hasCoverage?fmt(PulseViz.heatTick(t,maximum,mode))+tr('ui.f81526efce19'):'—';
  legend.title=tr('keyboard.'+mode)+tr('ui.236b68e2a080');
  legend.replaceChildren(el('div',{class:'heat-scale'},el('span',{},tr('ui.cbcc08419d9e')),el('span',{},tr('ui.306b3910c5a7')+tick(.5)),el('span',{},tr('ui.352fb34f4ae1')+tick(1))),ramp,
    el('div',{class:'heat-states'},el('span',{},el('i',{class:'missing-swatch'}),tr('ui.58a9ae1b8c01')),el('span',{},zero,tr('ui.0ce726400a2f'))));
 };
 chooser.onchange=()=>{heatmapName=chooser.value;redraw();};
 scale.onchange=()=>{PulseDate.savePreference(keyboardScalePreference,scale.value);redraw();};
 layout.onchange=()=>{PulseDate.savePreference(keyboardLayoutPreference,layout.value);redraw();};
 const keyboard=panel(tr('keyboard.heatmap'),el('div',{class:'heat-toolbar'},layout,scale,chooser),viewport,legend,detail);
 keyboard.classList.add('keyboard-panel');
 c.append(keyboard);redraw();
 chartObservers.push(PulseRender.observe(viewport,fit));
 c.append(trend('keyboard',null,tr("ui.5ab2a33a8977")),keyboardRanking(counts,top));
}
