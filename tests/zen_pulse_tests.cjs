'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const nodes=new Map(),sent=[],requests=[],frames=new Map(),observers=[];
let time=0,id=0,currentPath,painted=[],restoredTop,lastTransform;
const drawing={
 setTransform(...args){lastTransform=args;},clearRect(){painted=[];},
 createLinearGradient:()=>({addColorStop(){}}),
 beginPath(){currentPath=[];},moveTo(x,y){currentPath.push(y);},
 lineTo(x,y){currentPath.push(y);},stroke(){painted.push([...currentPath]);}
};
const document={
 visibilityState:'visible',handlers:{},
 body:{classList:new Set()},
 addEventListener(name,handler){this.handlers[name]=handler;}
};
document.body.classList.remove=document.body.classList.delete;
const $=id=>{
 if(!nodes.has(id))nodes.set(id,{
  id,width:1,height:1,clientWidth:641,clientHeight:279,textContent:'',
  events:{},addEventListener(name,handler){this.events[name]=handler;},
  setAttribute(name,value){this[name]=value;},focus(){document.activeElement=this;},getContext:()=>drawing
 });
 return nodes.get(id);
};
const flush=t=>{time=t;const tasks=[...frames.values()];frames.clear();for(const task of tasks)task(t);};
const snapshot=Date.UTC(2026,10,25,12),day=snapshot/60000-720;
let resolveDay;
const context=vm.createContext({
 $,document,viewGeneration:0,nativeVisible:true,tr:k=>k,fmt:String,
 PulseI18n:{locale:'en-US'},PulseDate:{calendarStart:minute=>Math.floor(minute/1440)*1440},
 PulseDatePicker:{close(){}},performance:{now:()=>time},
 chrome:{webview:{postMessage:payload=>sent.push(payload)}},
 window:{scrollY:450,devicePixelRatio:1.25,addEventListener(){},scrollTo(options){restoredTop=options.top;}},
 requestAnimationFrame:callback=>{frames.set(++id,callback);return id;},cancelAnimationFrame:token=>frames.delete(token),
 ResizeObserver:class{constructor(cb){this.callback=cb;observers.push(this);}observe(){}disconnect(){this.disconnected=true;}},
 getComputedStyle:()=>({getPropertyValue:key=>key==='--series-primary'?'#38bdf8':'#a78bfa'}),
 updateUiVisibility:()=>context.PulseZen.syncVisibility(),
 rpc:(type,params,timeout)=>{
  requests.push({type,params,timeout});
  if(type==='get_zen_day')return new Promise(resolve=>{resolveDay=resolve;});
  return Promise.resolve({fullscreen:params.enabled});
 }
});
vm.runInContext(fs.readFileSync('web/desktop/zen-pulse.js','utf8'),context);
const zen=context.PulseZen,canvas=$('zen-canvas');
const sample={session:'fixture',active:true,time_ms:snapshot,input_ms:snapshot,keys:8,clicks:4,network_bps:0,track:null};
const settle=()=>new Promise(setImmediate);
(async()=>{
 zen.init();
 zen.accept(sample);assert.equal(frames.size,0);assert.equal(requests.length,0);
 zen.enter();assert.equal(sent.at(-1).params.enabled,true);assert.ok(zen.active&&zen.running);
 flush(0);assert.equal(canvas.width,801);assert.equal(canvas.height,349);
 assert.equal(lastTransform[0],801/641);assert.equal(lastTransform[3],349/279);
 assert.equal(zen.running,false,'idle sweep does not keep requesting frames');
 zen.accept(sample);
 assert.equal(requests.at(-1).type,'get_zen_day');assert.equal(requests.at(-1).timeout,0);
 resolveDay({session:'fixture',day,keys:8,clicks:4,day_keys:100,day_clicks:60});await settle();
 assert.equal($('zen-keys').textContent,'100');
 time=1000;
 zen.accept({...sample,time_ms:snapshot+1000,keys:14,clicks:6,network_bps:2500000,track:{title:'Fixture song',artist:'Artist'}});
 assert.equal($('zen-keys').textContent,'106');assert.equal($('zen-clicks').textContent,'62');
 assert.equal($('zen-track').textContent,'Fixture song · Artist');
 context.window.devicePixelRatio=1.5;
 flush(1250);
 assert.equal(canvas.width,962);assert.equal(canvas.height,419);
 assert.ok(painted[1].some(y=>Math.abs(y-279*.32)>1),'input pulses have visible amplitude');
 assert.ok(painted[3].some(y=>Math.abs(y-279*.68)>1),'network sample has visible amplitude');
 assert.equal(requests.length,1,'visible clock and later totals need no repeated queries');
 const staleFrame=[...frames.values()][0];
 document.visibilityState='hidden';zen.syncVisibility();
 assert.ok(zen.active&&!zen.running&&observers[0].disconnected);assert.equal(canvas.width,1);
 assert.equal(sent.at(-1).params.enabled,false);
 staleFrame(1500);zen.accept(sample);
 assert.equal(frames.size,0);assert.equal(requests.length,1);
 document.visibilityState='visible';context.nativeVisible=false;zen.syncVisibility();
 assert.equal(frames.size,0,'the Windows visibility gate also applies');
 context.nativeVisible=true;zen.syncVisibility();flush(2000);
 zen.accept({...sample,time_ms:snapshot+86400000,input_ms:snapshot+86400000});
 assert.equal(requests.length,2,'resume and new civil day rebase totals');
 await $('zen-fullscreen').events.click();
 assert.equal($('zen-fullscreen')['aria-pressed'],'true');
 document.handlers.keydown({key:'Escape',preventDefault(){},stopPropagation(){}});
 resolveDay({session:'fixture',day:day+1440,day_keys:123,day_clicks:456,keys:8,clicks:4});await settle();
 assert.equal($('zen-keys').textContent,'—','late snapshot is discarded after exit');
 assert.ok(!zen.active&&!zen.running);assert.equal(requests.at(-1).params.enabled,false);
 assert.equal(restoredTop,450);assert.equal(document.activeElement.id,'zen-enter');
 zen.enter();canvas.events.dblclick();assert.equal(zen.active,false);
 console.log('Zen Pulse waveforms, DPR, totals, visibility, fullscreen and exit checks passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
