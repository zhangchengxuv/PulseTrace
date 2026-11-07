const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
class Node{
 constructor(tag,attrs={},children=[]){this.tag=tag;Object.assign(this,attrs);this.dataset={};this.style={};this.children=children.flat().filter(v=>v!=null);this.replacements=0;}
 append(...items){this.children.push(...items);}
 replaceChildren(...items){this.children=items;this.replacements++;}
 get textContent(){return this.children.map(v=>typeof v==='object'?v.textContent:v).join('');}
 all(){return [this,...this.children.filter(v=>v instanceof Node).flatMap(v=>v.all())];}
}
let hosts=[];const G=1024**3;
const ctx=vm.createContext({console,state:{config:{campus_enabled:true,campus_unit_price:.5}},PulseI18n:{locale:'en-US'},tr:(k,p={})=>k+Object.values(p).join(),bytes:n=>(n/G).toFixed(1)+' GiB',metricIcon:()=>new Node('svg'),navigate(){},page:'dashboard',
 el:(tag,attrs,...children)=>new Node(tag,attrs,children),document:{querySelectorAll:s=>s==='.campus-overview'?hosts:[]}});
vm.runInContext(fs.readFileSync('web/desktop/campus-view.js','utf8'),ctx);
const run=s=>vm.runInContext(s,ctx),model=()=>run('campusOverviewModel()');
run('campusData={rule:null};');assert.equal(model().status,'empty');assert.equal(model().values.total,null);
const fixture={origin:'https://campus.example',portal_url:'https://campus.example/usage',latest:{observed_ms:1794017725000,details:{web_values:{balance:10,used_bytes:20*G,free_quota_bytes:100*G,used_free_bytes:20*G,free_remaining_bytes:80*G}}},rule:{fields:{}}};
for(const field of ['balance','used_bytes','free_quota_bytes','used_free_bytes','free_remaining_bytes'])fixture.rule.fields[field]={mode:'auto',kind:field==='balance'?'currency':'bytes',selector:'#'+field};
fixture.rule.fields.spendable_balance={mode:'formula',kind:'currency',formula:'balance'};
run('campusData='+JSON.stringify(fixture));let m=model();assert.equal(m.status,'ready');assert.equal(m.values.funded,20*G);assert.equal(m.values.total,100*G);assert.equal(m.percent,.2);
assert.deepEqual(Array.from(m.segments,s=>s.key),['used','free','funded'],'total is a sum, not a fourth segment');
hosts=[run('campusDashboardCard()')];assert.ok(hosts[0].textContent.includes('campus.card.funded'));assert.ok(hosts[0].textContent.includes('campus.card.estimate'));
const replaced=hosts[0].replacements;run('PulseCampus.renderLive()');assert.equal(hosts[0].replacements,replaced,'unchanged data leaves card DOM intact');
run("campusFieldState.balance={mode:'manual',kind:'currency',value:999,unit:'CNY'};");assert.equal(model().values.balance,10,'unconfirmed settings preview never changes overview');
run("campusLastEventType='campus_error';");assert.equal(model().percent,null);assert.equal(model().values.total,100*G,'failed sync retains last valid data');run('PulseCampus.renderLive()');assert.equal(hosts[0].dataset.status,'error');assert.ok(hosts[0].textContent.includes('campus.card.retained'));
run("campusLastEventType='campus_auth_required';");assert.equal(model().status,'auth');
run("campusLastEventType='';state.config.campus_unit_price=0;");assert.equal(model().values.funded,null);assert.equal(model().values.total,null);assert.equal(model().status,'partial');assert.ok(!model().segments.some(s=>s.key==='funded'));
run("state.config.campus_unit_price=Infinity;");assert.equal(model().values.funded,null);
run("state.config.campus_unit_price=.5;state.config.campus_price_unit='GB';");assert.equal(model().values.funded,20e9,'price unit is preserved');
run("state.config.campus_price_unit='bad';");assert.equal(model().values.funded,null);
run("delete state.config.campus_price_unit;campusData.rule.fields.spendable_balance.formula='balance * 2';");assert.equal(model().values.funded,null,'arbitrary formulas are not presented as reliable paid capacity');
run("campusData.rule.fields.spendable_balance.formula='balance';delete campusData.rule.fields.free_remaining_bytes;delete campusData.rule.fields.free_quota_bytes;");assert.equal(model().values.free,null);assert.equal(model().values.total,null,'generic usage cannot supply a free allowance');
run("state.config.campus_enabled=false;");const disabled=run('campusNetworkPanel()');assert.equal(disabled.dataset.status,'disabled');assert.ok(disabled.textContent.includes('campus.card.configure'));assert.ok(!disabled.all().some(n=>n.class==='campus-metrics'));
run('state.config.campus_enabled=true;campusData='+JSON.stringify(fixture));run("campusData.latest.details.web_values.balance=0;campusData.latest.details.web_values.used_bytes=0;campusData.latest.details.web_values.free_remaining_bytes=0;campusData.latest.details.web_values.used_free_bytes=0;");m=model();assert.equal(m.values.total,0);assert.equal(m.segments.length,0);assert.equal(m.percent,0);
console.log('Campus overview: states, trusted inputs, estimates, partial/zero values, units and in-place updates passed');
