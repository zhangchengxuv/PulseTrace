'use strict';
function protocolSelection(){const id=state.network?.selected;return protocolSamples.filter(r=>r.interface_id===id);}
function protocolTotal(records,key){return records.reduce((n,r)=>n+Number(r[key]||0),0);}
function protocolTrend(title=tr("ui.ed82077242cc")){
 const id=state.network?.selected||'',actual=PulseDashboard.protocolRows(protocolSamples,id);
 return trend('network_ip',null,title,[[id+'/ipv4_up',tr("ui.59a323e0fe04"),'--protocol-ipv4',[]],[id+'/ipv4_down',tr("ui.c0159992fdcc"),'--protocol-ipv4',[6,4]],[id+'/ipv6_up',tr("ui.31e3f3c697c3"),'--protocol-ipv6',[]],[id+'/ipv6_down',tr("ui.147a719fb501"),'--protocol-ipv6',[6,4]]],actual);
}
function networkTransferCard(name,values,rates,missingRate,scope,series=''){
 const [up,down]=values,total=up==null||down==null?null:up+down;
 const directions=[['↑','ui.9e07e3c0532d','primary'],['↓','ui.4673a2306165','comparison']];
 const format=value=>value==null?'—':bytes(value);
 const detail=el('div',{class:'network-directions'},...directions.map(([arrow,label,kind],index)=>{
  const rate=Number.isFinite(rates[index])?bytes(rates[index])+'/s':missingRate;
  return el('div',{'data-series':kind},
   el('span',{},arrow+' '+tr(label)),el('b',{},format(values[index])),el('small',{title:tr('network.current')},rate));
 }));
 const track=el('div',{class:'network-direction-track','aria-hidden':'true'});
 if(total>0){
  for(const value of values){const segment=el('i');segment.style.width=(value/total*100)+'%';track.append(segment);}
 }
 return el('article',{class:'card network-transfer','data-series':series,title:scope},
  el('div',{class:'network-transfer-heading'},el('span',{},name),el('strong',{},format(total))),detail,track);
}
function renderProtocolNetwork(c){
 const n=state.network||{},p=n.protocol_capture||{},fault=state.network_error;
 const records=protocolSelection(),summary=PulseDashboard.protocol(records),chosen=(n.interfaces||[]).find(item=>item.id===n.selected);
 const valid=records.filter(item=>item.accepted>0);
 const interfaceValues=['interface_upload_bytes','interface_download_bytes'].map(metric=>
  rows.some(row=>row.module==='network'&&row.metric===metric)?sum('network',metric):null);
 const observed=(family,direction)=>summary.has?protocolTotal(valid,family+'_'+direction):null;
 const paused=!state.monitoring||state.suspended||state.quiescing;
 const rateStatus=fault?tr('ui.6aa27f14bff1'):paused?tr('ui.eb0c326b60ae'):null;
 const waiting=tr('network.rate_waiting');
 const linkRates=!rateStatus&&n.interface_rates_ready?[n.upload_bps,n.download_bps]:[];
 const ipRates=!rateStatus&&p.running&&!p.error&&p.interface_id===n.selected?p.rates||[]:[];
 const permissionDenied=p.start_status===5||p.enable_status===5;
 const protocolIssue=!rateStatus&&p.error?(permissionDenied?tr('network.ip_live_permission'):tr('network.ip_live_failed')):null;
 const ipStatus=rateStatus||protocolIssue||(p.rates_incomplete?tr('network.ip_live_incomplete'):waiting);
 const ipScope=tr('network.ip_scope',{name:chosen?.name||'—'});
 c.classList.add('network-content');
 if(fault)c.append(el('div',{class:'capture-error',role:'alert'},tr('ui.ee0a3b2108eb')+fault));
 if(protocolIssue)c.append(el('p',{class:'network-scope',role:'status',title:p.error},protocolIssue));
 const rangeText=range.from===0?tr('range.all'):formatTime(range.from)+' – '+formatTime(range.to);
 c.append(el('div',{class:'network-heading'},
  el('p',{class:'network-scope'},tr('network.range')+' · '+rangeText),
  el('p',{class:'network-scope',title:ipScope},tr('network.current')+' · '+(chosen?.name||'—'))));
 c.append(el('div',{class:'network-overview'},
  networkTransferCard(tr('network.interface_total'),interfaceValues,linkRates,rateStatus||waiting,tr('calendar.octets')),
  networkTransferCard('IPv4',['up','down'].map(direction=>observed('ipv4',direction)),ipRates.slice(0,2),ipStatus,ipScope,'primary'),
  networkTransferCard('IPv6',['up','down'].map(direction=>observed('ipv6',direction)),ipRates.slice(2,4),ipStatus,ipScope,'comparison')));
 const share=el('div',{class:'network-ip-share'});
 if(summary.ratio){
  const track=el('div',{class:'network-share-track','aria-hidden':'true'},el('i'),el('i'));
  track.children[0].style.width=summary.ratio[0]*100+'%';track.children[1].style.width=summary.ratio[1]*100+'%';
  share.append(el('span',{'data-series':'primary'},'IPv4 '+(summary.ratio[0]*100).toFixed(1)+'%'),track,el('span',{'data-series':'comparison'},'IPv6 '+(summary.ratio[1]*100).toFixed(1)+'%'));
 }
 share.append(el('small',{class:'network-quality'},summary.quality));c.append(share);
 c.append(el('div',{class:'network-trends'},protocolTrend(),trend('network',null,tr('network.interface_trend'))));
 const campus=PulseCampus.networkPanel();if(campus)c.append(campus);
 renderTrafficCalendar(c);
 const archive=el('div',{class:'network-archive'});renderMonthlyTraffic(archive);c.append(disclosure('network.archives',tr('network.archives'),monthlyTraffic?.months?.length||0,archive));
 c.append(disclosure('network.interfaces',tr('ui.f4964357f245'),n.interfaces?.length||0,pagedTable('network.interfaces.rows',[tr('ui.d44e9b3d3b31'),tr('ui.ba40014ff496'),tr('ui.6320b4a8722a'),tr('ui.c80d519245a7'),'IPv4','IPv6'],n.interfaces||[],x=>[x.name,x.type,x.up?tr('ui.5be0323e8adc'):tr('ui.3d52574ce150'),x.physical?tr('ui.a454f17872a1'):tr('ui.9d82dd6c9d3d'),x.ipv4_configured?tr('ui.de8184da1ef8'):tr('ui.80a57e03f071'),x.ipv6_configured?tr('ui.de8184da1ef8'):tr('ui.80a57e03f071')])));
 c.append(disclosure('network.connections',tr('ui.490156be6d55'),networkEvents.length,el('p',{class:'muted'},tr('network.events_limit')),pagedTable('network.connections.rows',[tr('ui.8b6ff498515b'),tr('ui.c80d519245a7'),tr('ui.6320b4a8722a')],networkEvents,e=>[new Date(e.observed_ms).toLocaleString(PulseI18n.locale),e.name||e.interface_id,connectionLabel(e.kind)])));
 c.append(disclosure('network.diagnostics',tr('ui.5dae149d3cd1'),records.length,table([tr('ui.79f326be4409'),tr('ui.6320b4a8722a')],[[tr('ui.a488e93d69cc'),p.source||'ETW NDIS-PacketCapture'],[tr('ui.56410fc65314'),p.start_status??'—'],[tr('ui.f4f0ead1116b'),p.enable_status??'—'],[tr('ui.e99a6717b691'),p.process_status??'—'],[tr('ui.7c95fe220d4e'),fmt(p.events_lost)],[tr('ui.452bd1087597'),fmt(p.buffers_lost)],[tr('ui.e3469bdc289e'),JSON.stringify(p.rejection_reasons||[])]]),pagedTable('network.diagnostics.rows',[tr('ui.8b6ff498515b'),tr('ui.c80d519245a7'),tr('ui.55847d2e5bed'),tr('ui.16ccab7ab0be'),tr('ui.94c099b8f21b')],[...records].reverse(),r=>[formatTime(r.minute),r.interface_id,r.quality==='incomplete_observation'?tr('ui.3f955ae5b843'):tr('ui.76a2e875d985'),fmt(r.accepted)+' / '+fmt(r.rejected),fmt(r.events_lost)+' / '+fmt(r.buffers_lost)])));
}
