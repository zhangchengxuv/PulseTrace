'use strict';
async function validateStaticInteractions(){
 const check=(value,message)=>{if(!value)throw new Error('Static UI: '+message);};
 check(!document.getElementById('eyebrow')&&!document.querySelector('aside .privacy'),'removed labels');
 const action=el('button',{disabled:false,onclick:()=>{action.dataset.clicked='true';}});action.click();
 check(!action.disabled&&action.dataset.clicked==='true','enabled button interaction');
 for(const node of document.querySelectorAll('nav button,h2,h3,.state,.kpi-head,.chart-legend,.card>span'))
  check(getComputedStyle(node).userSelect==='none','selectable static label');
 const probe=document.createElement('label');
 probe.innerHTML='<input value="12:34"><select><option>A</option><option>B</option></select><code>copy/path</code><span class="selectable-text">copy</span>';
 document.getElementById('content').append(probe);
 try{
  const input=probe.querySelector('input');input.focus();input.setSelectionRange(0,2);
  check(document.activeElement===input&&input.selectionStart===0&&input.selectionEnd===2,'input focus/selection');
  input.setRangeText('18',0,2,'end');check(input.value==='18:34','input editing');
  for(const n of probe.querySelectorAll('input,select,code,.selectable-text'))check(getComputedStyle(n).userSelect==='text','copy/edit escape hatch');
  const select=probe.querySelector('select');select.focus();select.selectedIndex=1;select.dispatchEvent(new Event('change',{bubbles:true}));
  check(document.activeElement===select&&select.value==='B'&&!select.disabled,'select operation');
 }finally{probe.remove();}
 if(page!=='settings'&&rangeFields){
  rangeFields.from.click();const picker=PulseDatePicker.current;
  check(picker?.dialog.open,'date button click');picker.hour.focus();picker.hour.select();
  check(document.activeElement===picker.hour&&getComputedStyle(picker.hour).userSelect==='text','date editor');
  picker.hour.setRangeText('18',0,picker.hour.value.length,'end');check(picker.hour.value==='18','date edit');picker.close();
 }
 // Range selection uses actual browser Selection, independent of styling.
 const copy=document.createElement('pre');copy.textContent='export/path';document.getElementById('content').append(copy);
 const range=document.createRange();range.selectNodeContents(copy);const selection=getSelection();selection.removeAllRanges();selection.addRange(range);
 check(selection.toString()==='export/path'&&getComputedStyle(copy).userSelect==='text','export copying');selection.removeAllRanges();copy.remove();
 window.pulseStaticInteractionReport={passed:true};
}
// Runs only in the explicit native --self-test process, never during ordinary monitoring.
async function validateScrollMaterial(){
 if(!state.self_test)return;
 const root=document.documentElement,toolbar=document.getElementById('dashboard-toolbar'),previous=root.dataset.transparency;
 const tick=()=>new Promise(resolve=>requestAnimationFrame(resolve));
 window.scrollTo({top:0,behavior:'instant'});await tick();await tick();
 await rpc('selftest_snapshot',{phase:'top'});
 const distance=Math.min(320,Math.max(0,root.scrollHeight-innerHeight)),frames=distance&&root.dataset.motion!=='reduced'?36:0,intervals=[];
 let last=performance.now();
 for(let i=1;i<=frames;i++){await tick();const now=performance.now();intervals.push(now-last);last=now;window.scrollTo({top:distance*i/frames,behavior:'instant'});}
 if(!frames&&distance)window.scrollTo({top:distance,behavior:'instant'});
 await tick();await tick();
 let top=0,blur=false;
 if(toolbar){const style=getComputedStyle(toolbar);top=toolbar.getBoundingClientRect().top;blur=style.backdropFilter!=='none'&&style.backdropFilter!=='';
  if(distance>toolbar.offsetTop+2&&Math.abs(top)>2)throw new Error('Toolbar did not remain sticky');
  if(style.filter!=='none'||style.opacity!=='1')throw new Error('Toolbar foreground was blurred');
  const hit=document.elementFromPoint(toolbar.getBoundingClientRect().left+20,Math.max(1,top)+20);
  if(!toolbar.contains(hit))throw new Error('Chart painted over toolbar');
 }
 const sorted=intervals.slice().sort((a,b)=>a-b),mean=intervals.length?intervals.reduce((a,b)=>a+b,0)/intervals.length:0;
 const metrics={frames,mean_ms:mean,p95_ms:sorted.length?sorted[Math.floor((sorted.length-1)*.95)]:0,fps:mean?1000/mean:0,scroll_px:distance,toolbar_top:top,blur_enabled:Number(blur),opacity_fallback:0};
 await rpc('selftest_snapshot',{phase:'scrolled',metrics});
 if(toolbar){root.dataset.transparency='off';await tick();await tick();const style=getComputedStyle(toolbar);
  if(style.backdropFilter!=='none')throw new Error('Opaque material fallback failed');
  metrics.opacity_fallback=1;await rpc('selftest_snapshot',{phase:'opaque',metrics});
 }
 if(previous==null)delete root.dataset.transparency;else root.dataset.transparency=previous;
 window.scrollTo({top:0,behavior:'instant'});await tick();await tick();
 window.pulseTraceScrollReport=metrics;
}
