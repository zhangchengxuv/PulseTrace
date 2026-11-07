const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
class Node{
 constructor(tag,attrs={},children=[]){this.tag=tag;Object.assign(this,attrs);this.children=[];this.value=attrs.value||'';this.hidden=false;this.disabled=false;this.checked=false;this.isConnected=true;this.append(...children.flat().filter(n=>n!=null));}
 append(...nodes){for(const n of nodes){this.children.push(n);if(n instanceof Node)n.parent=this;}}
 replaceChildren(...nodes){this.children=[];this.append(...nodes);}
 closest(){for(let n=this;n;n=n.parent)if(n.hidden)return n;return null;}
 checkValidity(){return true;}reportValidity(){}
 get textContent(){return this.children.map(n=>typeof n==='object'?n.textContent:String(n)).join('');}
 set textContent(v){this.children=[v];}
 all(){return [this,...this.children.filter(n=>n instanceof Node).flatMap(n=>n.all())];}
}
const group=new Node('section'),calls=[];let agree=false;
const name='PulseTrace-1794017725000-0123456789abcdef0123456789abcdef-v0.6.2-rc1.ptb';
let data={secure_forms:true,phase:'idle',config:{enabled:false,automatic:false,server:'https://dav.jianguoyun.com/dav/',directory:'PulseTrace/Backups',interval_minutes:1440},files:[name],history:[],encrypted:true};
const ctx=vm.createContext({console,page:'settings',nativeVisible:true,PulseI18n:{locale:'en-US'},tr:k=>k,confirm:()=>agree,clearTimeout(){},setTimeout:()=>1,queueMicrotask,
 el:(tag,attrs,...children)=>new Node(tag,attrs,children),panel:(title,...children)=>new Node('section',{},[title,...children]),table:()=>new Node('table'),disclosure:(key,title,n,...children)=>new Node('details',{},[title,...children]),document:{hidden:false},
 rpc:async(kind,params)=>{calls.push({kind,params});if(kind==='cloud_status')return structuredClone(data);if(kind==='cloud_configure'){data.config={...data.config,...Object.fromEntries(Object.entries(params).filter(([key])=>!['account','app_password','encryption_password'].includes(key)))};data.account=params.account;data.credentials_saved=true;data.encryption_password_saved=true;delete data.restore_token;}if(kind==='cloud_confirm_restore'){if(!agree)return {cancelled:true};data.pending_restore=true;}return {};}});
vm.runInContext(fs.readFileSync('web/desktop/cloud-view.js','utf8'),ctx);ctx.group=group;vm.runInContext('renderCloudSettings(group)',ctx);
const tick=()=>new Promise(resolve=>setImmediate(resolve)),field=key=>group.all().find(n=>n['aria-label']==='cloud.'+key),button=key=>group.all().find(n=>n.tag==='button'&&n.textContent==='cloud.'+key);
(async()=>{
 await tick();assert.equal(field('server').value,'https://dav.jianguoyun.com/dav/');assert.equal(field('enabled').checked,false);assert.equal(field('app_password').type,'password');assert.equal(field('encryption_password').type,'password');
 field('account').value='synthetic@example.test';field('enabled').checked=true;field('app_password').value=' app password spaces ';field('encryption_password').value='recovery passphrase fixture';
 await group.all().find(n=>n.tag==='form').onsubmit({preventDefault(){}});
 const saved=calls.find(c=>c.kind==='cloud_configure').params;assert.equal(saved.app_password,' app password spaces ');assert.equal(field('app_password').value,'');assert.equal(field('encryption_password').value,'','saved secrets are never prefilled');
 data.restore_token='0123456789abcdef0123456789abcdef';data.restore_created_ms=1794017725000;data.restore_version='0.6.2-rc1';data.phase='restore_ready';await group.onSettingsShown();
 field('confirm_text').value='RESTORE';await button('confirm_restore').onclick();assert.equal(data.pending_restore,undefined,'native cancelled confirmation cannot restore');
 agree=true;field('confirm_text').value='RESTORE';await button('confirm_restore').onclick();assert.equal(calls.filter(c=>c.kind==='cloud_confirm_restore').length,2);assert.equal(calls.at(-2).params.confirmation,'RESTORE');
 group.hidden=true;const before=calls.length;await group.onSettingsShown();assert.equal(calls.length,before,'hidden category performs no status polling');group.hidden=false;data.phase='error';data.error='CLOUD_AUTH_FAILED';await group.onSettingsShown();assert.ok(group.textContent.includes('cloud.error.auth'),'authentication errors are localized');
 console.log('Cloud settings: defaults, secure fields, saved feedback, explicit restore confirmation and hidden polling passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
