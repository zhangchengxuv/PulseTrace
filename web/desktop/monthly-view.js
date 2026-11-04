'use strict';
let monthlyTraffic=null,selectedTrafficMonth='';
function monthlyArchivePath(){return monthlyTraffic?.directory||(state.data_directory?state.data_directory+'\\network-months':'正在读取');}
function archiveFolderButton(){return el('button',{class:'secondary',onclick:async()=>{try{await rpc('open_monthly_folder');}catch(e){error(e);}}},'打开月度数据文件夹');}
function renderMonthlyTraffic(c){
 const data=monthlyTraffic,t=data?.totals||{},month=data?.month||'',chooser=el('select',{'aria-label':'月度流量集合'},el('option',{value:''},'本自然月'),el('option',{value:'all'},'全部已保存月份'),...(data?.months||[]).slice().reverse().map(m=>el('option',{value:m},m)));
 chooser.value=selectedTrafficMonth;chooser.onchange=()=>{selectedTrafficMonth=chooser.value;renderKey='';refresh(true);};
 const group=el('div',{class:'cards dashboard-cards monthly-cards'},...[
  ['upload_bytes','累计上传 · 接口总量',t.has_totals],['download_bytes','累计下载 · 接口总量',t.has_totals],
  ['ipv4_up','IPv4 累计上传 · IP 观测',t.has_ip],['ipv4_down','IPv4 累计下载 · IP 观测',t.has_ip],
  ['ipv6_up','IPv6 累计上传 · IP 观测',t.has_ip],['ipv6_down','IPv6 累计下载 · IP 观测',t.has_ip]
 ].map(([key,label,has])=>card(label,has?bytes(t[key]):'暂无数据',has?fmt(t[key])+' 字节':'尚无已保存的有效样本')));
 const quality=t.quality==='incomplete_observation'?'不完整观测':t.has_ip?'待独立对照验证':'暂无有效 IP 观测';
 c.append(panel('已使用流量 · '+(month==='all'?'全部已保存月份':month||'本自然月'),el('div',{class:'monthly-toolbar'},el('label',{},'自然月集合 ',chooser),archiveFolderButton()),group,
  el('p',{class:'muted'},'本区域独立于上方时间筛选。仅累计启用监测时的所选接口流量，不叠加 TUN；总量是接口 octet，IPv4/IPv6 是 IP 长度观测，二者口径不同，不能相加或当作校园网账单。'),
  el('p',{class:'muted'},'IP 质量：'+quality+'；拒收 '+fmt(t.rejected)+'，事件/缓冲丢失诊断 '+fmt(t.events_lost)+' / '+fmt(t.buffers_lost)+'。仅显示已持久化批次，按设置周期更新；'+(data?.last_flush_ms?'最近提交 '+new Date(data.last_flush_ms).toLocaleString():'等待首次提交')+'。'),
  el('div',{class:'archive-path'},el('span',{},'SQLite 集合：'),el('code',{},data?.database_file||monthlyArchivePath())),
  el('div',{class:'archive-path'},el('span',{},'可读 JSON：'),el('code',{},data?.summary_file||monthlyArchivePath())),
  el('p',{class:'muted'},'按 '+(data?.windows_timezone||'归档时区')+' 的自然月保存；未产生网络样本时不会创建空月份文件。分钟明细的保留天数不影响这些月度文件。')));
}
