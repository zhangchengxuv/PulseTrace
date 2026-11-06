const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const source=fs.readFileSync('web/desktop/product.js','utf8');
const node=()=>({dataset:{},hidden:true,classList:{remove(){}},replaceChildren(...items){this.items=items;this.updates=(this.updates||0)+1;},append(item){this.items.push(item);}});
const banner=node(),context=vm.createContext({$:()=>banner,finishStartup(){},tr:k=>k,document:{createTextNode:text=>({text})},help:()=>({}),el:()=>({}),rpc(){},refresh(){}});
vm.runInContext(source.slice(source.indexOf('function error('),source.indexOf('function notice(')),context);
for(const [code,key]of [['DATABASE_SAVE_FAILED','error.storage'],['DATABASE_QUERY_FAILED','error.query'],['ARCHIVE_FAILED','error.archive'],['RECOVERY_FAILED','error.recovery'],['INPUT_FAILED','error.input']]){
 context.error({code,message:'controlled failure'});assert.equal(banner.items[0].text,key,'failure category stays explicit');
 const before=banner.updates;context.error({code,message:'controlled failure'});assert.equal(banner.updates,before,'unchanged failures do not recreate the banner or close details');
}
const refreshSource=source.slice(source.indexOf('async function refresh('),source.indexOf('const pageRanges='));
const nodes={state:node(),toggle:node(),title:node(),content:node(),error:node()};let failures=[],rendered=0;
const c=vm.createContext({$:id=>nodes[id],loading:false,document:{hidden:false,querySelectorAll:()=>[]},nativeVisible:true,refreshTimer:0,viewGeneration:0,refreshQueued:false,page:'mouse',range:{from:1,to:2,bucket:1},rangePreset:'custom',state:{},rows:[{value:71}],protocolSamples:[],networkEvents:[],monthlyTraffic:null,renderKey:'',updateOpenPending:false,window:{},PulseI18n:{use:()=>false},applyTheme(){},applyAppearance(){},tr:k=>k,error:e=>failures.push(e),finishStartup(){},clearCharts(){},mountRangeControls(){},names:{mouse:'Mouse',settings:'Settings'},mousePage(){rendered++;},dashboard(){},networkPage(){},keyboardPage(){},history(){},settings(){rendered++;},appUsage(){},scheduleRefresh(){},clearTimeout,queueMicrotask,rpc:async type=>{if(type==='get_status')return {version:'test',config:{},monitoring:false};throw Object.assign(Error('controlled query failure'),{code:'DATABASE_QUERY_FAILED'});}});
vm.runInContext(refreshSource,c);
(async()=>{
 await c.refresh(true);assert.equal(rendered,0,'failed primary query never renders synthetic zeros');assert.equal(c.rows[0].value,71,'last successful rows remain unchanged');assert.equal(failures[0].code,'DATABASE_QUERY_FAILED');
 c.page='settings';c.rangePreset='all';await c.refresh(true);assert.equal(rendered,1,'settings remain available when archival queries fail');assert.equal(failures.length,2);
 const rpc=c.rpc;c.rpc=async type=>type==='get_status'?{version:'test',config:{},monitoring:true,worker_stopped:true}:rpc(type);
 await c.refresh(true);assert.equal(nodes.state.textContent,'status.stopped','a stopped worker cannot advertise running collection');assert.equal(nodes.toggle.disabled,true,'stopped worker cannot accept a misleading start action');
 console.log('17 storage failure/category/stale-history/control-availability checks passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
