'use strict';
const musicView={result:{rows:[],status:{}},player:'all',merge:false,period:'day'};
const musicText=key=>tr('music.'+key);
function musicRows(){return musicView.result.rows.filter(r=>musicView.player==='all'||r.player===musicView.player);}
function musicGroups(values,artist=false,merge=false){
 const groups=new Map();for(const r of values){const key=(merge?'':r.player+'|')+(artist?(r.artist?r.artist.normalize('NFKC').trim().toLocaleLowerCase():r.key):r.key);
  if(!groups.has(key))groups.set(key,{...r,played_ms:0,plays:0});const g=groups.get(key);g.played_ms+=r.played_ms;g.plays+=r.plays;
 }return [...groups.values()].sort((a,b)=>b.played_ms-a.played_ms||a.key.localeCompare(b.key));
}
function musicBins(values,period){
 const bins=new Map();for(const r of values){const d=new Date(r.minute*60000);d.setHours(0,0,0,0);if(period==='week')d.setDate(d.getDate()-(d.getDay()+6)%7);if(period==='month')d.setDate(1);const key=d.getTime()/60000;bins.set(key,(bins.get(key)||0)+r.played_ms);}
 return [...bins].sort((a,b)=>a[0]-b[0]);
}
async function refreshMusic(){const generation=viewGeneration;const result=await rpc('get_music',{from:range.from,to:range.to});if(generation===viewGeneration)musicView.result=result;}
function musicPage(c){
 const root=el('div',{class:'music-page'});root.dataset.locale=PulseI18n.locale;
 const player=el('select',{'aria-label':musicText('player')},...['all','qq','netease'].map(v=>el('option',{value:v},musicText(v))));player.value=musicView.player;
 const merge=el('input',{type:'checkbox',checked:musicView.merge});
 player.onchange=()=>{musicView.player=player.value;updateMusic(root);};merge.onchange=()=>{musicView.merge=merge.checked;updateMusic(root);};
 const feedback=el('span',{class:'muted',role:'status'});
 root.append(el('div',{class:'actions music-controls'},player,el('label',{class:'checkbox'},merge,musicText('merge')),el('button',{class:'secondary',onclick:async()=>{try{const r=await rpc('export_music',{from:range.from,to:range.to});feedback.textContent=musicText('exported')+' '+r.file;}catch(e){error(e);}}},musicText('export')),feedback),el('p',{class:'muted music-state',role:'status'}));
 const cards=el('div',{class:'cards'});for(const label of ['duration','plays','songs'])cards.append(card(musicText(label),'0'));root.append(cards);
 root.append(panel(musicText('current'),el('div',{class:'music-current'})));
 const rankings=el('div',{class:'chart-grid'});for(const key of ['songs_top','artists_top'])rankings.append(panel(musicText(key),el('div',{class:'usage-ranking','data-music-rank':key})));root.append(rankings);
 const period=el('select',{'aria-label':musicText('trend')},...['day','week','month'].map(v=>el('option',{value:v},musicText(v))));period.value=musicView.period;
 const canvas=el('canvas',{role:'img','aria-label':musicText('trend')}),empty=el('p',{class:'muted music-chart-empty'},musicText('empty'));
 const draw=()=>{if(!canvas.isConnected)return;const series=musicBins(musicRows(),musicView.period);empty.hidden=series.length>0;canvas.hidden=!series.length;const w=canvas.clientWidth;if(!series.length||!w)return;
  const height=190,ratio=Math.min(devicePixelRatio||1,3);canvas.width=Math.round(w*ratio);canvas.height=Math.round(height*ratio);const ctx=canvas.getContext('2d');ctx.scale(ratio,ratio);const css=getComputedStyle(document.documentElement);ctx.fillStyle=css.getPropertyValue('--muted');ctx.font='11px Segoe UI';
  const points=PulseViz.aggregate(series,120,1440),maximum=Math.max(...points.map(r=>r[1]),1),left=55,right=w-12;ctx.fillText(usageDuration(maximum),0,15);ctx.strokeStyle=css.getPropertyValue('--chart-grid');ctx.beginPath();ctx.moveTo(left,150);ctx.lineTo(right,150);ctx.stroke();ctx.fillStyle=css.getPropertyValue('--chart-upload');const step=(right-left)/points.length;
  points.forEach((r,i)=>ctx.fillRect(left+i*step+1,150-r[1]/maximum*120,Math.max(1,step-3),r[1]/maximum*120));ctx.fillStyle=css.getPropertyValue('--muted');ctx.fillText(PulseI18n.date(points[0][0]*60000,{month:'short',day:'numeric'}),left,178);if(w>330&&points.length>1){ctx.textAlign='right';ctx.fillText(PulseI18n.date(points.at(-1)[0]*60000,{month:'short',day:'numeric'}),right,178);}
 };
 const observer=PulseRender.observe(canvas,draw);chartObservers.push(observer);root.musicSchedule=()=>observer.schedule?.();period.onchange=()=>{musicView.period=period.value;draw();};
 root.append(panel(musicText('trend'),period,canvas,empty),help(tr('ui.details'),el('p',{},musicText('basis'))));c.append(root);updateMusic(root);
}
function updateMusic(root=document.querySelector('.music-page')){
 if(!root)return;const values=musicRows(),groups=musicGroups(values,false,musicView.merge),cfg=state.config||{},status=musicView.result.status||{};
 root.querySelector('.music-state').textContent=!cfg.music_enabled?musicText('disabled'):status.error?musicText('unavailable'):!status.active?musicText('paused'):musicText('observing');
 const numbers=[usageDuration(values.reduce((n,r)=>n+r.played_ms,0)),fmt(values.reduce((n,r)=>n+r.plays,0)),fmt(groups.length)];root.querySelectorAll('.cards strong').forEach((node,i)=>node.textContent=numbers[i]);
 const current=root.querySelector('.music-current'),sessions=(status.sessions||[]).filter(r=>musicView.player==='all'||r.player===musicView.player),fingerprint=JSON.stringify(sessions);
 if(current.dataset.key!==fingerprint){current.dataset.key=fingerprint;current.replaceChildren(...(sessions.length?sessions.map(s=>el('div',{class:'music-session'},el('strong',{},s.title||musicText('unknown')),el('span',{class:'muted'},[s.artist||musicText('unknown'),musicText(s.player),musicText(s.playing?'playing':'paused'),s.position_ms>=0?usageDuration(s.position_ms)+' / '+usageDuration(s.duration_ms):musicText('no_progress')].join(' · ')))):[el('p',{class:'muted'},musicText('no_session'))]));}
 for(const [key,artist]of [['songs_top',false],['artists_top',true]]){const host=root.querySelector('[data-music-rank="'+key+'"]'),list=musicGroups(values,artist,musicView.merge).slice(0,10),signature=JSON.stringify(list);if(host.dataset.key===signature)continue;host.dataset.key=signature;const max=list[0]?.played_ms||1;
  host.replaceChildren(...(list.length?list.map(r=>{const track=el('div',{class:'usage-track'},el('i'));track.firstChild.style.width=(r.played_ms/max*100)+'%';return el('div',{class:'usage-row'},el('div',{class:'usage-row-head'},el('span',{class:'music-track-title',title:artist?r.artist:[r.title,r.artist,r.album].filter(Boolean).join(' · ')},artist?r.artist||musicText('unknown'):r.title),el('span',{},usageDuration(r.played_ms)+' · '+fmt(r.plays)+' '+musicText('count'))),!musicView.merge?el('small',{class:'muted'},musicText(r.player)):null,track);}):[el('p',{class:'muted'},musicText('empty'))]));
 }
 root.musicSchedule?.();
}
function renderMusicSettings(c){
 const cfg=state.config||{},form=el('div',{class:'settings-grid'}),inputs={};
 for(const key of ['enabled','qq','netease']){const input=el('input',{type:'checkbox',checked:cfg['music_'+key]});inputs[key]=input;form.append(el('label',{class:'checkbox'},input,el('strong',{},musicText(key==='enabled'?'enable':key))));}
 const threshold=el('input',{type:'number',min:1,max:3600,value:cfg.music_threshold_seconds||30});form.append(el('label',{},el('strong',{},musicText('threshold')),threshold));const feedback=el('p',{class:'muted',role:'status'});
 form.append(el('button',{onclick:async()=>{try{if(!threshold.checkValidity())throw Error(tr('settings.invalid'));await rpc('set_config',{music_enabled:inputs.enabled.checked,music_qq:inputs.qq.checked,music_netease:inputs.netease.checked,music_threshold_seconds:Number(threshold.value)});state=await rpc('get_status');feedback.textContent=tr('ui.1bd91a7d0c53');}catch(e){error(e);}}},tr('ui.a3030bf8f16d')),feedback);c.append(panel(musicText('title'),form,help(tr('ui.details'),el('p',{},musicText('basis')))));
}
function validateMusicStates(host){
 const root=host.querySelector('.music-page'),saved={result:musicView.result,player:musicView.player,merge:musicView.merge,enabled:state.config.music_enabled};
 const sample={minute:Math.floor(Date.now()/60000),player:'qq',key:'synthetic-track',title:'Synthetic track',artist:'Synthetic artist',album:'Synthetic album',duration_ms:120000,played_ms:30000,plays:1};
 try{
  musicView.player='all';musicView.merge=false;state.config.music_enabled=true;
  for(const rows of [[],[sample],[sample,{...sample,player:'netease'}]]){
   musicView.result={rows,status:{active:true,sessions:[]}};const canvas=root.querySelector('canvas');updateMusic(root);
   if(root.querySelectorAll('.cards strong').length!==3||root.querySelector('canvas')!==canvas)throw Error('Music stable cards/chart failed');
   if(root.querySelectorAll('.cards strong')[2].textContent!==fmt(rows.length))throw Error('Music songs differ from aggregates');
  }
  musicView.merge=true;updateMusic(root);if(root.querySelectorAll('.cards strong')[2].textContent!==fmt(1))throw Error('Music merge failed');
  musicView.result.status={error:'SMTC_UNAVAILABLE',active:false};updateMusic(root);if(!root.textContent.includes(musicText('unavailable')))throw Error('Music error state missing');
 }finally{Object.assign(musicView,{result:saved.result,player:saved.player,merge:saved.merge});state.config.music_enabled=saved.enabled;updateMusic(root);}
}
