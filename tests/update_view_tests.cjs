const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
class Node{
 constructor(tag,attrs={},children=[]){this.tag=tag;Object.assign(this,attrs);this.children=[];this.hidden=Object.hasOwn(attrs,'hidden');this.isConnected=true;this.append(...children);}
 append(...nodes){for(const n of nodes.flat().filter(n=>n!=null)){this.children.push(n);if(n instanceof Node)n.parent=this;}}
 replaceChildren(...nodes){this.children=[];this.append(...nodes);}
 closest(){for(let n=this;n;n=n.parent)if(n.hidden)return n;return null;}
 get textContent(){return this.children.map(n=>n instanceof Node?n.textContent:String(n)).join('');}
 set textContent(v){this.children=[v];}
 all(){return [this,...this.children.filter(n=>n instanceof Node).flatMap(n=>n.all())];}
}
const group=new Node('section'),calls=[],locales={};
for(const locale of ['zh-CN','en-US'])locales[locale]=JSON.parse(fs.readFileSync(`web/desktop/locales/${locale}.json`,'utf8'));
let locale='zh-CN',status={phase:'error',error:'quota exhausted',providers:[{source:'github',ok:false,error_code:'rate_limited'}]};
const ctx=vm.createContext({console,page:'settings',nativeVisible:true,PulseI18n:{locale:'zh-CN'},state:{version:'0.7.5',config:{update_channel:'preview',update_source:'github'}},
 tr:k=>locales[locale][k]||k,error:e=>{throw e;},bytes:String,clearTimeout(){},setTimeout:()=>1,queueMicrotask,
 el:(tag,attrs,...children)=>new Node(tag,attrs,children),panel:(title,...children)=>new Node('section',{},[title,...children]),help:(label,...children)=>new Node('details',{},[label,...children]),
 document:{hidden:false,addEventListener(){}},rpc:async(kind,params)=>{calls.push({kind,params});return kind==='get_update_status'?structuredClone(status):{};}});
vm.runInContext(fs.readFileSync('web/desktop/update-view.js','utf8'),ctx);ctx.group=group;vm.runInContext('renderUpdateSettings(group)',ctx);
const button=key=>group.all().find(n=>n.tag==='button'&&n.textContent===(locales[locale][key]||key));
(async()=>{
 await new Promise(r=>setImmediate(r));
 assert.ok(group.textContent.includes(locales[locale]['update.error.rate']),'rate limit has a specific hint');
 assert.equal(button('ui.a78b542de282').hidden,true,'error cannot download');
 await button('update.try_auto').onclick();
 assert.equal(calls.find(c=>c.kind==='set_config').params.update_source,'auto');
 assert.ok(calls.findIndex(c=>c.kind==='set_config')<calls.findIndex(c=>c.kind==='check_updates'),'source preference saved before retry');
 assert.equal(button('update.try_auto'),undefined,'auto source has no redundant retry action');
 for(locale of ['zh-CN','en-US']){
  group.replaceChildren();ctx.PulseI18n.locale=locale;vm.runInContext('renderUpdateSettings(group)',ctx);
  for(const [code,key] of [['rate_limited','rate'],['access_denied','denied'],['network','generic']]){
   status.providers[0].error_code=code;await group.onSettingsShown();
   assert.ok(group.textContent.includes(locales[locale]['update.error.'+key]),`${locale} ${code} localized`);
  }
 }
 status={phase:'manual_check',metadata_only:true,latest:'9.0.0',providers:[]};await group.onSettingsShown();
 assert.ok(group.textContent.includes(locales[locale]['update.manual_check']));
 assert.ok(group.textContent.includes(locales[locale]['update.feed_hint']));
 assert.equal(button('ui.a78b542de282').hidden,true,'version-only result cannot download');
 await button('ui.5c5a53a77586').onclick();assert.equal(calls.at(-1).kind,'open_release_page');
 status={phase:'up_to_date',providers:[]};await group.onSettingsShown();
 assert.ok(!group.textContent.includes(locales[locale]['update.feed_hint']),'successful retry clears fallback hint');
 group.hidden=true;const before=calls.length;await group.onSettingsShown();assert.equal(calls.length,before,'hidden panel does not poll');
 console.log('Update settings: bilingual errors, explicit source retry, safe feed fallback and hidden polling passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
