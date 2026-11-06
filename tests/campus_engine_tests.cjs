const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
class Element{
 constructor(tag,text='',attrs={},children=[]){this.nodeType=1;this.tagName=tag.toUpperCase();this.id=attrs.id||'';this.className=attrs.class||'';this.aria=attrs.aria||'';this.children=children;this.parentElement=null;this.listeners={};for(const child of children)child.parentElement=this;this.ownText=text;}
 get textContent(){return [this.ownText,...this.children.map(c=>c.textContent)].filter(Boolean).join(' ').replace(/\s+/g,' ').trim();}
 getAttribute(name){return name==='aria-label'?this.aria:null;}
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
assert.equal(E.parseBytes('1.5 GiB').value,1.5*1024**3);
assert.equal(E.parseBytes('2048 KB').value,2048000);
assert.equal(E.parseMoney('余额 ¥ 12.30 元').value,12.3);
assert.equal(E.parseCycle('2026-11-01 至 2026-11-30').unit,'text');
function cell(tag,text,id){return new Element(tag,text,{id});}
function row(label,value,id){return new Element('tr','',{},[cell('td',label,id+'-label'),cell('td',value,id)]);}
const tableDoc=document([row('账户余额','¥88.60','balance'),row('已用流量','3.25 GiB','used'),row('剩余流量','26.75 GiB','remaining'),row('套餐额度','30 GiB','quota'),row('计费周期','2026-11','cycle')]);
context.document=tableDoc;
const table=E.scan(tableDoc,null);
assert.equal(table.candidates.balance[0].value,88.6);
assert.equal(table.candidates.used_bytes[0].value,3.25*1024**3);
const ruleResult=E.scan(tableDoc,{fields:{balance:{selector:'#balance',kind:'currency',unit:'CNY'},used_bytes:{selector:'#used',kind:'bytes',unit:'GiB'},remaining_bytes:{selector:'#remaining',kind:'bytes',unit:'GiB'},quota_bytes:{selector:'#quota',kind:'bytes',unit:'GiB'},billing_cycle:{selector:'#cycle',kind:'text',unit:'text'}}});
assert.ok(ruleResult.rule_valid,JSON.stringify({missing:ruleResult.missing,values:ruleResult.values}));
const cards=document([new Element('section','',{},[new Element('strong','Balance',{id:'b1'}),new Element('b','$42.10')]),new Element('section','',{},[new Element('strong','Data remaining',{id:'r1'}),new Element('b','512 MB')])]);
context.document=cards;const cardResult=E.scan(cards,null);assert.equal(cardResult.candidates.balance[0].value,42.1);assert.equal(cardResult.candidates.remaining_bytes[0].value,512*1e6);
const dl=document([new Element('dl','',{},[new Element('dt','Usage'),new Element('dd','1.2 GB')]),new Element('dl','',{},[new Element('dt','Quota'),new Element('dd','10 GB')])]);
context.document=dl;const dlResult=E.scan(dl,null);assert.ok(dlResult.candidates.used_bytes.some(x=>x.value===1.2e9));
const changed=E.scan(tableDoc,{fields:{balance:{selector:'#missing',kind:'currency',unit:'CNY'}}});assert.equal(changed.rule_valid,false);assert.equal(changed.snapshot,null);assert.deepEqual([...changed.missing],['balance']);
const ambiguous=document([new Element('section','Balance ¥12.00',{id:'a'}),new Element('section','Balance ¥13.00',{id:'b'})]);context.document=ambiguous;const ambiguousResult=E.scan(ambiguous,null);assert.equal(ambiguousResult.candidates.balance.ambiguous,true);
const login=document([new Element('form','Account Password Login',{id:'login'})]);context.document=login;assert.equal(Object.keys(E.scan(login,null).candidates).length,0);
let picked=null;context.document=tableDoc;E.installPicker(tableDoc,'balance',event=>picked=event);const target=tableDoc.querySelector('#balance');tableDoc.listeners.click({preventDefault(){},stopPropagation(){},target});assert.equal(picked.field,'balance');assert.ok(picked.candidate&&picked.candidate.manual);
console.log('Campus engine: units, three DOM structures, ambiguity, login-empty, manual selection, confirmation and stale-rule checks passed');
