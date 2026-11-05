'use strict';
function widgetStatusKey(status){
 if(status.mode==='embedded')return status.visibility===1?'widget.visible':status.visibility===2?'widget.hidden':status.visibility===3?'widget.occluded':'widget.verifying';
 if(status.mode==='floating')return status.reason?.includes('fallback')?'widget.fallback':status.visibility===1?'widget.floating_visible':'widget.verifying';
 if(status.mode==='tray'&&status.reason==='Tray-only selected')return 'widget.tray';
 if(!status.reason&&status.helper_running)return 'widget.verifying';
 return status.reason?.includes('space')?'widget.no_space':'widget.failed';
}
function renderWidgetSettings(c){
 const cfg=state.config||{},mode=el('select',{'aria-label':tr("ui.431737ccb5fa")},... [['embedded',tr("ui.f8e1e432a03d")],['floating',tr("ui.bb0654f809d7")],['tray',tr("ui.49fc88224f86")]].map(([value,title])=>el('option',{value},title)));mode.value=cfg.taskbar_mode||'embedded';
 const monitor=el('input',{type:'number',min:'0',max:'32',value:cfg.taskbar_monitor||0,'aria-label':tr("ui.fac15462c14d")}),status=state.taskbar_widget||{};
 const statusKey=widgetStatusKey(status);
 const background=el('select',{'aria-label':tr('widget.background')},el('option',{value:'transparent'},tr('widget.transparent')),el('option',{value:'subtle'},tr('widget.subtle')));background.value=cfg.taskbar_background||'transparent';
 const feedback=el('p',{class:'muted'},tr(statusKey));
 const save=el('button',{onclick:async()=>{try{await rpc('set_config',{taskbar_mode:mode.value,taskbar_monitor:Number(monitor.value),taskbar_background:background.value});feedback.textContent=tr("ui.4ea94bf6f41a");}catch(e){error(e);}}},tr("ui.63c73c4730f4"));
 c.append(panel(tr("ui.cb0e1905bb5a"),el('div',{class:'settings-grid'},el('label',{},tr("ui.8b91a131263f"),mode),el('label',{},tr("ui.1a13ea5910a0"),monitor,el('small',{},tr("ui.4d516d631c7a"))),el('label',{},tr('widget.background'),background,el('small',{},tr('widget.background_hint')))),feedback,help(tr("ui.cab87d2c1297"),el('p',{},status.reason||tr("ui.00a976529361")),el('p',{},tr("ui.45b4019204aa"))),save));
}
