'use strict';
function protocolSelection(){const id=state.network?.selected;return protocolSamples.filter(r=>r.interface_id===id);}
function protocolTotal(records,key){return records.reduce((n,r)=>n+Number(r[key]||0),0);}
function protocolTrend(title='IP 流量趋势'){
 const id=state.network?.selected||'';const valid=new Set(protocolSelection().filter(r=>r.accepted>0).map(r=>r.minute)),actual=rows.filter(r=>r.module==='network_ip'&&valid.has(r.minute)&&r.metric.startsWith(id+'/'));
 return trend('network_ip',null,title,[[id+'/ipv4_up','IPv4 上传','--protocol-ipv4',[]],[id+'/ipv4_down','IPv4 下载','--protocol-ipv4',[6,4]],[id+'/ipv6_up','IPv6 上传','--protocol-ipv6',[]],[id+'/ipv6_down','IPv6 下载','--protocol-ipv6',[6,4]]],actual);
}
function renderProtocolNetwork(c){
 const textCard=(...args)=>Object.assign(card(...args),{className:'card text-value'}),n=state.network||{},p=n.protocol_capture||{},fault=state.network_error||p.error,records=protocolSelection(),has=records.some(r=>r.accepted>0),chosen=(n.interfaces||[]).find(x=>x.id===n.selected),v4=protocolTotal(records,'ipv4_up')+protocolTotal(records,'ipv4_down'),v6=protocolTotal(records,'ipv6_up')+protocolTotal(records,'ipv6_down'),quality=p.quality==='incomplete_observation'||records.some(x=>x.quality==='incomplete_observation')?'不完整观测':has?'待验证':'暂无数据';
 if(fault)c.append(el('div',{class:'capture-error',role:'alert'},'采集失败：'+fault));
 c.append(el('div',{class:'cards dashboard-cards'},textCard('统计接口',chosen?.name||'未选择',chosen?.physical?'物理接口':'虚拟接口'),textCard('采集状态',fault?'读取失败':p.running?'正在采集':'已暂停'),textCard('IP 数据质量',quality,'观测值 · 非计费数据')));
 const labels=[['ipv4_up','IPv4 上传'],['ipv4_down','IPv4 下载'],['ipv6_up','IPv6 上传'],['ipv6_down','IPv6 下载']];
 c.append(el('h3',{},'所选范围'),el('div',{class:'cards'},...labels.map(([key,label],i)=>card(label,has?bytes(protocolTotal(records,key)):'暂无数据',fault?'实时速率：采集失败':!p.running?'实时速率：已暂停':p.rates?.[i]==null?'实时速率：等待采样':'实时速率：'+bytes(p.rates[i])+'/s'))));
 c.append(panel('IP 占比 · 观测值',el('p',{class:'muted'},has&&v4+v6?'IPv4 '+(v4/(v4+v6)*100).toFixed(1)+'% · IPv6 '+(v6/(v4+v6)*100).toFixed(1)+'%':'暂无数据')),
 panel('系统连接',table(['指标','状态'],[['上传速率',state.network_error?'读取失败':state.monitoring?bytes(n.upload_bps||0)+'/s':'已暂停'],['下载速率',state.network_error?'读取失败':state.monitoring?bytes(n.download_bps||0)+'/s':'已暂停'],['IPv4（系统）',n.ipv4_internet===true?'互联网':n.ipv4_internet===false?'无互联网提示':'未知'],['IPv6（系统）',n.ipv6_internet===true?'互联网':n.ipv6_internet===false?'无互联网提示':'未知']]),el('p',{class:'muted'},'系统提示，仅供参考。')),protocolTrend(),
 panel('网络接口',table(['名称','类型','状态','接口','IPv4','IPv6'],(n.interfaces||[]).map(x=>[x.name,x.type,x.up?'已连接':'未连接',x.physical?'物理':'虚拟',x.ipv4_configured?'已配置':'未配置',x.ipv6_configured?'已配置':'未配置']))),
 panel('连接记录',table(['时间','接口','状态'],networkEvents.slice(0,500).map(e=>[new Date(e.observed_ms).toLocaleString(),e.name||e.interface_id,connectionLabel(e.kind)]))),
 help('采集诊断',table(['项目','状态'],[['来源',p.source||'ETW NDIS-PacketCapture'],['启动',p.start_status??'未知'],['启用',p.enable_status??'未知'],['处理',p.process_status??'未知'],['事件丢失',fmt(p.events_lost)],['缓冲丢失',fmt(p.buffers_lost)],['拒收分类',JSON.stringify(p.rejection_reasons||[])]]),
 table(['时间','接口','质量','有效 / 拒收','事件 / 缓冲丢失'],records.slice(-100).reverse().map(r=>[formatTime(r.minute),r.interface_id,r.quality==='incomplete_observation'?'不完整观测':'待验证',fmt(r.accepted)+' / '+fmt(r.rejected),fmt(r.events_lost)+' / '+fmt(r.buffers_lost)]))));
}
