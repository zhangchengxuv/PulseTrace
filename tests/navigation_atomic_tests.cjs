const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const source=fs.readFileSync('web/desktop/product.js','utf8');
assert.match(source,/pendingScrollTop=pageScrollPositions\.get\(next\)/);
assert.doesNotMatch(source,/page=next;window\.scrollTo/);
const refreshSource=source.slice(source.indexOf('async function refresh('),source.indexOf('const pageRanges='));
const node=()=>({dataset:{},classList:{remove(){},toggle(){},contains(){return false;}},replaceChildren(){},append(){}});
let rendered=false,replacedAfterRender=false,replaceCalls=0;
const stage={className:'page-stage',childNodes:[{kind:'settings'}]};
stage.classList={remove(){},toggle(){},contains(){return false;}};
const content={dataset:{},classList:{remove(){},toggle(name,value){this.network=value;}},children:[],replaceChildren(items){replaceCalls++;replacedAfterRender=rendered;this.children=items.items||[];}};
const fragment={items:[],append(...items){this.items.push(...items);}};
const nodes={content,state:node(),toggle:node(),title:node(),error:node()};
const context=vm.createContext({
 $:id=>nodes[id],loading:false,nativeVisible:true,document:{hidden:false,createElement:tag=>{assert.equal(tag,'section');return stage;},createDocumentFragment:()=>fragment,querySelectorAll:()=>[]},
 viewGeneration:0,refreshTimer:0,refreshQueued:false,page:'settings',range:{from:1,to:2,bucket:1},rangePreset:'custom',rangeDirty:false,pendingScrollTop:null,pageScrollPositions:new Map(),selectedTrafficMonth:'all',
 state:{},campusData:null,rows:[],protocolSamples:[],networkEvents:[],monthlyTraffic:null,renderKey:'',updateOpenPending:false,window:{scrollTo(){throw new Error('refresh must not reset scroll');}},
 PulseCampus:{refresh:async()=>{}},PulseI18n:{use:()=>false},PulseDatePicker:{current:null,close(){}},applyTheme(){},applyAppearance(){},tr:k=>k,error(e){throw e;},clearCharts(){},finishStartup(){},
 mountRangeControls(){},names:{settings:'Settings'},settings(){rendered=true;stage.childNodes.push({kind:'rendered'});},dashboard(){},networkPage(){},keyboardPage(){},mousePage(){},history(){},appUsage(){},
 scheduleRefresh(){},clearTimeout,queueMicrotask,requestAnimationFrame:fn=>fn(),
 rpc:async type=>{if(type==='get_status')return {version:'test',config:{},monitoring:false,effective_language:'en-US'};if(type==='get_monthly_traffic')return null;throw new Error(type);}
});
vm.runInContext(refreshSource,context);
(async()=>{await context.refresh(true);assert.equal(rendered,true);assert.equal(replaceCalls,1);assert.equal(replacedAfterRender,true,'content must be replaced only after detached rendering completes');assert.equal(content.children.length,2);console.log('Atomic page replacement check passed');})().catch(e=>{console.error(e);process.exitCode=1;});
