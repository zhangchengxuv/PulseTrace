'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
class Node{
 constructor(tag,attrs={},children=[]){this.tag=tag;this.dataset={};this.style={};this.children=[];this.events={};this.isConnected=true;this.clientWidth=800;Object.assign(this,attrs);this.checked=!!attrs.checked;for(const [k,v]of Object.entries(attrs))if(k.startsWith('data-'))this.dataset[k.slice(5).replace(/-([a-z])/g,(_,c)=>c.toUpperCase())]=v;this.append(...children);}
 append(...nodes){for(const n of nodes.flat())if(n!=null){this.children.push(n);if(n instanceof Node){n.parent=this;if(this.tag==='select'&&this.value==null)this.value=n.value;}}}
 addEventListener(name,action){this.events[name]=action;}
 setAttribute(name,value){this[name]=value;}
 replaceChildren(...nodes){this.children=[];this.append(...nodes);}get firstChild(){return this.children[0];}
 get textContent(){return this.children.map(n=>n instanceof Node?n.textContent:String(n)).join('');}set textContent(v){this.children=[String(v)];}
 all(){return this.children.filter(n=>n instanceof Node).flatMap(n=>[n,...n.all()]);}
 matches(s){if(s.startsWith('.'))return (this.class||'').split(' ').includes(s.slice(1));const a=s.match(/^\[data-music-rank="(.*)"\]$/);return a?this.dataset.musicRank===a[1]:this.tag===s;}
 querySelectorAll(s){const parts=s.split(' ');return this.all().filter(n=>n.matches(parts.at(-1))&&(parts.length===1||function(){for(let p=n.parent;p;p=p.parent)if(p.matches(parts[0]))return true;return false;}()));}
 querySelector(s){return this.querySelectorAll(s)[0]||null;}checkValidity(){return Number(this.value)>=1&&Number(this.value)<=3600;}
 getContext(){return {scale(){},fillText(){},beginPath(){},moveTo(){},lineTo(){},stroke(){},fillRect(){}};}
}
const c=new Node('div'),calls=[];let schedules=0,returnedRows=[],held;
const insightResult={leaderboard:[{title:'Synthetic',artist:'Artist',player:'qq',kpm:42,observed_ms:300000}],
 soundtracks:[{title:'Synthetic',apps:[{name:'Editor',overlap_ms:15000}]}],trend:[{minute:100,presses:42}]};
const ctx=vm.createContext({console,Map,Date,JSON,Number,Math,devicePixelRatio:1,viewGeneration:0,page:'music',nativeVisible:true,appliedRangePreset:'custom',queueMicrotask,
 tr:(key,params)=>params?key+' '+Object.values(params).join(' '):key,PulseI18n:{locale:'en-US',date:()=>''},state:{config:{music_enabled:false,music_qq:true,music_netease:true,music_threshold_seconds:30}},range:{from:100,to:110,bucket:1,time_zone:'UTC'},chartObservers:[],document:{querySelector:s=>c.querySelector(s)},getComputedStyle:()=>({getPropertyValue:()=>''}),
 el:(tag,attrs={},...children)=>new Node(tag,attrs,children),panel:(title,...children)=>new Node('article',{},[new Node('h3',{},[title]),...children]),card:(title,value)=>new Node('article',{},[new Node('span',{},[title]),new Node('strong',{},[value])]),help:(title,...children)=>new Node('details',{},[title,...children]),usageDuration:ms=>ms+'ms',fmt:String,error:e=>{throw e;},PulseRender:{observe:()=>({schedule(){schedules++;},disconnect(){}})},PulseViz:{aggregate:x=>x},
 rpc:async(type,p)=>{calls.push({type,p});if(type==='set_config')Object.assign(ctx.state.config,p);if(type==='get_status')return ctx.state;
  if(type==='get_music_insights')return held??insightResult;
  return {file:'synthetic.csv',rows:returnedRows,status:{active:false}};}});
vm.runInContext(fs.readFileSync('web/desktop/date-time.js','utf8'),ctx);
vm.runInContext(fs.readFileSync('web/desktop/music-insights.js','utf8'),ctx);
vm.runInContext(fs.readFileSync('web/desktop/music-view.js','utf8'),ctx);ctx.host=c;
vm.runInContext('musicPage(host)',ctx);const root=c.querySelector('.music-page'),canvas=c.querySelector('canvas');assert.ok(c.textContent.includes('music.disabled'));assert.equal(c.querySelectorAll('.cards strong').length,3);
const row={minute:100,key:'song',title:'Synthetic',artist:'Artist',album:'Album',player:'qq',played_ms:30000,plays:1,duration_ms:120000};ctx.sample=row;
vm.runInContext('state.config.music_enabled=true;musicView.result={rows:[sample,{...sample,player:"netease",played_ms:60000}],status:{active:true,sessions:[]}};updateMusic()',ctx);assert.equal(c.querySelectorAll('.cards strong')[0].textContent,'90000ms');assert.equal(c.querySelectorAll('.cards strong')[2].textContent,'2');assert.equal(c.querySelector('canvas'),canvas,'refresh preserves chart');assert.equal(c.querySelector('.music-page'),root,'refresh preserves root');
vm.runInContext('musicView.merge=true;updateMusic()',ctx);assert.equal(c.querySelectorAll('.cards strong')[2].textContent,'1');vm.runInContext('musicView.player="qq";updateMusic()',ctx);assert.equal(c.querySelectorAll('.cards strong')[0].textContent,'30000ms');
vm.runInContext('musicView.result.status={error:"SMTC_UNAVAILABLE",active:false};updateMusic()',ctx);assert.ok(c.textContent.includes('music.unavailable'));assert.equal(c.querySelectorAll('.cards strong')[0].textContent,'30000ms','failure preserves history');
vm.runInContext('musicView.result.status={active:false};updateMusic()',ctx);assert.ok(c.textContent.includes('music.paused'));
const groups=ctx.musicGroups([row,{...row,player:'netease'}],false,true);assert.equal(groups.length,1);assert.equal(groups[0].plays,2);assert.equal(ctx.musicGroups([row,{...row,key:'other',album:'Live'}],false,true).length,2);
const unknown={...row,artist:''};assert.equal(ctx.musicGroups([unknown,{...unknown,key:'different'}],true,true).length,2,'unknown artist is not guessed');
for(const period of ['day','week','month']){const bins=ctx.musicBins([row,{...row,minute:101}],period);assert.equal(bins.reduce((n,r)=>n+r[1],0),60000);assert.equal(bins.length,1);}
const settings=new Node('div');ctx.settings=settings;vm.runInContext('renderMusicSettings(settings)',ctx);const inputs=settings.querySelectorAll('input');assert.equal(inputs.length,4);assert.equal(inputs[0].checked,true);inputs[0].checked=false;inputs[3].value=45;
const settle=()=>new Promise(setImmediate);
(async()=>{
 await settings.querySelector('button').onclick();
 assert.equal(calls[0].type,'set_config');assert.equal(calls[0].p.music_threshold_seconds,45);assert.equal(calls[0].p.music_enabled,false);
 const panel=c.querySelector('.bgm-panel'),insightCalls=()=>calls.filter(call=>call.type==='get_music_insights');
 returnedRows=[row];
 await ctx.refreshMusic();ctx.updateMusic(root);
 assert.equal(calls.at(-1).type,'get_music');assert.ok(panel.hidden);assert.equal(insightCalls().length,0);
 ctx.state.config.music_enabled=true;
 returnedRows=[{...row,played_ms:0}];
 await ctx.refreshMusic();ctx.updateMusic(root);assert.ok(panel.hidden);assert.equal(insightCalls().length,0);
 returnedRows=[row,{...row,minute:101},{...row,minute:103}];
 await ctx.refreshMusic();ctx.updateMusic(root);assert.equal(panel.hidden,false);assert.equal(insightCalls().length,0);
 panel.open=true;panel.events.toggle();await settle();
 assert.equal(insightCalls().length,1);assert.match(panel.textContent,/bgm.kpm 42/);assert.match(panel.textContent,/Editor/);
 const timeline=c.querySelector('.bgm-timeline'),toggle=timeline.querySelector('input');
 assert.equal(timeline.hidden,false);assert.equal(toggle.checked,false);
 toggle.checked=true;toggle.onchange();assert.match(timeline.querySelector('.bgm-readout').textContent,/Synthetic/);
 const minutes=ctx.PulseMusicInsights.playbackMinutes(returnedRows,ctx.range);
 assert.equal(minutes.bands.length,2);assert.equal(minutes.bands[0].to,102);
 assert.equal(ctx.PulseMusicInsights.singleDay({...ctx.range,from:1439,to:1441}),false);
 ctx.updateMusic(root);await settle();assert.equal(insightCalls().length,1,'unchanged refresh does not rescan correlation');
 ctx.nativeVisible=false;await panel.querySelector('button').onclick();
 assert.equal(insightCalls().length,1,'hidden window does not query');
 ctx.nativeVisible=true;
 const group=panel.querySelector('select');group.value='artists';
 let resolve;held=new Promise(done=>{resolve=done;});
 const stale=group.onchange();assert.equal(insightCalls().at(-1).p.group,'artists');
 ctx.state.config.music_enabled=false;ctx.updateMusic(root);assert.ok(panel.hidden);
 resolve(insightResult);await stale;assert.ok(!panel.textContent.includes('Editor'),'disabled panel discards in-flight results');
 assert.ok(schedules>3);
 console.log('Music UI: settings, stable charts, opt-in correlation, request lifecycle and playback marks passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
