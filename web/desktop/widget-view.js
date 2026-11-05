'use strict';
function widgetStatusKey(status){
 if(status.mode==='embedded')return status.visibility===1?'widget.visible':status.visibility===2?'widget.hidden':status.visibility===3?'widget.occluded':'widget.verifying';
 if(status.mode==='floating')return status.reason?.includes('fallback')?'widget.fallback':status.visibility===1?'widget.floating_visible':'widget.verifying';
 if(status.mode==='tray'&&status.reason==='Tray-only selected')return 'widget.tray';
 if(!status.reason&&status.helper_running)return 'widget.verifying';
 return status.reason?.includes('space')?'widget.no_space':'widget.failed';
}
globalThis.PulseWidgetStyle={
 defaults:{taskbar_font:'Segoe UI Variable Text',taskbar_font_size:10,taskbar_unit:'auto',taskbar_precision:1,taskbar_upload_color:'#3498db',taskbar_download_color:'#8bc4ea',taskbar_offset_x:0,taskbar_offset_y:0},
 rate(value,unit,precision){let index=0;const units=['B/s','KiB/s','MiB/s','GiB/s'];if(unit==='auto'){while(value>=1024&&index<3){value/=1024;index++;}}else{index={bytes:0,kib:1,mib:2}[unit];value/=1024**index;}return value.toFixed(precision)+' '+units[index];}
};
function renderWidgetSettings(c){
 const cfg={...PulseWidgetStyle.defaults,...state.config},form=el('div',{class:'settings-grid widget-settings'}),fields={},feedback=el('p',{class:'muted',role:'status'},tr(widgetStatusKey(state.taskbar_widget||{}))),preview=el('div',{class:'widget-style-preview','aria-label':tr('widget.preview')});
 const add=(key,label,input)=>{fields[key]=input;input.dataset.setting=key;if(input.type==='number')input.required=true;input.value=String(cfg[key]);form.append(el('label',{},tr(label),input));return input;};
 add('taskbar_mode','ui.431737ccb5fa',el('select',{},...[['embedded','ui.f8e1e432a03d'],['floating','ui.bb0654f809d7'],['tray','ui.49fc88224f86']].map(([v,k])=>el('option',{value:v},tr(k)))));
 add('taskbar_monitor','ui.fac15462c14d',el('input',{type:'number',min:0,max:32}));
 add('taskbar_font','widget.font',el('select',{},...['Segoe UI Variable Text','Segoe UI','Microsoft YaHei UI','Consolas'].map(v=>el('option',{value:v},v))));
 add('taskbar_font_size','widget.font_size',el('input',{type:'number',min:8,max:12,step:1}));
 add('taskbar_unit','widget.unit',el('select',{},...['auto','bytes','kib','mib'].map(v=>el('option',{value:v},v==='auto'?tr('widget.auto'):{bytes:'B/s',kib:'KiB/s',mib:'MiB/s'}[v]))));
 add('taskbar_precision','widget.precision',el('input',{type:'number',min:0,max:3,step:1}));
 add('taskbar_download_color','widget.download_color',el('input',{type:'color'}));add('taskbar_upload_color','widget.upload_color',el('input',{type:'color'}));
 add('taskbar_offset_x','widget.offset_x',el('input',{type:'number',min:-200,max:200,step:1}));add('taskbar_offset_y','widget.offset_y',el('input',{type:'number',min:-200,max:200,step:1}));
 add('taskbar_background','widget.background',el('select',{},el('option',{value:'transparent'},tr('widget.transparent')),el('option',{value:'subtle'},tr('widget.subtle'))));
 const read=()=>Object.fromEntries(Object.entries(fields).map(([key,input])=>[key,input.type==='number'?Number(input.value):input.value]));
 function draw(){if(Object.values(fields).some(n=>!n.checkValidity()))return;const next=read(),n=state.network||{};preview.style.fontFamily=next.taskbar_font;preview.style.fontSize=next.taskbar_font_size+'pt';preview.replaceChildren();for(const [glyph,metric,color]of [['↓','download_bps','taskbar_download_color'],['↑','upload_bps','taskbar_upload_color']]){const arrow=el('span',{},glyph);arrow.style.color=next[color];preview.append(el('div',{},arrow,el('span',{},state.monitoring?PulseWidgetStyle.rate(n[metric]||0,next.taskbar_unit,next.taskbar_precision):tr('widget.paused'))));}}
 let chain=Promise.resolve();const save=()=>{if(Object.values(fields).some(n=>!n.checkValidity())){feedback.textContent=tr('widget.invalid_style');return;}const next=read();chain=chain.catch(()=>{}).then(async()=>{try{await rpc('set_config',next);state.config={...state.config,...next};feedback.textContent=tr('ui.1bd91a7d0c53');}catch(e){feedback.textContent=tr('widget.invalid_style');error(e);}});return chain;};
 for(const input of Object.values(fields)){input.addEventListener('input',()=>{if(Object.values(fields).every(n=>n.checkValidity()))draw();});input.addEventListener('change',()=>{draw();save();});}
 const reset=el('button',{class:'secondary',onclick:()=>{for(const [key,value]of Object.entries(PulseWidgetStyle.defaults))fields[key].value=String(value);draw();save();}},tr('widget.reset'));
 c.append(panel(tr('ui.cb0e1905bb5a'),form,el('p',{class:'muted'},tr('widget.style_hint')),preview,feedback,el('div',{class:'actions'},el('button',{onclick:save},tr('ui.63c73c4730f4')),reset),disclosure('settings.widget.diagnostics',tr('ui.cab87d2c1297'),null,el('p',{},state.taskbar_widget?.reason||tr('ui.00a976529361')),el('p',{},tr('ui.45b4019204aa')))));draw();
}

async function validateWidgetStyleSettings(){
 if(!state.self_test||page!=='settings')return;const before=await rpc('get_status'),control=document.querySelector('[data-setting="taskbar_unit"]');if(!control)throw new Error('Widget style controls missing');
 try{control.value='mib';control.dispatchEvent(new Event('change',{bubbles:true}));let saved;
  for(let i=0;i<30;i++){saved=await rpc('get_status');if(saved.config.taskbar_unit==='mib')break;await new Promise(r=>setTimeout(r,100));}
  if(saved.config.taskbar_unit!=='mib'||saved.monitoring!==before.monitoring||saved.session!==before.session)throw new Error('Widget style change lost or restarted monitoring');
  if(!document.querySelector('.widget-style-preview'))throw new Error('Widget preview missing');
 }finally{await rpc('set_config',{taskbar_unit:before.config.taskbar_unit});state.config.taskbar_unit=before.config.taskbar_unit;control.value=before.config.taskbar_unit;}
}
