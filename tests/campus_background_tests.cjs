// Only synthetic loopback traffic and isolated data; monitoring and app usage stay disabled.
const fs=require('fs'),path=require('path'),http=require('http'),assert=require('assert/strict'),{spawn}=require('child_process'),{DatabaseSync}=require('node:sqlite');
const exe=path.resolve(process.argv[2]),root=path.resolve(process.argv[3],String(Date.now()));
let requests=0,expired=false;
const server=http.createServer((request,response)=>{requests++;response.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Connection':'close'});response.end(expired?'<input type="password"><div id="balance">Balance 99.00</div>':'<table><tr><th>Balance</th><td id="balance">27.50 CNY</td></tr><tr><th>Used data</th><td id="used">2 GiB</td></tr></table>');});
async function run(enabled){
 fs.writeFileSync(path.join(root,'config.json'),JSON.stringify({theme:'dark',language:'en-US',campus_enabled:enabled,campus_portal_url:url,check_updates_on_start:false,capture_on_launch:false,usage_enabled:false,taskbar_mode:'tray'}));
 await new Promise((resolve,reject)=>{
  const child=spawn(exe,['--background','--core-seconds','4','--data-dir',root],{windowsHide:true,stdio:'ignore'});
  const timer=setTimeout(()=>{child.kill();reject(Error('Isolated campus background test timed out'));},30000);
  child.on('error',error=>{clearTimeout(timer);reject(error);});child.on('exit',code=>{clearTimeout(timer);code===0?resolve():reject(Error('Campus background process exited with '+code));});
 });
}
function latest(){const db=new DatabaseSync(path.join(root,'monitor.db'),{readOnly:true});try{return db.prepare('SELECT observed_ms,balance,used_bytes FROM campus_snapshots ORDER BY observed_ms DESC LIMIT 1').get();}finally{db.close();}}
let url;
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));url=`http://127.0.0.1:${server.address().port}/usage`;
 fs.mkdirSync(path.join(root,'campus'),{recursive:true});
 const fields={balance:{mode:'auto',selector:'#balance',kind:'currency',unit:'CNY',confidence:1},used_bytes:{mode:'auto',selector:'#used',kind:'bytes',unit:'B',confidence:1}};
 fs.writeFileSync(path.join(root,'campus/rules.json'),JSON.stringify({format:1,portals:[{origin:new URL(url).origin,portal_url:url,fields}]}));
 await run(true);assert.ok(requests>0,'confirmed rule restores a hidden portal at normal app startup');
 const saved=latest();assert.equal(saved.balance,27.5);assert.equal(saved.used_bytes,2*1024**3);
 requests=0;await run(false);assert.equal(requests,0,'disabled campus monitoring must make no portal requests');assert.deepEqual(latest(),saved,'disabling preserves the last valid snapshot');
 requests=0;expired=true;await run(true);assert.ok(requests>0);assert.deepEqual(latest(),saved,'expired authentication must not overwrite the last valid snapshot');
 console.log('Campus background: normal startup restore, hidden query/persistence, disabled zero requests and expired-login preservation passed');
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(()=>server.close());
