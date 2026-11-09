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
 svg.append(shape('path',{class:'mouse-body',d:'M135 18 C78 18 48 57 48 119 L48 231 C48 293 78 330 135 330 C192 330 222 293 222 231 L222 119 C222 57 192 18 135 18Z'}));
 const paths={left:'M114 31 C78 39 59 70 59 119 L59 141 Q59 152 70 152 L112 152 Q120 152 120 144 L120 39 Q120 30 114 31Z',right:'M156 31 C192 39 211 70 211 119 L211 141 Q211 152 200 152 L158 152 Q150 152 150 144 L150 39 Q150 30 156 31Z',middle:'M135 62 Q144 62 144 71 L144 111 Q144 120 135 120 Q126 120 126 111 L126 71 Q126 62 135 62Z',mouse5:'M37 143 Q43 143 43 149 L43 180 Q43 186 37 186 Q31 186 31 180 L31 149 Q31 143 37 143Z',mouse4:'M37 196 Q43 196 43 202 L43 233 Q43 239 37 239 Q31 239 31 233 L31 202 Q31 196 37 196Z'};
 const names=Object.fromEntries(mouseButtons()),detail=el('p',{class:'mouse-detail',role:'status'},data.has?tr('mouse.legend',{count:fmt(data.max)}):tr('mouse.no_data'));
 for(const b of data.buttons){
  const rgb=PulseViz.color('arctic',b.count,data.max),title=tr('mouse.share',{name:names[b.id],count:fmt(b.count),share:b.share===null?'—':(b.share*100).toFixed(1)+'%'}),node=shape('path',{d:paths[b.id],class:'mouse-button '+b.state,'data-button':b.id,tabindex:'0',role:'img','aria-label':data.has?title:names[b.id]+' · '+tr('mouse.no_data')});
  if(b.count>0)node.style.fill=PulseViz.css(rgb);node.append(shape('title',{}));node.firstChild.textContent=node.getAttribute('aria-label');node.addEventListener('focus',()=>detail.textContent=node.getAttribute('aria-label'));node.addEventListener('mouseenter',()=>detail.textContent=node.getAttribute('aria-label'));svg.append(node);
 }
 diagram.append(svg,detail);
 for(const b of [...data.buttons].sort((a,b)=>b.count-a.count)){
  const track=el('div',{class:'mouse-share-track'},el('i'));track.firstChild.style.width=((b.share||0)*100)+'%';if(data.has)track.firstChild.style.backgroundColor=PulseViz.css(PulseViz.color('arctic',b.count,data.max));
  ranking.append(el('div',{class:'mouse-rank',title:data.has?tr('mouse.share',{name:names[b.id],count:fmt(b.count),share:(100*(b.share||0)).toFixed(1)+'%'}):tr('mouse.no_data')},el('div',{class:'mouse-rank-heading'},el('span',{},names[b.id]),el('strong',{},data.has?fmt(b.count):'—'),el('small',{},b.share===null?'—':(b.share*100).toFixed(1)+'%')),track));
 }
 const legend=el('div',{class:'mouse-color-scale'});legend.style.background='linear-gradient(to right,'+Array.from({length:65},(_,i)=>PulseViz.css(PulseViz.arctic(i/64).map(Math.round))).join(',')+')';
 legend.title=tr('mouse.mapping');legend.tabIndex=0;ranking.append(legend,el('small',{},data.has?tr('mouse.legend',{count:fmt(data.max)}):tr('mouse.no_data')));host.append(layout);container.append(host);
}
