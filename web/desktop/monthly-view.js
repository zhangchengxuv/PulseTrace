'use strict';
let monthlyTraffic=null,selectedTrafficMonth='';
function archiveFolderButton(){return el('button',{class:'secondary',onclick:async()=>{try{await rpc('open_monthly_folder');}catch(e){error(e);}}},'打开文件夹');}
function renderMonthlyTraffic(c){
 const data=monthlyTraffic,t=data?.totals||{},month=data?.month||'',chooser=el('select',{'aria-label':'月份'},el('option',{value:''},'本月'),el('option',{value:'all'},'全部月份'),...(data?.months||[]).slice().reverse().map(m=>el('option',{value:m},m)));
 chooser.value=selectedTrafficMonth;chooser.onchange=()=>{selectedTrafficMonth=chooser.value;renderKey='';refresh(true);};
 const group=el('div',{class:'cards dashboard-cards monthly-cards'},...[
  ['upload_bytes','上传总量',t.has_totals],['download_bytes','下载总量',t.has_totals],
  ['ipv4_up','IPv4 上传',t.has_ip],['ipv4_down','IPv4 下载',t.has_ip],
  ['ipv6_up','IPv6 上传',t.has_ip],['ipv6_down','IPv6 下载',t.has_ip]
 ].map(([key,label,has])=>Object.assign(card(label,has?bytes(t[key]):'暂无数据'),{title:has?fmt(t[key])+' 字节':'暂无数据'})));
 const quality=t.quality==='incomplete_observation'?'不完整观测':t.has_ip?'待验证':'暂无数据';
 c.append(panel('月度流量 · '+(month==='all'?'全部月份':month||'本月'),el('div',{class:'monthly-toolbar'},chooser,archiveFolderButton()),group,
  el('p',{class:'muted'},'IP：'+quality+' · '+(data?.last_flush_ms?'已保存 '+new Date(data.last_flush_ms).toLocaleTimeString('zh-CN'):'等待保存')),
  el('p',{class:'muted'},'总量按接口统计；IP 分项为观测值，非计费数据。')));
}
