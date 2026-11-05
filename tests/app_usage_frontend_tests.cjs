const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const context=vm.createContext({Date,Map,tr:x=>x});
vm.runInContext(fs.readFileSync('web/desktop/app-usage.js','utf8'),context);
const group=vm.runInContext('groupUsage',context),bins=vm.runInContext('usageBins',context);
const rows=[{minute:60,key:'a',name:'A',active_ms:2000,foreground_ms:3000},{minute:61,key:'a',name:'A',active_ms:3000,foreground_ms:5000},{minute:62,key:'b',name:'B',active_ms:1000,foreground_ms:2000}];
const result=group(rows);assert.equal(result.length,2);assert.equal(result[0].active_ms,5000);assert.equal(result[0].foreground_ms,8000);assert.equal(rows[0].active_ms,2000);
assert.equal(bins(rows).reduce((n,r)=>n+r[1],0),6000);assert.equal(bins(rows,true).reduce((n,r)=>n+r[1],0),6000);
assert.equal(group([]).length,0);assert.equal(bins([]).length,0);
console.log('8 usage grouping and local-calendar aggregation checks passed');
