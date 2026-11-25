'use strict';
const insightText=(key,params)=>tr('insight.'+key,params);
const insightNumber=(value,maximumFractionDigits=1)=>value.toLocaleString(PulseI18n.locale,{maximumFractionDigits});
const insightPercent=(value,maximumFractionDigits=1)=>value==null?'—':
 value.toLocaleString(PulseI18n.locale,{style:'percent',maximumFractionDigits});
const insightProgress=(value,max,label)=>el('progress',{class:'insight-progress',max,value:Math.min(max,Math.max(0,value??0)),'aria-label':label});
const insightYears=years=>years==null?'—':
 years>99?insightText('years_over_limit'):insightText('years',{value:insightNumber(years)});

function insightWear(device,records,selection,items){
 const rated=PulseInsights.rating(device),days=PulseInsights.observedDays(records,selection,device);
 return panel(insightText('wear_title'),el('p',{class:'muted insight-note'},insightText('wear_rating',{count:fmt(rated)})),
  el('div',{class:'insight-stats'},items.map(({name,count})=>{
   const {share,years}=PulseInsights.wear(count,rated,days);
   const used=insightPercent(share,share>0&&share<.001?7:1);
   return el('div',{class:'insight-stat'},el('strong',{},name),
    el('span',{class:'insight-emphasis'},insightText('wear_used',{value:used})),
    el('small',{},insightText('wear_left',{value:insightYears(years)})),
    insightProgress(share,1,insightText('wear_used',{value:used})));
  })),el('p',{class:'muted insight-note'},insightText('wear_note')));
}

function insightHandSvg(hand,fingers,maximum){
 const ns='http://www.w3.org/2000/svg';
 const shape=(tag,attrs)=>{
  const node=document.createElementNS(ns,tag);
  for(const [name,value] of Object.entries(attrs))node.setAttribute(name,value);
  return node;
 };
 const svg=shape('svg',{viewBox:'0 0 140 148','aria-hidden':'true',class:'insight-hand-svg'});
 const outline=shape('g',{transform:hand==='right'?'translate(140 0) scale(-1 1)':'translate(0 0)'});
 const paths={
  pinky:'M16 79 9 47a7 7 0 0 1 14-3l8 35z',
  ring:'M31 74 27 27a8 8 0 0 1 16-1l4 48z',
  middle:'M48 73 47 17a8 8 0 0 1 16 0l2 56z',
  index:'M66 74 69 24a8 8 0 0 1 16 1l-1 52z',
  thumb:'M84 89 107 66a9 9 0 0 1 13 12l-19 30z'
 };
 for(const finger of fingers){
  const path=shape('path',{d:paths[finger.name],class:'insight-finger-shape','data-finger':finger.name});
  if(finger.count>0)path.style.setProperty('--finger-load',(22+78*finger.count/maximum)+'%');
  outline.append(path);
 }
 outline.append(shape('path',{class:'insight-palm',d:'M16 77q26-7 51-6l17 3q14 5 20 25-2 22-16 38H38q-17-18-22-60Z'}));
 svg.append(outline);return svg;
}
function renderKeyboardInsights(container,counts,total,top,records,selection){
 const data=PulseInsights.keyboard(counts,total);
 const maximum=Math.max(...data.fingers.map(finger=>finger.count));
 const active=data.fingers.filter(finger=>finger.count>0&&finger.count===maximum);
 const busiest=active.length?active.map(finger=>insightText(finger.hand)+' · '+insightText(finger.name)).join(' / '):'—';
 const sides=data.left+data.right;
 const load=panel(insightText('hands_title'),
  el('p',{class:'insight-busiest'},insightText('busiest',{name:busiest})));
 const hands=el('div',{class:'insight-hands'},['left','right'].map(hand=>{
  const fingers=data.fingers.filter(finger=>finger.hand===hand);
  const share=sides>0?data[hand]/sides:null;
  return el('section',{class:'insight-hand','data-side':hand},
   el('h4',{},insightText(hand),el('strong',{class:'insight-emphasis'},insightPercent(share))),
   insightHandSvg(hand,fingers,maximum),
   el('div',{class:'insight-finger-list'},fingers.map(({name,count})=>{
    const title=insightText(name),fraction=data.total>0?count/data.total:null;
    return el('div',{},el('span',{},title),el('small',{},fmt(count)+' · '+insightPercent(fraction)),
     insightProgress(fraction,1,title));
   })));
 }));
 load.append(hands,
  el('p',{class:'insight-secondary-counts'},insightText('shared_counts',{space:fmt(data.space),other:fmt(data.unassigned)})),
  el('p',{class:'muted insight-note'},insightText('hands_note')));
 const correction=panel(insightText('correction_title'),
  el('strong',{class:'insight-number'},insightPercent(data.correctionRate)),
  el('p',{class:'muted insight-note'},insightText('correction_note',{count:fmt(data.corrections),total:fmt(data.typed)})));
 const counted=top&&total>0?Number(top[1]):null;
 const wear=insightWear('keyboard',records,selection,[{name:top?keyLabel(Number(top[0])):'—',count:counted}]);
 container.append(load,el('div',{class:'insight-grid'},correction,wear));
}

function renderMouseInsights(container,records,selection,names,model){
 const wheel=PulseInsights.wheel(records);
 const metres=wheel?.metres??0;
 const distance=wheel?insightText(metres>=1000?'distance_km':'distance_m',
  {value:insightNumber(metres>=1000?metres/1000:metres,metres>=1000?2:3)}):'—';
 const distancePanel=panel(insightText('wheel_title'),el('strong',{class:'insight-number'},distance),
  wheel?el('p',{class:'insight-secondary-counts'},insightText('wheel_reference',{value:insightNumber(metres/PulseInsights.trackMetres,6)})):null,
  el('p',{class:'muted insight-note'},insightText('wheel_note',{millimetres:PulseInsights.scrollStepMetres*1000})),
  wheel?.fallback?el('p',{class:'muted insight-note'},insightText('wheel_fallback')):null);
 const items=model.buttons.filter(item=>item.id==='left'||item.id==='right')
  .map(({id,count})=>({name:names[id],count:wheel||model.has?count:null}));
 const grid=el('div',{class:'insight-grid','data-side':'right'},distancePanel,insightWear('mouse',records,selection,items));
 container.append(grid);
}

function renderInsightSettings(){
 const devices=['keyboard','mouse'],inputs={};
 const fields=devices.map(device=>{
  const input=el('input',{type:'number',min:1,max:Number.MAX_SAFE_INTEGER,step:1,required:'',value:PulseInsights.rating(device)});
  inputs[device]=input;
  return el('label',{'data-insight-rating':device},el('strong',{},insightText('rating_'+device)),input);
 });
 const status=el('p',{role:'status',class:'muted insight-note'});
 const save=el('button',{type:'button',class:'secondary',onclick:()=>{
  const ratings=Object.fromEntries(devices.map(device=>[device,Number(inputs[device].value)]));
  if(!Object.values(ratings).every(PulseInsights.validRating)){
   status.textContent=insightText('rating_invalid');return;
  }
  try{PulseInsights.saveRatings(ratings);status.textContent=insightText('rating_saved');}
  catch{status.textContent=insightText('rating_failed');}
 }},tr('ui.a3030bf8f16d'));
 return panel(insightText('rating_title'),el('div',{class:'settings-grid'},fields,
  el('div',{class:'actions settings-form-actions'},save,status)));
}

let achievementMusic=null;
function achievementCard(title,value,goal,count,target,status,side='left'){
 return el('article',{class:'insight-achievement','data-side':side},
  el('div',{class:'insight-achievement-heading'},el('h4',{},title),el('small',{class:'insight-emphasis'},status)),
  el('strong',{class:'insight-number'},value),
  insightProgress(count,target,title),
  el('small',{class:'muted'},goal));
}
function renderAchievements(records,selection,musicEnabled){
 const group='dashboard.achievements';
 const previous=document.querySelector('[data-disclosure="'+group+'"]');
 if(previous)disclosureState.set(group,{open:previous.open});
 const list=el('div',{class:'insight-achievement-grid'});
 const music=el('div');
 const details=disclosure(group,insightText('achievements_title'),null,list,musicEnabled?music:null);
 const locked=insightText('achievement_locked'),unlocked=insightText('achievement_unlocked');
 const rangeKey=JSON.stringify([selection.from,selection.to]);
 const fraction=(count,target)=>insightText('fraction',{value:fmt(count),target:fmt(target)});
 let rendered=false;
 const render=()=>{
  if(!details.open)return;
  if(!rendered){
   const data=PulseInsights.achievements(records,selection),minimum=PulseInsights.minimumActions;
   const ratioCard=(key,ratio,target,actions,available=true)=>achievementCard(insightText(key+'_title'),
    insightPercent(ratio),available?insightText(key+'_goal',{target:insightPercent(target),minimum:fmt(minimum)}):insightText('night_granularity'),
    actions>=minimum?ratio:null,target,available?(ratio>=target&&actions>=minimum?unlocked:locked):insightText('achievement_unavailable'),'right');
   const milestone=(key,{count,level,levels,target},side)=>achievementCard(insightText(key+'_title'),fraction(count,target),
    insightText('achievement_stage',{level:fmt(level),levels:fmt(levels)}),count,target,level>0?unlocked:locked,side);
   list.append(
    achievementCard(insightText('all_keys_title'),fraction(data.keyCount,data.physicalKeys),insightText('all_keys_goal'),
     data.keyCount,data.physicalKeys,data.keyCount===data.physicalKeys?unlocked:locked),
    ratioCard('wasd',data.wasd,PulseInsights.wasdTarget,data.presses.count),
    ratioCard('night',data.night,PulseInsights.nightTarget,data.actions,data.timed),
    milestone('key_milestone',data.presses,'left'),milestone('mouse_milestone',data.clicks,'right'));
   rendered=true;
  }
  if(musicEnabled)details.updateAchievementMusic();
 };
 const loadMusic=async()=>{
  if(!details.isConnected||document.hidden||!nativeVisible||achievementMusic?.key===rangeKey&&achievementMusic.loading)return;
  const request={key:rangeKey,loading:true};
  achievementMusic=request;
  details.updateAchievementMusic();
  try{
   const result=await rpc('get_music',{from:selection.from,to:selection.to});
   request.count=PulseInsights.songPlays(result.rows);
  }catch{request.failed=true;}
  finally{
   request.loading=false;
   if(achievementMusic===request)
    document.querySelector('[data-disclosure="'+group+'"]')?.updateAchievementMusic?.();
  }
 };
 if(musicEnabled){
  details.updateAchievementMusic=()=>{
   if(!details.open)return;
   const value=achievementMusic?.key===rangeKey?achievementMusic:null;
   if(value?.count!=null){
    const target=PulseInsights.songTarget;
    music.replaceChildren(achievementCard(insightText('song_title'),fraction(value.count,target),insightText('song_goal'),
     value.count,target,value.count>=target?unlocked:locked,'right'));
    return;
   }
   const status=value?.loading?'song_loading':value?.failed?'song_failed':'song_pending';
   music.replaceChildren(el('div',{class:'insight-music-status'},el('span',{role:'status',class:'muted'},insightText(status)),
    value?.loading?null:el('button',{type:'button',class:'secondary',onclick:loadMusic},insightText('song_load'))));
  };
  details.querySelector('summary').addEventListener('click',()=>{if(!details.open)loadMusic();});
 }
 details.classList.add('insight-achievements');
 details.addEventListener('toggle',()=>{if(details.isConnected)render();});
 render();return details;
}
