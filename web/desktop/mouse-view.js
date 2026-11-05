'use strict';
// Raw Input identities are unchanged: XBUTTON1 = Mouse4/back, XBUTTON2 = Mouse5/forward.
globalThis.PulseMouse = (() => {
 const ids=['left','right','middle','mouse4','mouse5'];
 function model(values){
  const counts=Object.fromEntries(ids.map(id=>[id,values.filter(r=>r.module==='mouse'&&r.metric===id).reduce((n,r)=>n+r.value,0)]));
  const total=Object.values(counts).reduce((n,v)=>n+v,0),max=Math.max(...Object.values(counts)),has=values.some(r=>r.module==='coverage'&&r.value>0)||total>0;
  return {has,total,max,buttons:ids.map(id=>({id,count:counts[id],share:total?counts[id]/total:null,state:PulseViz.heatState(has,counts[id])}))};
 }
 return {model};
})();
function renderMouseDiagram(container){
 const data=PulseMouse.model(rows),host=panel(tr('mouse.diagram')),diagram=el('div',{class:'mouse-diagram'}),ranking=el('div',{class:'mouse-ranking'}),layout=el('div',{class:'mouse-layout'},diagram,ranking);
 const ns='http://www.w3.org/2000/svg',svg=document.createElementNS(ns,'svg');svg.setAttribute('viewBox','0 0 270 350');svg.setAttribute('role','group');svg.setAttribute('aria-label',tr('mouse.diagram'));
 const shape=(tag,attrs)=>{const n=document.createElementNS(ns,tag);for(const [k,v]of Object.entries(attrs))n.setAttribute(k,v);return n;};
 svg.append(shape('path',{class:'mouse-body',d:'M136 15 C70 15 47 55 47 122 L47 231 C47 305 77 335 137 335 C199 335 229 303 229 231 L229 122 C229 53 204 15 136 15Z'}));
 const paths={left:'M130 25 C77 26 58 62 58 120 L58 150 L130 150Z',right:'M146 25 C198 27 218 61 218 120 L218 150 L146 150Z',middle:'M135 66 Q135 57 141 57 Q147 57 147 66 L147 111 Q147 120 141 120 Q135 120 135 111Z',mouse5:'M35 143 Q25 143 25 152 L25 185 Q25 193 35 193 L43 189 L43 146Z',mouse4:'M35 201 Q25 201 25 210 L25 243 Q25 251 35 251 L43 245 L43 204Z'};
 const names=Object.fromEntries(mouseButtons()),detail=el('p',{class:'mouse-detail',role:'status'},data.has?tr('mouse.legend',{count:fmt(data.max)}):tr('mouse.no_data'));
 for(const b of data.buttons){
  const rgb=PulseViz.color('arctic',b.count,data.max),title=tr('mouse.share',{name:names[b.id],count:fmt(b.count),share:b.share===null?'—':(b.share*100).toFixed(1)+'%'}),node=shape('path',{d:paths[b.id],class:'mouse-button '+b.state,'data-button':b.id,tabindex:'0',role:'img','aria-label':data.has?title:names[b.id]+' · '+tr('mouse.no_data')});
  if(data.has)node.style.fill=PulseViz.css(rgb);node.append(shape('title',{}));node.firstChild.textContent=node.getAttribute('aria-label');node.addEventListener('focus',()=>detail.textContent=node.getAttribute('aria-label'));node.addEventListener('mouseenter',()=>detail.textContent=node.getAttribute('aria-label'));svg.append(node);
 }
 const wheelLabel=shape('text',{x:141,y:137,'text-anchor':'middle',class:'mouse-wheel-label'});wheelLabel.textContent=tr('mouse.wheel');svg.append(wheelLabel);diagram.append(svg,detail);
 for(const b of [...data.buttons].sort((a,b)=>b.count-a.count)){
  const track=el('div',{class:'mouse-share-track'},el('i'));track.firstChild.style.width=((b.share||0)*100)+'%';if(data.has)track.firstChild.style.backgroundColor=PulseViz.css(PulseViz.color('arctic',b.count,data.max));
  ranking.append(el('div',{class:'mouse-rank',title:data.has?tr('mouse.share',{name:names[b.id],count:fmt(b.count),share:(100*(b.share||0)).toFixed(1)+'%'}):tr('mouse.no_data')},el('div',{class:'mouse-rank-heading'},el('span',{},names[b.id]),el('strong',{},data.has?fmt(b.count):'—'),el('small',{},b.share===null?'—':(b.share*100).toFixed(1)+'%')),track));
 }
 const legend=el('div',{class:'mouse-color-scale'});legend.style.background='linear-gradient(to right,'+Array.from({length:65},(_,i)=>PulseViz.css(PulseViz.arctic(i/64).map(Math.round))).join(',')+')';
 ranking.append(el('p',{class:'muted'},tr('mouse.mapping')),legend,el('small',{},data.has?tr('mouse.legend',{count:fmt(data.max)}):tr('mouse.no_data')));host.append(layout);container.append(host);
}
