require('../web/desktop/locales.js');require('../web/desktop/i18n.js');PulseI18n.use('zh-CN');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');require(path.join(root,'web/desktop/colormaps.js'));require(path.join(root,'web/desktop/visualization.js'));
const v=PulseViz;let checks=0;function check(ok,message){checks++;assert.ok(ok,message);}
require(path.join(root,'web/desktop/dashboard-data.js'));const dash=PulseDashboard;
const r=(minute,module,metric,value)=>({minute,module,metric,value});
check(dash.inputs([]).length===0,'empty dashboard does not invent bins');
let bin=dash.inputs([r(1,'keyboard','30',3)],1)[0];check(bin.keyboard===3&&bin.mouse===null,'unknown mouse interval stays missing');
bin=dash.inputs([r(1,'coverage','observed_ms',1000)],1)[0];check(bin.keyboard===0&&bin.mouse===0,'observed idle interval is real zero');
const inputRows=Array.from({length:3000},(_,i)=>r(i,'keyboard','30',2));const inputBins=dash.inputs(inputRows,1);
check(inputBins.length<=96&&inputBins.reduce((n,b)=>n+b.keyboard,0)===6000,'dashboard bar coarsening preserves totals and point bound');
const shiftedBins=dash.inputs(inputRows.map(row=>({...row,minute:row.minute+31})),1);
check(shiftedBins.length<=96&&shiftedBins.reduce((n,b)=>n+b.keyboard,0)===6000,'unaligned range still respects point bound and totals');
check(dash.distribution([0,0])===null&&dash.distribution([1,3])[1]===.75,'ratio requires real denominator');
const sample={accepted:1,source:'ndis',basis:'ip',ipv4_up:10,ipv4_down:20,ipv6_up:20,ipv6_down:10,quality:'incomplete_observation'};
check(dash.protocol([sample]).ratio[0]===.5&&dash.protocol([sample]).quality==='不完整观测','observed ratio retains quality');
check(!dash.protocol([sample,{...sample,basis:'other'}]).has,'incomparable protocol bases are not summed');
check(!dash.protocol([{...sample,accepted:0}]).has,'rejected-only samples are not a valid zero');
const monitored=(ms,presses)=>[r(1,'coverage','observed_ms',ms),r(1,'keyboard','30',presses)];
check(dash.comparison(monitored(60000,12),monitored(60000,10))===.2,'comparison uses actual equivalent observation periods');
check(dash.comparison(monitored(30000,12),monitored(60000,10))===null&&dash.comparison(monitored(60000,12),[])===null,'incomparable coverage suppresses change percentage');
require(path.join(root,'web/desktop/keyboard-layout.js'));
const keys=PulseKeyboard.keys;
check(keys.length===104,'ANSI exactly 104 keys');check(new Set(keys.map(k=>k.id)).size===104,'ANSI unique Raw Input IDs');
for(const k of keys){check(k.x>=0&&k.y>=0&&k.x+k.w<=22.700001&&k.y+k.h<=6.000001,'key bounds '+k.id);for(const q of keys)if(q.id>k.id)check(!(k.x<q.x+q.w-.0001&&k.x+k.w>q.x+.0001&&k.y<q.y+q.h-.0001&&k.y+k.h>q.y+.0001),'physical keys do not overlap '+k.id+'/'+q.id);}
const key=id=>keys.find(k=>k.id===id);
check(key(78).h===2&&key(284).h===2&&key(82).w===2,'numpad spanning keys');
check(key(28).group==='main'&&key(284).group==='numpad','Enter keys isolated');
check(key(328).x===key(336).x&&key(328).y<key(336).y&&key(331).x<key(336).x&&key(333).x>key(336).x,'inverted T arrows');
check(key(63).x-key(62).x>1&&key(67).x-key(66).x>1,'function group gaps');
check(key(338).x-15>=.25&&key(69).x-(key(338).x+3)>=.25,'main, navigation and numpad have distinct group spacing');
check(key(57).w===6.25&&key(42).w===2.25&&key(54).w===2.75,'ANSI relative widths');
check(v.normalized(25,100)===.5&&v.normalized(0,0)===0,'sqrt normalization and zero guard');
for(const n of [NaN,Infinity,-1])assert.throws(()=>v.color('arctic',n,1));
for(let i=0;i<6;i++){const expected=v.arcticStops[i].slice(1).match(/../g).map(h=>parseInt(h,16));check(v.arctic(i/5).every((x,c)=>Math.abs(x-expected[c])<.01),'Arctic stop '+i);}
let prior=v.arctic(0);for(let i=1;i<=4096;i++){const rgb=v.arctic(i/4096);check(rgb.every(x=>Number.isFinite(x)&&x>=0&&x<=255),'Arctic in gamut');check(Math.max(...rgb.map((x,j)=>Math.abs(x-prior[j])))<1,'Arctic continuous interpolation');const fg=v.foreground(rgb)==='#ffffff'?[255,255,255]:[0,0,0];check(v.contrast(rgb,fg)>=4.5,'Arctic readable labels');prior=rgb;}
for(const [name,map]of Object.entries(PulseColorMaps)){
 check(map.length===256,name+' complete LUT');
 check(JSON.stringify(v.color(name,0,100))===JSON.stringify(map[0].map(x=>Math.round(x*255))),name+' minimum');
 check(JSON.stringify(v.color(name,100,100))===JSON.stringify(map[255].map(x=>Math.round(x*255))),name+' maximum');
 check(JSON.stringify(v.color(name,50,100))===JSON.stringify(map[128].map(x=>Math.round(x*255))),name+' midpoint');
 for(let i=0;i<256;i++){const rgb=v.color(name,i,255),text=v.foreground(rgb)==='#ffffff'?[255,255,255]:[0,0,0];check(v.contrast(rgb,text)>=4.5,name+' readable key label '+i);}
}
check(v.heatState(false,0)==='missing','missing distinct');check(v.heatState(true,0)==='zero','zero distinct');check(v.heatState(true,1)==='positive','positive distinct');
assert.throws(()=>v.color('fake',1,2));assert.throws(()=>v.color('viridis',-1,2));
const many=Array.from({length:10080},(_,i)=>[i,i%17]);const reduced=v.aggregate(many);
check(reduced.length<=240,'bounded chart points');check(reduced.reduce((s,p)=>s+p[1],0)===many.reduce((s,p)=>s+p[1],0),'chart aggregation preserves total');check(v.aggregate([]).length===0,'empty data not invented');
check(v.aggregate([[0,2],[1000,3]],240).length===2,'missing periods not fabricated as zero');
check(v.aggregate([[0,2],[60,3]],240,60).length===2,'hour bucket retained when below point cap');
check(v.binWidth([[0,2],[60*1000,3]],240,60)===300,'time-based coarsening accounts for sparse gaps');
const css=fs.readFileSync(path.join(root,'web/desktop/style.css'),'utf8'),base=[...css.matchAll(/:root\{([^}]+)/g)].map(m=>m[1]).join(';'),dark=[...css.matchAll(/html\[data-theme=dark\]\{([^}]+)/g)].map(m=>m[1]).join(';');
const parse=text=>Object.fromEntries([...text.matchAll(/(--[\w-]+):([^;}]*)/g)].map(m=>[m[1],m[2].trim()]));
const results=[];
check(!/\?{3,}|\uFFFD/.test(fs.readFileSync(path.join(root,'web/desktop/product.js'),'utf8')),'frontend source contains no corrupted translation text');
for(const theme of ['light','dark']){const vars={...parse(base),...(theme==='dark'?parse(dark):{})};const rgb=key=>{let val=vars[key]||key;while(val.startsWith('var('))val=vars[val.slice(4,-1)];return val.replace('#','').match(/../g).map(h=>parseInt(h,16));};
 for(const fg of ['--text','--muted'])for(const bg of ['--bg','--panel','--sidebar','--hover','--accent-soft']){const ratio=v.contrast(rgb(fg),rgb(bg));check(ratio>=4.5,`${theme} ${fg}/${bg} contrast ${ratio}`);results.push({theme,fg,bg,ratio});}
 for(const [fg,bg,min]of [['--accent','--accent-soft',4.5],['--button-text','--accent',4.5],['--bg','--error',4.5],['--chart-upload','--panel',3],['--chart-download','--panel',3],['--protocol-ipv4','--panel',3],['--protocol-ipv6','--panel',3],['--control-border','--panel',3],...['--data-keyboard','--data-mouse','--data-wheel','--data-time','--input-mouse','--eyebrow'].map(k=>[k,'--panel',4.5])]){const ratio=v.contrast(rgb(fg),rgb(bg));check(ratio>=min,`${theme} ${fg}/${bg} contrast ${ratio}`);results.push({theme,fg,bg,ratio});}}
console.log(`${checks} frontend checks passed. Minimum text/interaction contrast: ${Math.min(...results.filter(r=>!r.fg.startsWith('--chart')&&!r.fg.startsWith('--protocol')&&r.fg!=='--control-border').map(r=>r.ratio)).toFixed(2)}:1`);
if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify({checks,contrast:results},null,2));
