require('../web/desktop/locales.js');require('../web/desktop/i18n.js');PulseI18n.use('zh-CN');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');require(path.join(root,'web/desktop/colormaps.js'));require(path.join(root,'web/desktop/visualization.js'));
const v=PulseViz;let checks=0;function check(ok,message){checks++;assert.ok(ok,message);}
check(PulseRange.bucket(240)===1&&PulseRange.bucket(241)===60&&PulseRange.bucket(14400)===60&&PulseRange.bucket(14401)===1440,'all-data granularity adapts to saved span');
require('../web/desktop/traffic-calendar.js');
for(const [y,m,count]of [[2024,2,29],[2100,2,28],[2026,11,30],[2026,12,31]]){const cells=PulseCalendar.cells(y,m);check(cells.length===count&&new Set(cells.map(c=>c.column+'/'+c.row)).size===count,'calendar leap/count/unique day mapping');check(cells.every(c=>c.row<7&&c.column<6),'calendar week bounds');}
check(PulseCalendar.value(null,'all','total').state==='missing','calendar missing is not zero');
const day={has_totals:true,has_ip:true,upload_bytes:10,download_bytes:20,ipv4_up:2,ipv4_down:3,ipv6_up:0,ipv6_down:0,observed_minutes:1440,ip_minutes:1440,expected_minutes:1440,quality:'observed_unverified_reference'};
check(PulseCalendar.value(day,'all','total').bytes===30&&PulseCalendar.value(day,'4','up').bytes===2,'calendar never sums interface and IP counters');
check(PulseCalendar.value(day,'6','total').state==='zero','known zero family is distinct');
check(PulseCalendar.value({...day,ip_minutes:1},'4','total').state==='partial','partial coverage retains actual bytes and quality');
check(PulseCalendar.value({...day,has_ip:false},'4','total').bytes===null,'unknown family never replaced with zero');
check(PulseCalendar.value({...day,has_ip:false,quality:'incomparable_bases'},'4','total').state==='partial'&&PulseCalendar.value({...day,has_ip:false,quality:'incomparable_bases'},'4','total').reason==='incomparable_bases','incompatible calendar observation is not mislabeled as no observations');
require('../web/desktop/mouse-view.js');
check(!PulseMouse.model([]).has,'mouse missing coverage stays missing');
const mouse=PulseMouse.model([{module:'coverage',metric:'observed_ms',value:60000},{module:'mouse',metric:'mouse4',value:1},{module:'mouse',metric:'mouse5',value:3}]);
check(mouse.has&&mouse.total===4&&mouse.max===3,'mouse model uses real aggregate counts');
check(mouse.buttons.find(b=>b.id==='mouse4').share===.25&&mouse.buttons.find(b=>b.id==='mouse5').share===.75,'side buttons retain distinct identities and shares');
check(mouse.buttons.find(b=>b.id==='left').state==='zero'&&PulseMouse.model([]).buttons[0].state==='missing','mouse zero differs from missing');
require(path.join(root,'web/desktop/dashboard-data.js'));const dash=PulseDashboard;
const r=(minute,module,metric,value)=>({minute,module,metric,value});
require('../web/desktop/keyboard-layout.js');
require('../web/desktop/date-time.js');
require('../web/desktop/insights-data.js');
{
 const insight=PulseInsights;
 const start=Date.UTC(2026,10,15)/60000,selection={from:start,to:start+10*1440,bucket:60,time_zone:'UTC'};
 const keyboard=insight.keyboard({17:2,30:8,36:10,56:3,312:4,57:12,14:2,339:1,71:5},47);
 check(keyboard.fingers.length===10&&keyboard.fingers.find(f=>f.hand==='left'&&f.name==='pinky').count===8,'finger data uses physical scan codes');
 check(keyboard.left===13&&keyboard.right===16&&keyboard.space===12&&keyboard.unassigned===6,'hand, shared and unassigned counts reconcile');
 check(keyboard.typed===20&&keyboard.corrections===3&&keyboard.correctionRate===.15,'correction counts include Delete and exclude keypad digits');
 check(insight.keyboard({},0).correctionRate===null&&PulseKeyboard.alphanumeric.length===36,'empty correction rate and 36 typing keys');
 const assignments=Object.values(PulseKeyboard.fingerKeys).flatMap(hand=>Object.values(hand).flat());
 check(new Set(assignments).size===assignments.length&&PulseKeyboard.alphanumeric.every(key=>assignments.includes(key))&&!assignments.includes(57),'finger assignments are exclusive and cover letters and digits');
 check(insight.wheel([])===null&&insight.wheel([r(start,'coverage','observed_ms',50)]).metres===0,'wheel estimate distinguishes empty from observed zero');
 const wheel=insight.wheel([r(start,'mouse','wheel_up_delta',60),r(start,'mouse','wheel_down_delta',-240),r(start,'mouse','wheel_down_events',9),
  r(start+60,'mouse','wheel_down_events',2),r(start,'mouse','horizontal_events',10)]);
 check(wheel.fallback&&Math.abs(wheel.metres-.0135)<1e-12,'wheel delta keeps partial notches and falls back by bucket and direction');
 check(insight.rating('keyboard')===50000000&&insight.rating('mouse')===20000000&&!insight.validRating(1.5),'default lifetime ratings and integer validation');
 const originalStorage=Object.getOwnPropertyDescriptor(globalThis,'localStorage');
 const saved=new Map();
 Object.defineProperty(globalThis,'localStorage',{configurable:true,value:{getItem:key=>saved.get(key),setItem:(key,value)=>saved.set(key,value)}});
 try{
  insight.saveRatings({keyboard:70000000,mouse:30000000});
  check(saved.size===1&&insight.rating('keyboard')===70000000&&insight.rating('mouse')===30000000,'custom ratings persist together in this profile');
  insight.saveRatings({keyboard:-1,mouse:0});
  check(insight.rating('keyboard')===50000000&&insight.rating('mouse')===20000000,'invalid stored ratings fall back to defaults');
 }finally{if(originalStorage)Object.defineProperty(globalThis,'localStorage',originalStorage);else delete globalThis.localStorage;}
 const dates=[r(start-60,'coverage','observed_ms',30000),r(start+8*1440,'keyboard','30',100)];
 const days=insight.observedDays(dates,selection,'keyboard');
 check(days===8+60/1440&&insight.observedDays(dates,{...selection,to:start+60},'keyboard')===1/24,'wear annualization clamps elapsed observation span to selected range');
 check(insight.wear(100,500,6).years===null&&insight.wear(100,500,days).years>0&&insight.wear(600,500,0).years===0,'wear has sample gate and nonnegative years at rated end');
 check(insight.wear(0,500,10).years===null&&insight.wear(null,500,10).share===null,'zero-use and missing-use model');
 const actions=[r(start+60,'keyboard','17',100),r(start+240,'keyboard','57',200),
  r(start+60,'mouse','left',200),r(start+240,'mouse','right',600),
  r(start+60,'mouse','wheel_up_events',1500),r(start+60,'repeat','17',200)];
 const progress=insight.achievements(actions,selection);
 check(progress.keyCount===2&&progress.wasd===1/3&&progress.night===300/1100,'WASD and nighttime use the correct denominators and exclude 04:00');
 check(progress.clicks.level===1&&progress.clicks.target===1000&&progress.presses.target===1000,'separate graded milestones');
 check(insight.achievements(actions,{...selection,bucket:1440}).night===null&&insight.achievements([],selection).wasd===null,'daily rolls and empty data cannot imply night or WASD ratios');
 check(insight.achievements(PulseKeyboard.keys.map(key=>r(start,'keyboard',key.id,1)),selection).keyCount===104,'all physical keys stay eligible with TKL display');
 check(insight.achievements([r(start,'keyboard','30',1e12),r(start,'mouse','left',1e10)],selection).presses.level===4,'milestones tolerate large aggregates and retain final tier');
 check(insight.songPlays([{player:'qq',key:'a',plays:5},{player:'qq',key:'a',plays:6},
  {player:'netease',key:'a',plays:10},{player:'qq',key:'b',plays:3}])===11,'most repeated qualified plays group by track within one player');
 const originalZone=process.env.TZ;
 process.env.TZ='Asia/Singapore';
 const shifted=[r(start-60,'mouse','left',1),r(start+16*60,'mouse','right',4)];
 check(insight.achievements(shifted,{...selection,time_zone:'local'}).night===.8&&insight.achievements(shifted,selection).night===0,'night uses requested civil timezone');
 if(originalZone===undefined)delete process.env.TZ;else process.env.TZ=originalZone;
}
check(dash.inputs([]).length===0,'empty dashboard does not invent bins');
let bin=dash.inputs([r(1,'keyboard','30',3)],1)[0];check(bin.keyboard===3&&bin.mouse===null,'unknown mouse interval stays missing');
bin=dash.inputs([r(1,'coverage','observed_ms',1000)],1)[0];check(bin.keyboard===0&&bin.mouse===0,'observed idle interval is real zero');
const inputRows=Array.from({length:3000},(_,i)=>r(i,'keyboard','30',2));const inputBins=dash.inputs(inputRows,1);
check(inputBins.length<=96&&inputBins.reduce((n,b)=>n+b.keyboard,0)===6000,'dashboard bar coarsening preserves totals and point bound');
const shiftedBins=dash.inputs(inputRows.map(row=>({...row,minute:row.minute+31})),1);
check(shiftedBins.length<=96&&shiftedBins.reduce((n,b)=>n+b.keyboard,0)===6000,'unaligned range still respects point bound and totals');
check(dash.distribution([0,0])===null&&dash.distribution([1,3])[1]===.75,'ratio requires real denominator');
const sample={accepted:1,source:'ndis',basis:'ip',ipv4_up:10,ipv4_down:20,ipv6_up:20,ipv6_down:10,quality:'incomplete_observation'};
const peak=dash.peakInput([
 {minute:10,module:'keyboard',metric:'30',value:18},{minute:10,module:'keyboard',metric:'31',value:12},
 {minute:10,module:'mouse',metric:'left',value:7},{minute:11,module:'mouse',metric:'left',value:51},
 {minute:11,module:'mouse',metric:'wheel_up_delta',value:12000},{minute:10,module:'repeat',metric:'30',value:100}
]);
check(peak.kpm===30&&peak.apm===51&&dash.peakInput([])===null,'peak rates use minute counts without repeat presses or scroll delta');
check(dash.protocol([sample]).ratio[0]===.5&&dash.protocol([sample]).quality==='不完整观测','observed ratio retains quality');
check(!dash.protocol([sample,{...sample,basis:'other'}]).has,'incomparable protocol bases are not summed');
check(!dash.protocol([{...sample,accepted:0}]).has,'rejected-only samples are not a valid zero');
check(!dash.protocol([sample,{...sample,source:'other'}]).has,'different capture sources are not additive');
check(!dash.protocol([{...sample,source:''}]).has,'unknown source cannot establish comparable traffic');
check(!dash.protocol([{...sample,source:'a|b',basis:'c'},{...sample,source:'a',basis:'b|c'}]).has,'source and basis pair cannot collide');
const ipRows=dash.protocolRows([{...sample,minute:60,interface_id:'19'},{...sample,minute:61,interface_id:'19',accepted:0},{...sample,minute:62,interface_id:'20'}],'19');
check(ipRows.length===4&&ipRows.every(r=>r.minute===60&&r.module==='network_ip')&&ipRows[0].value===10,'protocol chart uses accepted identity-preserving records, not unrelated generic rows');
check(dash.protocolRows([{...sample,minute:60,interface_id:'19'},{...sample,minute:60,interface_id:'19',basis:'other'}],'19').length===0,'chart suppresses incompatible bases in the same bucket');
check(dash.protocolRows([{...sample,minute:60,interface_id:'19'},{...sample,minute:120,interface_id:'19',source:'other'}],'19').length===0,'chart suppresses incompatible sources across time');
const chartContext={state:{network:{selected:'19'}},protocolSamples:[{...sample,minute:60,interface_id:'19'}],rows:[r(60,'network_ip','19/ipv4_up',999)],PulseDashboard:dash,tr};
chartContext.trend=(...args)=>args;require('node:vm').runInNewContext(fs.readFileSync(path.join(root,'web/desktop/protocol-view.js'),'utf8'),chartContext);
check(chartContext.protocolTrend()[4][0].value===10,'actual chart renderer ignores incompatible legacy generic rows');
chartContext.protocolSamples.push({...sample,minute:60,interface_id:'19',basis:'other'});
check(chartContext.protocolTrend()[4].length===0,'actual chart renderer leaves incompatible observation gaps empty');
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
check(v.normalized(10,10000,'log')>.2&&v.normalized(10,10000,'linear')===.001,'log scale separates low and middle frequencies');
check(v.heatTick(.5,9995,'linear')===4998&&v.heatTick(.5,10000,'log')===99,'heat legend uses integer ticks');
check(v.color('arctic',0,0,'linear',true).join() ==='248,250,252','light Arctic zero key uses pearl surface');
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
 for(const [fg,bg,min]of [['--accent','--accent-soft',4.5],['--button-text','--button-bg',4.5],['--bg','--error',4.5],['--series-primary-text','--panel',4.5],['--chart-upload','--panel',3],['--chart-download','--panel',3],['--protocol-ipv4','--panel',3],['--protocol-ipv6','--panel',3],['--control-border','--panel',3],...['--data-keyboard','--data-mouse','--data-wheel','--data-time'].map(k=>[k,'--panel',4.5])]){const ratio=v.contrast(rgb(fg),rgb(bg));check(ratio>=min,`${theme} ${fg}/${bg} contrast ${ratio}`);results.push({theme,fg,bg,ratio});}}
console.log(`${checks} frontend checks passed. Minimum text/interaction contrast: ${Math.min(...results.filter(r=>!r.fg.startsWith('--chart')&&!r.fg.startsWith('--protocol')&&r.fg!=='--control-border').map(r=>r.ratio)).toFixed(2)}:1`);
if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify({checks,contrast:results},null,2));
