'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict');
const context=vm.createContext({});vm.runInContext(fs.readFileSync(__dirname+'/../web/desktop/rankings.js','utf8'),context);
const select=(rows,opts={})=>context.rankingSelection({rows},{sort:'total',search:'',limit:10,visible:100,...opts});
// Explicit fixtures only; the product contains no synthetic statistics.
const fixture=[{key:'a',name:'App A',upload:30,download:70,total:100},{key:'b',name:'App B',upload:80,download:10,total:90},{key:'unknown',name:'',upload:10,download:0,total:10}];
let n=0;function check(x){assert.ok(x);n++;}
check(select(fixture).rows[0].key==='a');check(select(fixture,{sort:'upload'}).rows[0].key==='b');check(select(fixture,{sort:'download'}).rows[0].key==='a');check(select(fixture,{search:'APP b'}).rows.length===1);check(select(fixture,{search:'APP b'}).denominator===200);check(select(fixture,{limit:1}).rows.length===1);check(select(fixture,{limit:0,visible:2}).rows.length===2);check(select(fixture,{sort:'name'}).rows[0].key==='a');check(select([]).denominator===0);check(fixture[0].key==='a');
const product=fs.readFileSync(__dirname+'/../web/desktop/product.js','utf8');check(product.includes('await refreshTrafficRankings()'));check(product.includes('rankingView.root?.contains(document.activeElement)'));const source=fs.readFileSync(__dirname+'/../web/desktop/rankings.js','utf8');check(!source.includes('innerHTML'));check(!source.includes('Math.random'));check(source.includes("confirm(rankLabel('clear_confirm'))"));check(source.includes("type:'password'"));
console.log(n+' ranking filter, privacy and refresh checks passed');
