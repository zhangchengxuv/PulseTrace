const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const context=vm.createContext({console,structuredClone,state:{config:{campus_unit_price:.5}},PulseI18n:{locale:'en-US'},tr:(key,vars={})=>key.replace(/\{(\w+)\}/g,(_,k)=>vars[k]??'')});
vm.runInContext(fs.readFileSync('web/desktop/campus-view.js','utf8'),context);
const GiB=1024**3;
vm.runInContext(`campusData={latest:{cycle_text:'2026-11',details:{web_values:{used_bytes:${10*GiB},free_quota_bytes:${99*GiB}}}},
 rule:{fields:{
  free_quota_bytes:{mode:'manual',value:10,unit:'GiB',kind:'bytes',reset_on_cycle:true,cycle:'2026-11'},
  used_free_bytes:{mode:'manual',value:4,unit:'GiB',kind:'bytes',reset_on_cycle:true,cycle:'2026-11'},
  free_remaining_bytes:{mode:'formula',formula:'free_quota_bytes - used_free_bytes',kind:'bytes'},
  paid_used_bytes:{mode:'formula',formula:'max(0, used_bytes - used_free_bytes)',kind:'bytes'},
  spendable_balance:{mode:'manual',value:2,unit:'CNY',kind:'currency'},
  used_bytes:{mode:'auto',selector:'#used',kind:'bytes',unit:'B',confidence:1}
 }}};`,context);
let r=vm.runInContext('campusResolve()',context);
assert.equal(r.freeRemaining,6*GiB);
assert.equal(r.resolved.paid_used_bytes,6*GiB);
assert.equal(r.funded,4*GiB);
assert.equal(r.total,10*GiB);
assert.equal(r.progress,.4);
assert.equal(r.estimated,3,'only paid traffic contributes to a local fee estimate');
assert.equal(r.sources.free_remaining_bytes,'formula');
assert.equal(r.resolved.free_quota_bytes,10*GiB,'manual values are not overwritten by portal refresh');
vm.runInContext(`state.config.campus_unit_price=0;`,context);
r=vm.runInContext('campusResolve()',context);assert.equal(r.funded,null);assert.equal(r.total,null);
vm.runInContext(`state.config.campus_unit_price=.5;campusData.latest.cycle_text='2026-12';`,context);
r=vm.runInContext('campusResolve()',context);assert.equal(r.resolved.used_free_bytes,null);assert.ok(r.errors.some(e=>e.message.includes('formula_missing')));
vm.runInContext(`campusData.rule.fields.billing_cycle={mode:'manual',kind:'text',value:'2026-12',unit:'text'};campusData.rule.fields.free_quota_bytes.cycle='2026-12';campusData.rule.fields.used_free_bytes.cycle='2026-12';`,context);
r=vm.runInContext('campusResolve()',context);assert.equal(r.resolved.used_free_bytes,4*GiB,'manual billing cycle controls cycle reset');
vm.runInContext(`campusData.latest.cycle_text='2026-11';campusData.rule.fields.paid_used_bytes.formula='unknown_field';`,context);
r=vm.runInContext('campusResolve()',context);assert.equal(r.resolved.paid_used_bytes,null);assert.ok(r.errors.some(e=>e.field==='paid_used_bytes'));
vm.runInContext(`campusData.rule.fields.paid_used_bytes.formula='used_free_bytes / 0';`,context);r=vm.runInContext('campusResolve()',context);assert.ok(r.errors.some(e=>e.message.includes('formula_zero')));
vm.runInContext(`campusData.rule.fields.free_remaining_bytes.formula='free_remaining_bytes + 1';`,context);r=vm.runInContext('campusResolve()',context);assert.ok(r.errors.some(e=>e.message.includes('formula_cycle')));
vm.runInContext(`campusFieldState={};campusData={latest:{details:{web_values:{quota_bytes:100,used_bytes:20,remaining_bytes:80}}},rule:{fields:{quota_bytes:{mode:'auto'},used_bytes:{mode:'auto'},remaining_bytes:{mode:'auto'}}}};`,context);
r=vm.runInContext('campusResolve()',context);assert.equal(r.freeRemaining,null,'generic remaining and plan quota do not mean free data');assert.equal(r.estimated,null,'total usage must not be billed as paid usage');
vm.runInContext(`campusData.rule.fields.free_quota_bytes={mode:'manual',value:10,unit:'GB',kind:'bytes',reset_on_cycle:false};campusData.rule.fields.used_free_bytes={mode:'manual',value:2,unit:'GiB',kind:'bytes',reset_on_cycle:true,cycle:'2026-11'};`,context);
r=vm.runInContext('campusResolve()',context);assert.equal(r.resolved.free_quota_bytes,1e10);assert.equal(r.resolved.used_free_bytes,null,'unknown billing period cannot validate cycle-specific usage');
vm.runInContext(`state.config.campus_price_unit='GB';campusData.rule.fields.spendable_balance={mode:'manual',value:2,unit:'CNY',kind:'currency'};campusData.rule.fields.paid_used_bytes={mode:'manual',value:3,unit:'GB',kind:'bytes'};`,context);
r=vm.runInContext('campusResolve()',context);assert.equal(r.funded,4e9,'GB price must not use a GiB divisor');assert.equal(r.estimated,1.5);
vm.runInContext(`campusFieldState={};campusData={rule:null};campusCandidates={balance:[{value:10,raw:'10',selector:'#balance',unit:'CNY',confidence:.9}]};campusSuggest();`,context);
assert.equal(vm.runInContext('campusFieldState.balance.selector',context),'#balance','recognized values prefill for explicit confirmation');
vm.runInContext(`campusCandidates.balance[0].selector='#changed';campusSuggest();`,context);
assert.equal(vm.runInContext('campusFieldState.balance.selector',context),'#balance','automatic rescans do not replace a pending selection');
vm.runInContext(`campusData={origin:'https://campus.example',runtime_status:'campus_auth_required'};campusEventStatus='';`,context);
assert.equal(vm.runInContext('campusQuality()',context),'campus.auth_required','background authentication failure survives reopening the main UI');
vm.runInContext(`PulseCampus.ingest({type:'campus_candidate',portal_origin:'https://other.example',field:'balance',candidate:{selector:'#unrelated',kind:'currency',unit:'CNY',confidence:1}});`,context);
assert.equal(vm.runInContext('campusFieldState.balance.selector',context),'#balance','events from a previous portal cannot replace the current selection');
console.log('Campus calculations: manual, formula, GB/GiB, missing data, cycle reset, free/paid separation and zero-price checks passed');
