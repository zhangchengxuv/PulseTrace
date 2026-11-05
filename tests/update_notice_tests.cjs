const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
let checks=0;const check=(v,m)=>{checks++;assert.ok(v,m);};
const context=vm.createContext({window:{addEventListener(){}},setTimeout,clearTimeout});vm.runInContext(fs.readFileSync('web/desktop/update-notice.js','utf8'),context);const Controller=context.PulseOpenUpdates;
(async()=>{
 let calls=[],notices=[],release;let blocked=new Promise(r=>release=r);
 const c=new Controller(async type=>{calls.push(type);if(type==='check_updates'){await blocked;return {phase:'available',latest:'9.0.0'};}return {phase:'idle'};},s=>notices.push(s));
 const a=c.open(),b=c.open();check(a===b,'concurrent opens share one operation');release();await a;check(calls.filter(x=>x==='check_updates').length===1&&notices.length===1,'one check and one notice');
 const silent=new Controller(async()=>({phase:'up_to_date'}),()=>{throw Error('Unexpected notice');});await silent.open();check(!silent.lastError,'no update is silent');
 const failure=new Controller(async()=>{throw Error('offline');},()=>{});await failure.open();check(failure.lastError==='offline','offline stays nonfatal');
 let completed=0,waitRelease;const delay=new Promise(r=>waitRelease=r),hidden=new Controller(async type=>type==='check_updates'?{phase:'checking',busy:true}:{phase:'idle'},()=>completed++,()=>delay);
 const pending=hidden.open();await new Promise(r=>setImmediate(r));hidden.stop();waitRelease();await pending;check(completed===0,'hidden window does not receive a stale dialog');
 const busyCalls=[],busy=new Controller(async type=>{busyCalls.push(type);return {busy:true,phase:'downloading'};},()=>{});await busy.open();check(busyCalls.length===1,'active download not interrupted by open check');
 let polls=0,busyNotices=0;const inProgress=new Controller(async()=>++polls===1?{busy:true,phase:'checking'}:{busy:false,phase:'available',latest:'9.0.0'},()=>busyNotices++,async()=>{});await inProgress.open();check(polls===2&&busyNotices===1,'existing startup check reused');
 let unblock;const held=new Promise(r=>unblock=r),seen=[];let apiCalls=0;
 const reopened=new Controller(async()=>++apiCalls===1?await held:{phase:'up_to_date'},v=>seen.push(v));const old=reopened.open();reopened.stop();const fresh=reopened.open();check(old!==fresh,'rapid reopen gets a fresh generation');await fresh;unblock({phase:'available',latest:'stale'});await old;check(!seen.length,'old result cannot surface after reopening');
 console.log(checks+' open/update concurrency and visibility checks passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
