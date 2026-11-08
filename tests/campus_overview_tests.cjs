const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
class Node{
 constructor(tag,attrs={},children=[]){this.tag=tag;Object.assign(this,attrs);this.dataset={};this.style={};this.children=children.flat().filter(v=>v!=null);this.replacements=0;}
 append(...items){this.children.push(...items);}
 replaceChildren(...items){this.children=items;this.replacements++;}
 get textContent(){return this.children.map(v=>typeof v==='object'?v.textContent:v).join('');}
 all(){return [this,...this.children.filter(v=>v instanceof Node).flatMap(v=>v.all())];}
}
let hosts=[];const G=1024**3;
class TestDate extends Date{constructor(...a){super(...(a.length?a:['2026-11-08T10:00:00']));}static now(){return new TestDate().getTime();}}
const ctx=vm.createContext({console,Date:TestDate,state:{config:{campus_enabled:true,campus_unit_price:.5,campus_linear_tariff:true,campus_price_basis:'school',campus_traffic_radix:1024}},PulseI18n:{locale:'en-US'},tr:(k,p={})=>k+Object.values(p).join(),metricIcon:()=>new Node('svg'),navigate(){},page:'dashboard',
 el:(tag,attrs,...children)=>new Node(tag,attrs,children),document:{querySelectorAll:s=>s==='.campus-overview'?hosts:[]}});
vm.runInContext(fs.readFileSync('web/desktop/campus-view.js','utf8'),ctx);
const run=s=>vm.runInContext(s,ctx),model=()=>run('campusOverviewModel()');
run('campusData={rule:null}');assert.equal(model().status,'empty');
const fixture={origin:'https://campus.example',latest:{observed_ms:TestDate.now(),cycle_text:'2026-11',measurement:{raw_readings:{used_bytes:{value:30399.45,unit:'MB'}}},details:{web_values:{balance:27.45}}},rule:{fields:{
 balance:{mode:'auto'},used_bytes:{mode:'selector'},free_quota_bytes:{mode:'manual',value:120,unit:'GB'},billing_cycle:{mode:'manual',kind:'text',value:'2026-11'},
 spendable_balance:{mode:'formula',formula:'balance'},used_free_bytes:{mode:'formula',formula:'min(used_bytes, free_quota_bytes)',accounting_confirmed:true,cycle:'2026-11',confirmed_month:'2026-11'}
}}};
const reset=()=>run('campusFieldState={};campusLastEventType="";campusData='+JSON.stringify(fixture));reset();
let m=model();assert.equal(m.status,'ready');
assert.equal((m.values.used/G).toFixed(2),'29.69');assert.equal((m.values.free/G).toFixed(2),'90.31');assert.equal((m.percent*100).toFixed(2),'24.74');
assert.equal(m.values.funded,54.9*G);assert.ok(Math.abs(m.values.total/G-(120-30399.45/1024+54.9))<1e-10);
assert.deepEqual(Array.from(m.segments,s=>s.key),['used','free']);assert.equal(m.sum,120*G,'denominator is free quota, not remaining capacity');
const dashboard=run('campusDashboardCard()'),network=run('campusNetworkPanel()');assert.equal(dashboard.textContent,network.textContent);assert.ok(dashboard.textContent.includes('29.69 GB'));assert.ok(dashboard.textContent.includes('30399.45 MB'));
hosts=[dashboard];const replacements=dashboard.replacements;run('PulseCampus.renderLive()');assert.equal(dashboard.replacements,replacements);
run('state.config.campus_traffic_radix=1000');m=model();assert.equal(m.values.used/1e9,30.39945);assert.ok(Math.abs(m.values.free/1e9-89.60055)<1e-10);run('PulseCampus.renderLive()');assert.ok(dashboard.textContent.includes('30.40 GB'));assert.equal(run('campusData.latest.measurement.raw_readings.used_bytes.value'),30399.45);
run('state.config.campus_traffic_radix=1024');
run('state.config.campus_linear_tariff=false');assert.equal(model().values.funded,null);assert.equal(model().values.total,null);run('state.config.campus_linear_tariff=true');
run('state.config.campus_unit_price=0');assert.equal(model().values.funded,null);run('state.config.campus_unit_price=.5');
run('delete campusData.latest.details.web_values.balance');assert.equal(model().values.funded,null);reset();
run('campusData.latest.details.web_values.balance=0');assert.equal(model().values.balance,0);assert.equal(model().values.funded,0);assert.equal(model().values.total,model().values.free);reset();
run('delete campusData.rule.fields.used_free_bytes.accounting_confirmed');assert.equal(model().values.free,null);assert.equal(model().percent,null);reset();
run('campusData.latest.cycle_text="2026-10"');assert.equal(model().values.total,null);assert.equal(model().percent,null);reset();
run('campusData.latest.observed_ms=new Date("2026-10-01").getTime()');assert.equal(model().values.total,null);assert.equal(model().values.free,null);reset();
run('campusData.rule.fields.free_quota_bytes.value=10');m=model();assert.equal(m.values.free,0);assert.equal(m.percent,1);assert.equal(m.sum,10*G);reset();
run('campusData.rule.fields.spendable_balance.formula="balance * 2"');assert.equal(model().values.funded,null);reset();
assert.equal(run('campusResolve().funded'),54.9*G);
run('campusData.rule.fields.spendable_balance.formula="balance * 2"');assert.equal(run('campusResolve().funded'),null,'settings and overview reject the same unverified balance formula');reset();
run('campusData.latest.measurement.raw_readings.free_remaining_bytes={value:10,unit:"GB"};campusData.rule.fields.free_remaining_bytes={mode:"auto"}');assert.equal(model().percent,null);assert.equal(model().segments.length,0,'inconsistent free counters cannot display a percentage');reset();
run('campusData.latest.measurement.raw_readings.used_bytes.value=1024;campusData.latest.observed_ms=Date.now()');assert.equal(model().values.used,G,'new raw readings replace derived values without reusing a prior conversion');reset();
run('campusLastEventType="campus_error"');m=model();assert.equal(m.status,'error');assert.equal(m.percent,null);assert.equal(m.values.balance,27.45);assert.ok(run('campusNetworkPanel()').textContent.includes('campus.card.retained'));reset();
run('campusLastEventType="campus_auth_required"');assert.equal(model().status,'auth');reset();
run('state.config.campus_enabled=false');assert.equal(run('campusNetworkPanel()').dataset.status,'disabled');run('state.config.campus_enabled=true');
run('campusData.latest.measurement={raw_readings:{},status:"needs_confirmation"}');assert.equal(model().values.used,null);assert.equal(model().segments.length,0);reset();
run('delete campusData.latest.measurement;campusData.latest.details.web_values.used_bytes=30399.45e6;campusData.latest.details.web_units={used_bytes:"MB"}');assert.equal((model().values.used/G).toFixed(2),'29.69','recover decimal-normalized legacy observation once');
run('state.config.campus_price_basis="legacy_bytes";state.config.campus_price_unit="GiB";state.config.campus_traffic_radix=1000');assert.equal(model().values.funded,54.9*G,'legacy yuan/GiB keeps original economic byte basis');
run('state.config.campus_price_unit="GB"');assert.equal(model().values.funded,54.9e9);
console.log('Campus overview: school radix, raw history, billing scope/cycles, zero/missing/complex rates, cache, shared cards and quota denominator passed');
