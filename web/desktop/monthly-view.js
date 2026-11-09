'use strict';
let monthlyTraffic=null,selectedTrafficMonth='all';
function archiveFolderButton(){return el('button',{class:'secondary',onclick:async()=>{try{await rpc('open_monthly_folder');}catch(e){error(e);}}},tr("ui.fcf8b4bff0df"));}
function renderMonthlyTraffic(c){
 const data=monthlyTraffic,t=data?.totals||{},month=data?.month||'',chooser=el('select',{'aria-label':tr("ui.a27822d13506")},el('option',{value:''},tr("ui.0eeecd26f2ba")),el('option',{value:'all'},tr("ui.660f69de1a7f")),...(data?.months||[]).slice().reverse().map(m=>el('option',{value:m},m)));
 chooser.value=selectedTrafficMonth;chooser.onchange=()=>{selectedTrafficMonth=chooser.value;renderKey='';refresh(true);};
 const group=el('div',{class:'cards dashboard-cards monthly-cards'},...[
  ['upload_bytes',tr("ui.251aa58cf232"),t.has_totals],['download_bytes',tr("ui.f83e7ef6f9c5"),t.has_totals],
  ['ipv4_up',tr("ui.59a323e0fe04"),t.has_ip],['ipv4_down',tr("ui.c0159992fdcc"),t.has_ip],
  ['ipv6_up',tr("ui.31e3f3c697c3"),t.has_ip],['ipv6_down',tr("ui.147a719fb501"),t.has_ip]
 ].map(([key,label,has])=>Object.assign(card(label,has?bytes(t[key]):tr("ui.497c85690c4c")),{title:has?fmt(t[key])+tr("ui.f5d87a8b74a0"):tr("ui.497c85690c4c")})));
 const quality=t.quality==='incomparable_bases'?tr('ui.013c99479178'):t.quality==='incomplete_observation'?tr("ui.3f955ae5b843"):t.has_ip?tr("ui.76a2e875d985"):tr("ui.497c85690c4c");
 c.append(panel(tr("ui.a1d2f7491156")+(month==='all'?tr("ui.660f69de1a7f"):month||tr("ui.0eeecd26f2ba")),el('div',{class:'monthly-toolbar'},chooser,archiveFolderButton()),group,
  el('p',{class:'muted'},'IP：'+quality+' · '+(data?.last_flush_ms?tr("ui.c2e69b675c2e")+new Date(data.last_flush_ms).toLocaleTimeString(PulseI18n.locale):tr("ui.58b2e85e16bd"))),
  help(tr('ui.details'),el('p',{},tr("ui.de51491d7c16")))));
}
