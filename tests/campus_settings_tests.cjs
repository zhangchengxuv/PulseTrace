const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
class Node{
 constructor(tag,attrs={},children=[]){this.tag=tag;Object.assign(this,attrs);this.children=children.flat().filter(v=>v!=null);this.classList={add(){}};this.value=attrs.value??'';}
 append(...items){this.children.push(...items);}
 replaceChildren(...items){this.children=items;}
 checkValidity(){return true;}
 get textContent(){return this.children.map(v=>typeof v==='object'?v.textContent:v).join('');}
 set textContent(v){this.children=[v];}
 all(){return [this,...this.children.filter(v=>v instanceof Node).flatMap(v=>v.all())];}
}
let calls=[],rejectConfig=false;
const fixture={origin:'https://campus.example',portal_url:'https://campus.example/usage',rule:null};
const context=vm.createContext({structuredClone,console,state:{config:{campus_enabled:true,campus_portal_url:fixture.portal_url}},PulseI18n:{locale:'en-US'},page:'settings',tr:k=>k,bytes:String,
 el:(tag,attrs,...children)=>new Node(tag,attrs,children),document:{querySelectorAll:()=>[]},
 rpc:async(type,params)=>{calls.push({type,params});if(type==='set_config'&&rejectConfig)throw Error('controlled save failure');if(type==='get_campus_status')return structuredClone(fixture);if(type==='get_status')return {config:{}};return {};}});
vm.runInContext(fs.readFileSync('web/desktop/campus-view.js','utf8'),context);
vm.runInContext(`campusData=${JSON.stringify(fixture)};campusCandidates={balance:[{value:10,raw:'¥10',selector:'#balance',kind:'currency',unit:'CNY',confidence:.95}]};`,context);
const host=new Node('div');context.host=host;vm.runInContext('renderCampusSettings(host)',context);
const label=key=>host.all().find(n=>n.tag==='label'&&n.children[0]?.textContent===key);
const button=key=>host.all().find(n=>n.tag==='button'&&n.textContent===key);
const tick=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 const quota=label('campus.field.free_quota_bytes').all().find(n=>n.tag==='input');quota.value='10';quota.onchange();await tick();await tick();
 let saved=calls.find(c=>c.type==='save_campus_mapping').params.fields;
 assert.equal(saved.free_quota_bytes.value,10);assert.equal(saved.balance,undefined,'billing edits never confirm unreviewed page candidates');
 calls=[];await button('campus.confirm_results').onclick();
 saved=calls.find(c=>c.type==='save_campus_mapping').params.fields;assert.equal(saved.balance.selector,'#balance','explicit confirmation saves prefilled page fields');
 assert.ok(calls.some(c=>c.type==='campus_rescan'),'confirmation reads the saved rule immediately');
 calls=[];rejectConfig=true;await button('campus.open').onclick();
 assert.equal(calls.some(c=>c.type==='open_campus_portal'),false,'failed configuration must abort portal opening');
 assert.ok(host.all().some(n=>n.textContent==='controlled save failure'),'save failure remains visible');
 console.log('Campus settings: explicit confirmation, billing isolation, immediate rule replay and failed-save handling passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
