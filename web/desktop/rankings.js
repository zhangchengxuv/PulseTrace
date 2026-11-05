'use strict';
// Filters and configuration DOM survive normal live-data refreshes.
const rankingView={kind:'app',source:'windows_etw',initialized:false,family:0,sort:'total',limit:10,search:'',visible:100,offset:0,root:null,locale:'',result:null,revision:0,icons:new Map()};
function rankLabel(key){return tr('ranking.'+key);}
function rankingControls(){
 const v=rankingView,controls=el('div',{class:'ranking-controls'});
 const select=(name,options,current,change)=>{const input=el('select',{'aria-label':rankLabel(name)},options.map(([value,label])=>el('option',{value},label)));input.value=String(current);input.onchange=()=>{change(input.value);v.visible=100;v.offset=0;refreshTrafficRankings();};return el('label',{},el('span',{},rankLabel(name)),input);};
 controls.append(select('type',[['app',rankLabel('apps')],['domain',rankLabel('domains')]],v.kind,x=>{v.kind=x;if(x==='domain')v.source='mihomo';v.root=null;renderKey='';refresh(true);}),
 select('source',[['windows_etw',rankLabel('windows')],['mihomo','Mihomo']],v.source,x=>{v.source=x;}),
 select('family',[[0,rankLabel('all')],[4,'IPv4'],[6,'IPv6']],v.family,x=>v.family=Number(x)),
 select('sort',[['total',rankLabel('total')],['upload',rankLabel('upload')],['download',rankLabel('download')],['name',rankLabel('name')]],v.sort,x=>v.sort=x),
 select('limit',[[5,'5'],[10,'10'],[20,'20'],[0,rankLabel('all')]],v.limit,x=>v.limit=Number(x)));
 const search=el('input',{type:'search',value:v.search,placeholder:rankLabel('search'),'aria-label':rankLabel('search'),oninput:e=>{v.search=e.target.value;v.visible=100;v.offset=0;drawRankingRows();}});controls.append(el('label',{class:'ranking-search'},el('span',{},rankLabel('search')),search));
 if(v.kind==='domain')controls.querySelectorAll('select')[1].disabled=true;return controls;
}
function rankingConfiguration(){
 const cfg=state.config||{},form=el('form',{class:'ranking-config settings-grid'}),fields={};
 for(const [key,label]of [['ranking_windows_enabled','enable_windows'],['ranking_mihomo_enabled','enable_mihomo'],['ranking_domains_enabled','enable_domains']]){fields[key]=el('input',{type:'checkbox',checked:!!cfg[key]});form.append(el('label',{class:'checkbox'},fields[key],rankLabel(label)));}
 fields.mihomo_controller=el('input',{type:'text',value:cfg.mihomo_controller||'',placeholder:'http://127.0.0.1:9090',autocomplete:'off',spellcheck:'false'});
 fields.ranking_retention_days=el('input',{type:'number',min:1,max:365,value:cfg.ranking_retention_days||30});
 const secret=el('input',{type:'password',autocomplete:'new-password',maxlength:1024,placeholder:rankLabel('secret_hint')}),feedback=el('span',{class:'muted',role:'status'});
 form.append(el('label',{},rankLabel('controller'),fields.mihomo_controller),el('label',{},rankLabel('secret'),secret),el('label',{},rankLabel('retention'),fields.ranking_retention_days),el('button',{type:'submit'},rankLabel('save')));
 form.onsubmit=async e=>{e.preventDefault();try{const next={};for(const [key,input]of Object.entries(fields))next[key]=input.type==='checkbox'?input.checked:input.type==='number'?Number(input.value):input.value.trim();
  if(secret.value){await rpc('set_mihomo_secret',{secret:secret.value});secret.value='';}await rpc('set_config',next);state.config={...state.config,...next};feedback.textContent=rankLabel('saved');await refreshTrafficRankings();
 }catch(e){feedback.textContent=rankLabel('configuration_error');}};
 const forget=el('button',{type:'button',class:'secondary',onclick:async()=>{try{await rpc('set_mihomo_secret',{secret:''});secret.value='';feedback.textContent=rankLabel('secret_cleared');}catch(e){feedback.textContent=rankLabel('configuration_error');}}},rankLabel('forget_secret'));
 const clear=el('button',{type:'button',class:'danger',onclick:async()=>{if(!confirm(rankLabel('clear_confirm')))return;try{await rpc('clear_ranking_domains',{confirmation:'DELETE'});feedback.textContent=rankLabel('cleared');await refreshTrafficRankings();}catch(e){feedback.textContent=rankLabel('pause_required');}}},rankLabel('clear_domains'));
 form.append(el('div',{class:'ranking-actions'},forget,clear),feedback);
 return help(rankLabel('configure'),el('p',{},rankLabel('privacy')),el('p',{},rankLabel('credentials')),form);
}
function renderTrafficRankings(container){
 const v=rankingView;if(!v.root||v.locale!==PulseI18n.locale){v.locale=PulseI18n.locale;v.root=panel(rankLabel('title'),el('p',{class:'muted'},rankLabel('subtitle')),rankingControls(),el('div',{class:'ranking-status',role:'status'}),el('div',{class:'ranking-list'}),el('div',{class:'ranking-foot muted'}),rankingConfiguration());v.root.classList.add('traffic-ranking');}
 container.append(v.root);drawRankingRows();
}
async function refreshTrafficRankings(){
 if(page!=='network'||document.hidden||!nativeVisible)return;const v=rankingView,revision=++v.revision;
 if(!v.initialized){v.initialized=true;if(state.config?.ranking_mihomo_enabled&&!state.config?.ranking_windows_enabled)v.source='mihomo';}
 try{const result=await rpc('get_rankings',{from:range.from,to:range.to,source:v.source,kind:v.kind,family:v.family});if(revision!==v.revision)return;v.result=result;drawRankingRows();}
 catch(e){if(revision===v.revision&&v.root)v.root.querySelector('.ranking-status').textContent=rankLabel('query_error');}
}
function rankingSelection(result,view){
 const metric=view.sort==='name'?'total':view.sort,denominator=result.rows.reduce((sum,r)=>sum+Number(r[metric]||0),0),search=view.search.toLocaleLowerCase();
 const all=result.rows.filter(r=>(r.name||r.key).toLocaleLowerCase().includes(search));
 all.sort((a,b)=>view.sort==='name'?(a.name||a.key).localeCompare(b.name||b.key):Number(b[metric])-Number(a[metric])||(a.key<b.key?-1:1));
 const size=view.limit||view.visible,offset=view.limit?0:Math.min(view.offset||0,Math.floor(Math.max(0,all.length-1)/size)*size);
 return {rows:all.slice(offset,offset+size),offset,matched:all.length,denominator,metric};
}
function drawRankingRows(){
 const v=rankingView;if(!v.root)return;const list=v.root.querySelector('.ranking-list'),status=v.root.querySelector('.ranking-status'),foot=v.root.querySelector('.ranking-foot'),result=v.result;
 if(!result){list.replaceChildren(el('p',{class:'empty'},rankLabel('empty')));return;}
 const drawKey=JSON.stringify({result,kind:v.kind,source:v.source,family:v.family,sort:v.sort,limit:v.limit,search:v.search,offset:v.offset,locale:PulseI18n.locale,from:range.from,to:range.to});if(v.drawKey===drawKey&&list.childElementCount)return;v.drawKey=drawKey;
 const source=result.source||v.source,provider=result.status?.[source]||{},statusKey=!result.status?.monitoring?'paused':v.kind==='domain'&&!result.status?.domains_enabled?'disabled':provider.state||'disabled';
 status.replaceChildren(el('span',{},(source==='mihomo'?'Mihomo':rankLabel('windows'))+' · '+rankLabel('state_'+statusKey)),el('small',{},rankLabel(source==='mihomo'?'basis_mihomo':'basis_windows')));
 if(result.status?.capacity_events)status.append(el('strong',{},rankLabel('quality_32')));
 const {rows:items,offset,matched,denominator,metric}=rankingSelection(result,v);v.offset=offset;list.replaceChildren();const extra=el('div');
 if(!items.length)list.append(el('p',{class:'empty'},v.kind==='domain'&&source!=='mihomo'?rankLabel('domain_source'):rankLabel('empty')));
 items.forEach((r,index)=>{
  const name=r.key==='unknown'?rankLabel(v.kind==='domain'?'unknown_target':'unknown_app'):r.key==='overflow'?rankLabel('overflow'):r.name||r.key;
  const proportion=denominator>0?Number(r[metric])/denominator*100:0;
  const icon=el('span',{class:'ranking-icon','aria-hidden':'true'},v.kind==='domain'?'◎':name.slice(0,1));
  const cached=v.icons.get(source+'/'+r.key);if(cached)icon.replaceChildren(el('img',{src:cached,alt:''}));
  const rate=x=>x==null?'—':bytes(x)+'/s',details=el('div',{class:'ranking-detail'},el('span',{},'↑ '+bytes(r.upload)),el('span',{},'↓ '+bytes(r.download)),el('span',{},rankLabel('now')+': ↑ '+rate(r.up_bps)+' · ↓ '+rate(r.down_bps)));
  const quality=[1,2,4,8,16,32].filter(bit=>r.quality&bit).map(bit=>rankLabel('quality_'+bit)).join(' · ');
  const row=el('article',{class:'ranking-row',tabindex:0,title:[name,r.path,rankLabel('attribution_'+r.attribution),quality,'IPv4 ↑ '+bytes(r.ipv4_up)+' ↓ '+bytes(r.ipv4_down),'IPv6 ↑ '+bytes(r.ipv6_up)+' ↓ '+bytes(r.ipv6_down),rankLabel('unknown_family')+': '+bytes(r.unknown_family_bytes)].filter(Boolean).join('\n')},
   el('span',{class:'ranking-position'},index+1),icon,el('div',{class:'ranking-body'},el('div',{class:'ranking-heading'},el('strong',{},name),el('span',{class:'ranking-value'},bytes(r.total))),details,el('div',{class:'ranking-track',role:'meter','aria-label':rankLabel('share'),'aria-valuemin':0,'aria-valuemax':100,'aria-valuenow':proportion.toFixed(2)},el('div',{class:'ranking-bar'})),el('small',{class:'muted'},proportion.toFixed(1)+'% · '+rankLabel('attribution_'+r.attribution))));
  row.querySelector('.ranking-bar').style.width=Math.min(100,proportion)+'%';
  (index<5?list:extra).append(row);
  if(v.kind==='app'&&index<20&&!v.icons.has(source+'/'+r.key)&&r.key.startsWith('exe:')){const key=source+'/'+r.key;v.icons.set(key,'');rpc('get_ranking_icon',{source,key:r.key}).then(({icon:image})=>{if(image?.startsWith('data:image/png;base64,')){v.icons.set(key,image);if(icon.isConnected)icon.replaceChildren(el('img',{src:image,alt:''}));}if(v.icons.size>128)v.icons.delete(v.icons.keys().next().value);}).catch(()=>{});}
 });
 if(items.length>5){const fold=disclosure('ranking.extra.'+v.kind,tr('network.more'),items.length-5,extra);fold.classList.add('ranking-extra');list.append(fold);}
 if(!v.limit)list.append(el('div',{class:'actions'},el('button',{class:'secondary',disabled:!v.offset,onclick:()=>{v.offset=Math.max(0,v.offset-v.visible);drawRankingRows();}},tr('ui.c9b9ae7a6144')),el('span',{},String((v.offset||0)+1)+'–'+String((v.offset||0)+items.length)+' / '+matched),el('button',{class:'secondary',disabled:(v.offset||0)+items.length>=matched,onclick:()=>{v.offset=(v.offset||0)+v.visible;drawRankingRows();}},rankLabel('more'))));
 foot.textContent=rankLabel('range')+' · '+(range.from===0?tr('range.all'):new Date(range.from*60000).toLocaleString(PulseI18n.locale))+' – '+new Date(range.to*60000).toLocaleString(PulseI18n.locale)+' · '+rankLabel('not_additive')+' '+rankLabel('coverage');
}
async function validateTrafficRankingView(){
 if(!state.self_test)return;const v=rankingView,saved={kind:v.kind,source:v.source,family:v.family,result:v.result,root:v.root,locale:v.locale};
 const testHost=el('div',{});$('content').append(testHost);
 try{for(const kind of ['app','domain'])for(const family of [0,4,6]){
  v.kind=kind;v.source='mihomo';v.family=family;v.root=null;v.result=await rpc('get_rankings',{from:range.from,to:range.to,source:'mihomo',kind,family});testHost.replaceChildren();renderTrafficRankings(testHost);
  if(kind==='domain'&&!v.root.querySelectorAll('select')[1].disabled)throw new Error('Domain source control must be explicit');
  if(v.root.querySelectorAll('.ranking-row').length!==Math.min(v.limit||v.visible,v.result.rows.length))throw new Error('Ranking rows missing');
  for(const r of v.result.rows){if(r.kind!==kind||r.source!=='mihomo'||(family&&r.family!==family)||r.total!==r.upload+r.download||r.total!==r.ipv4_up+r.ipv4_down+r.ipv6_up+r.ipv6_down+r.unknown_family_bytes)throw new Error('Ranking source/family/totals inconsistent');}
  if(v.root.textContent.includes('ranking.'))throw new Error('Ranking translation missing');
 }}finally{testHost.remove();Object.assign(v,saved);}
}
