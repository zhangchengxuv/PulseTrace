// Developer microbenchmark only; no monitoring records are read or generated.
const fs=require('fs'),vm=require('vm'),{performance}=require('perf_hooks');
const context=vm.createContext({Intl,Date,Map,globalThis:null});context.globalThis=context;
vm.runInContext(fs.readFileSync('web/desktop/locales.js','utf8'),context);
vm.runInContext(fs.readFileSync('web/desktop/i18n.js','utf8'),context);
const api=context.PulseI18n,samples=[];let checksum=0;
for(let round=-1;round<7;round++){
 const start=performance.now();for(const locale of ['zh-CN','en-US']){api.use(locale);for(let i=0;i<10000;i++)checksum+=api.number(i*123.45).length;}
 if(round>=0)samples.push(performance.now()-start);
}
const sorted=samples.slice().sort((a,b)=>a-b),result={engine:process.version,operation:'20000 bilingual number formats per round',rounds:samples.length,median_ms:sorted[3],samples_ms:samples,checksum};
if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify(result,null,2));console.log(JSON.stringify(result));
