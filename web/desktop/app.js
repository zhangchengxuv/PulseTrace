'use strict';
let sequence=0;const pending=new Map();
function rpc(type,params={}){return new Promise((resolve,reject)=>{const request_id=++sequence;const timer=setTimeout(()=>{pending.delete(request_id);reject(new Error('核心请求超时'));},10000);pending.set(request_id,{resolve,reject,timer});chrome.webview.postMessage({type,request_id,params});});}
chrome.webview.addEventListener('message',event=>{const msg=event.data;if(msg.type==='navigate'){page=msg.page;render();return;}const p=pending.get(msg.request_id);if(!p)return;clearTimeout(p.timer);pending.delete(msg.request_id);msg.ok?p.resolve(msg.data):p.reject(new Error(msg.error?.message||'请求失败'));});
const $=id=>document.getElementById(id);let state={},page='dashboard';
function error(e){$('error').hidden=false;$('error').textContent=e.message;}
async function render(){try{state=await rpc('get_status');$('state').textContent=state.monitoring?'正在监测':'已暂停';$('toggle').textContent=state.monitoring?'暂停监测':'开始监测';$('title').textContent=page;const c=$('content');c.replaceChildren();const p=document.createElement('p');p.textContent='WebView2 双向通信已连接。鼠标本次会话点击 '+state.mouse_clicks_session+'，键盘 '+state.keyboard_presses_session+'。';c.append(p);}catch(e){error(e);}}
for(const name of ['dashboard','network','keyboard','mouse','history','settings']){const b=document.createElement('button');b.textContent=name;b.onclick=()=>{page=name;render();};$('navigation').append(b);}
$('toggle').onclick=async()=>{try{await rpc('set_monitoring',{enabled:!state.monitoring});render();}catch(e){error(e);}};
rpc('ping').then(render).catch(error);
