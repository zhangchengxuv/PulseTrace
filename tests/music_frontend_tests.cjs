'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
class Node{
 constructor(tag,attrs={},children=[]){this.tag=tag;this.dataset={};this.style={};this.children=[];this.isConnected=true;this.clientWidth=800;Object.assign(this,attrs);this.checked=!!attrs.checked;for(const [k,v]of Object.entries(attrs))if(k.startsWith('data-'))this.dataset[k.slice(5).replace(/-([a-z])/g,(_,c)=>c.toUpperCase())]=v;this.append(...children);}
 append(...nodes){for(const n of nodes.flat())if(n!=null){this.children.push(n);if(n instanceof Node)n.parent=this;}}
 replaceChildren(...nodes){this.children=[];this.append(...nodes);}get firstChild(){return this.children[0];}
 get textContent(){return this.children.map(n=>n instanceof Node?n.textContent:String(n)).join('');}set textContent(v){this.children=[String(v)];}
 all(){return this.children.filter(n=>n instanceof Node).flatMap(n=>[n,...n.all()]);}
 matches(s){if(s.startsWith('.'))return (this.class||'').split(' ').includes(s.slice(1));const a=s.match(/^\[data-music-rank="(.*)"\]$/);return a?this.dataset.musicRank===a[1]:this.tag===s;}
 querySelectorAll(s){const parts=s.split(' ');return this.all().filter(n=>n.matches(parts.at(-1))&&(parts.length===1||function(){for(let p=n.parent;p;p=p.parent)if(p.matches(parts[0]))return true;return false;}()));}
 querySelector(s){return this.querySelectorAll(s)[0]||null;}checkValidity(){return Number(this.value)>=1&&Number(this.value)<=3600;}
 getContext(){return {scale(){},fillText(){},beginPath(){},moveTo(){},lineTo(){},stroke(){},fillRect(){}};}
}
const c=new Node('div'),calls=[];let schedules=0;const ctx=vm.createContext({console,Map,Date,JSON,Number,Math,devicePixelRatio:1,viewGeneration:0,
 tr:key=>key,PulseI18n:{locale:'en-US',date:()=>''},state:{config:{music_enabled:false,music_qq:true,music_netease:true,music_threshold_seconds:30}},range:{from:0,to:99999999},chartObservers:[],document:{querySelector:s=>c.querySelector(s)},getComputedStyle:()=>({getPropertyValue:()=>''}),
 el:(tag,attrs={},...children)=>new Node(tag,attrs,children),panel:(title,...children)=>new Node('article',{},[new Node('h3',{},[title]),...children]),card:(title,value)=>new Node('article',{},[new Node('span',{},[title]),new Node('strong',{},[value])]),help:(title,...children)=>new Node('details',{},[title,...children]),usageDuration:ms=>ms+'ms',fmt:String,error:e=>{throw e;},PulseRender:{observe:()=>({schedule(){schedules++;},disconnect(){}})},PulseViz:{aggregate:x=>x},
 rpc:async(type,p)=>{calls.push({type,p});if(type==='set_config')Object.assign(ctx.state.config,p);if(type==='get_status')return ctx.state;return {file:'synthetic.csv',rows:[],status:{active:false}};}});
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
(async()=>{await settings.querySelector('button').onclick();assert.equal(calls[0].type,'set_config');assert.equal(calls[0].p.music_threshold_seconds,45);assert.equal(calls[0].p.music_enabled,false);await ctx.refreshMusic();assert.equal(calls.at(-1).type,'get_music');assert.ok(schedules>3);console.log('Music UI: disabled/partial/history/error, filters, merge, trends, saved settings and stable DOM passed');})().catch(e=>{console.error(e);process.exitCode=1;});
