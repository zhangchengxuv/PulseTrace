const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
class Element{
 constructor(tag,text='',attrs={},children=[]){this.nodeType=1;this.tagName=tag.toUpperCase();this.id=attrs.id||'';this.className=attrs.class||'';this.aria=attrs.aria||'';this.children=children;this.parentElement=null;this.listeners={};for(const child of children)child.parentElement=this;this.ownText=text;}
 get textContent(){return [this.ownText,...this.children.map(c=>c.textContent)].filter(Boolean).join(' ').replace(/\s+/g,' ').trim();}
 getAttribute(name){return name==='aria-label'?this.aria:null;}
 getBoundingClientRect(){return this.rect||{left:0,right:0,top:0,bottom:0,width:0,height:0};}
 closest(selector){const tags=new Set(selector.split(',').map(s=>s.trim().split('.')[0].toUpperCase()));for(let n=this;n;n=n.parentElement)if(tags.has(n.tagName)||[...tags].some(t=>this.className.split(/\s+/).includes(t)))return n;return null;}
 matches(selector){return selector===this.tagName.toLowerCase()||selector==='.'+this.className.split(' ')[0];}
 descendants(){return this.children.flatMap(c=>[c,...c.descendants()]);}
 querySelectorAll(selector){if(selector==='th,td')return this.descendants().filter(e=>e.tagName==='TH'||e.tagName==='TD');return this.descendants().filter(e=>e.matches(selector));}
 addEventListener(type,fn){this.listeners[type]=fn;}
 removeEventListener(type,fn){if(this.listeners[type]===fn)delete this.listeners[type];}
}
function document(rows){const body=new Element('body','',{},rows),listeners={};return {body,listeners,querySelectorAll:s=>s==='body *'?body.descendants():s==='tr'?rows.filter(r=>r.tagName==='TR'):[],querySelector:s=>rows.map(r=>({r,hit:r.querySelectorAll('th,td').find(e=>'#'+e.id===s)||(s===`tr:nth-of-type(1)`?r:null)})).find(x=>x.hit)?.hit||null,addEventListener(type,fn){listeners[type]=fn;},removeEventListener(type,fn){if(listeners[type]===fn)delete listeners[type];}};}
class CSS{static escape(v){return v.replace(/[^\w-]/g,'\\$&');}}
const context=vm.createContext({globalThis:null,CSS,document:null,location:{origin:'https://campus.example',pathname:'/usage'}});context.globalThis=context;
vm.runInContext(fs.readFileSync('web/desktop/campus-engine.js','utf8'),context);
const E=context.PulseCampusEngine;
assert.equal(E.parseBytes('1,024 MiB').value,1024**3);
assert.equal(E.parseBytes('1 gib').value,1024**3);
assert.equal(E.parseBytes('-2 GB'),null);
assert.equal(E.parseMoney('¥ 1,234.50').value,1234.5);
assert.equal(E.parseMoney('-12.00'),null);
assert.equal(E.parseMoney('12 GB'),null);
assert.equal(E.parseBytes('1.5 GiB').value,1.5*1024**3);
assert.equal(E.parseBytes('2048 KB').value,2048000);
assert.equal(E.parseMoney('余额 ¥ 12.30 元').value,12.3);
assert.equal(E.parseCycle('2026-11-01 至 2026-11-30').unit,'text');
function cell(tag,text,id){return new Element(tag,text,{id});}
function row(label,value,id){return new Element('tr','',{},[cell('td',label,id+'-label'),cell('td',value,id)]);}
const tableDoc=document([row('账户余额','¥88.60','balance'),row('免费流量总额度','20 GiB','free-quota'),row('免费剩余流量','15 GiB','free-remaining'),row('已用免费流量','5 GiB','used-free'),row('可用于流量消费的余额','¥10.00','spendable'),row('付费流量已使用量','1.5 GiB','paid-used'),row('已用流量','3.25 GiB','used'),row('剩余流量','26.75 GiB','remaining'),row('套餐额度','30 GiB','quota'),row('计费周期','2026-11','cycle')]);
context.document=tableDoc;
const table=E.scan(tableDoc,null);
assert.equal(table.candidates.balance[0].value,88.6);
assert.equal(table.candidates.used_bytes[0].value,3.25*1024**3);
assert.equal(table.candidates.free_quota_bytes[0].value,20*1024**3);
assert.equal(table.candidates.used_free_bytes[0].value,5*1024**3);
assert.equal(table.candidates.spendable_balance[0].value,10);
assert.equal(table.candidates.paid_used_bytes[0].value,1.5*1024**3);
assert.ok(!table.candidates.balance.some(x=>x.value===10),'spendable balance is not misclassified as account balance');
assert.ok(!table.candidates.used_bytes.some(x=>x.value===5*1024**3||x.value===1.5*1024**3),'free and paid usage are not misclassified as total usage');
const ruleResult=E.scan(tableDoc,{fields:{balance:{mode:'auto',selector:'#balance',kind:'currency',unit:'CNY'},used_bytes:{mode:'auto',selector:'#used',kind:'bytes',unit:'GiB'},remaining_bytes:{mode:'selector',selector:'#remaining',kind:'bytes',unit:'GiB'},quota_bytes:{mode:'auto',selector:'#quota',kind:'bytes',unit:'GiB'},billing_cycle:{mode:'auto',selector:'#cycle',kind:'text',unit:'text'},free_remaining_bytes:{mode:'manual',value:12,unit:'GiB',kind:'bytes'},paid_used_bytes:{mode:'formula',formula:'used_bytes - free_remaining_bytes',kind:'bytes'}}});
assert.ok(ruleResult.rule_valid,JSON.stringify({missing:ruleResult.missing,values:ruleResult.values}));
assert.equal(ruleResult.snapshot.free_remaining_bytes,undefined,'manual fields are not overwritten by page scan');
assert.equal(ruleResult.snapshot.paid_used_bytes,undefined,'formula fields are not overwritten by page scan');
const cards=document([new Element('section','',{},[new Element('strong','Balance',{id:'b1'}),new Element('b','$42.10')]),new Element('section','',{},[new Element('strong','Data remaining',{id:'r1'}),new Element('b','512 MB')])]);
context.document=cards;const cardResult=E.scan(cards,null);assert.equal(cardResult.candidates.balance[0].value,42.1);assert.equal(cardResult.candidates.remaining_bytes[0].value,512*1e6);
const dl=document([new Element('dl','',{},[new Element('dt','Usage'),new Element('dd','1.2 GB')]),new Element('dl','',{},[new Element('dt','Quota'),new Element('dd','10 GB')])]);
context.document=dl;const dlResult=E.scan(dl,null);assert.ok(dlResult.candidates.used_bytes.some(x=>x.value===1.2e9));
const changed=E.scan(tableDoc,{fields:{balance:{selector:'#missing',kind:'currency',unit:'CNY'}}});assert.equal(changed.rule_valid,false);assert.equal(changed.snapshot,null);assert.deepEqual([...changed.missing],['balance']);
const nineFields=Object.fromEntries(Object.entries(table.candidates).slice(0,9).map(([field,items])=>[field,{...items[0],mode:'auto'}]));
assert.equal(E.scan(tableDoc,{fields:nineFields}).snapshot.mapped_fields.length,9,'extended mappings replay together');
const hiddenBalance=tableDoc.querySelector('#balance');hiddenBalance.hidden=true;
assert.equal(E.scan(tableDoc,{fields:{balance:{selector:'#balance',kind:'currency'}}}).snapshot,null,'hidden historical values cannot synchronize');hiddenBalance.hidden=false;
const loginDoc={...tableDoc,querySelectorAll:s=>s==='input[type="password"]'?[{hidden:false}]:tableDoc.querySelectorAll(s)};
assert.equal(E.scan(loginDoc,{fields:nineFields}).snapshot,null,'expired authentication preserves the previous snapshot');
const ambiguous=document([new Element('section','Balance ¥12.00',{id:'a'}),new Element('section','Balance ¥13.00',{id:'b'})]);context.document=ambiguous;const ambiguousResult=E.scan(ambiguous,null);assert.equal(ambiguousResult.candidates.balance.ambiguous,true);
const login=document([new Element('form','Account Password Login',{id:'login'})]);context.document=login;assert.equal(Object.keys(E.scan(login,null).candidates).length,0);
function positioned(tag,text,id,x,y,width=100){const e=new Element(tag,text,{id});e.rect={left:x,right:x+width,top:y,bottom:y+16,width,height:16};return e;}
const spatialLabel=positioned('p','账户余额：','spatial-label',100,50),spatialValue=positioned('p','38.25','spatial-value',210,50);
const spatialDoc=document([new Element('div','',{},[spatialValue]),new Element('div','',{},[spatialLabel]),
 new Element('div','',{},[positioned('p','93.00','other-row',210,75)]),
 new Element('div','',{},[positioned('p','Used data','spatial-used-label',100,100)]),
 new Element('div','',{},[positioned('p','3 GiB','spatial-used',210,100)])]);
context.document=spatialDoc;const spatial=E.scan(spatialDoc,null);
assert.equal(spatial.candidates.balance[0].value,38.25,'separate columns use visual alignment rather than DOM order');
assert.equal(spatial.candidates.balance[0].selector,'#spatial-value','persist the value element');
assert.ok(spatial.candidates.balance[0].confidence<.8,'spatial suggestions require user confirmation');
assert.equal(spatial.candidates.used_bytes[0].value,3*1024**3);
spatialValue.hidden=true;assert.equal(E.scan(spatialDoc,null).candidates.balance,undefined,'hidden and adjacent-row values are not paired');
spatialValue.hidden=false;spatialValue.rect.left=600;assert.equal(E.scan(spatialDoc,null).candidates.balance,undefined,'distant columns are not paired');
const vague=document([new Element('section','流量(V4)： 2 GB',{id:'vague'})]);
assert.equal(E.scan(vague,null).candidates.used_bytes,undefined,'an unspecified traffic counter is not assumed to be used data');
let picked=null;context.document=tableDoc;E.installPicker(tableDoc,'balance',event=>picked=event);const target=tableDoc.querySelector('#balance');tableDoc.listeners.click({preventDefault(){},stopPropagation(){},target});assert.equal(picked.field,'balance');assert.ok(picked.candidate&&picked.candidate.manual);
E.installPicker(tableDoc,'balance',()=>{});E.cancelPicker(tableDoc);assert.equal(tableDoc.listeners.click,undefined,'closing the portal cancels click interception');
assert.equal(E.installPicker(loginDoc,'balance',()=>{}).picker,'login_required','picker never intercepts login form clicks');
assert.equal(loginDoc.listeners.click,undefined);
console.log('Campus engine: units, three DOM structures, ambiguity, login-empty, manual selection, confirmation and stale-rule checks passed');
