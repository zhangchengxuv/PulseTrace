const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
// A details toggle event is asynchronous: redraw must preserve the live property
// even if that event has not reached the saved disclosure state yet.
{
 let live=null;
 const rankingContext=vm.createContext({document:{querySelector:()=>live},tr:key=>key,fmt:String,keyLabel:id=>'Key '+id,
  el:(tag,attrs,...children)=>({tag,children,append(node){this.children.push(node);},addEventListener(){},querySelector(){return this.children[0];}})});
 vm.runInContext(fs.readFileSync('web/desktop/disclosure.js','utf8'),rankingContext);
 rankingContext.pagedTable=(key,headers,values)=>({values});
 vm.runInContext(fs.readFileSync('web/desktop/keyboard-view.js','utf8'),rankingContext);
 const render=()=>rankingContext.keyboardRanking(Object.fromEntries(Array.from({length:75},(_,i)=>[i,i+1])),['74',75]);
 assert.equal(render().open,false,'ranking starts collapsed');
 live={open:true};assert.equal(render().open,true,'pending expansion survives redraw');
 live={open:false};assert.equal(render().open,false,'pending collapse survives redraw');
 assert.equal(render().children[1].values.length,75,'ranking preserves all keys');
 assert.equal(render().children[0].children.at(-1).children[0],'Key 74 · 75');
}
const source=fs.readFileSync('web/desktop/product.js','utf8');
assert.match(source,/pendingScrollTop=pageScrollPositions\.get\(next\)/);
assert.doesNotMatch(source,/page=next;window\.scrollTo/);
const refreshSource=source.slice(source.indexOf('function pageViewSignature('),source.indexOf('const pageRanges='));
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
 state:{},campusData:null,rows:[],protocolSamples:[],networkEvents:[],comparisonRows:[],comparisonRange:'',comparisonFetched:0,monthlyTraffic:null,renderKey:'',updateOpenPending:false,window:{scrollTo(){throw new Error('refresh must not reset scroll');}},
 PulseCampus:{refresh:async()=>{}},PulseI18n:{use:()=>false},PulseDatePicker:{current:null,close(){}},applyTheme(){},applyAppearance(){},tr:k=>k,error(e){throw e;},clearCharts(){},finishStartup(){},
 mountRangeControls(){},names:{settings:'Settings'},settings(){rendered=true;stage.childNodes.push({kind:'rendered'});},dashboard(){},networkPage(){},keyboardPage(){},mousePage(){},history(){},appUsage(){},
 scheduleRefresh(){},clearTimeout,queueMicrotask,requestAnimationFrame:fn=>fn(),
 rpc:async type=>{if(type==='get_status')return {version:'test',config:{},monitoring:false,effective_language:'en-US'};if(type==='get_monthly_traffic')return null;if(type==='get_statistics')return {rows:[]};throw new Error(type);}
});
vm.runInContext(refreshSource,context);
(async()=>{await context.refresh(true);assert.equal(rendered,true);assert.equal(replaceCalls,1);assert.equal(replacedAfterRender,true,'content must be replaced only after detached rendering completes');assert.equal(content.children.length,2);
 context.pendingScrollTop=42;context.renderKey='';let scrolled=false;
 context.window.scrollTo=options=>{assert.equal(options.top,42);scrolled=true;};context.requestAnimationFrame=()=>{throw Error('scroll restoration was delayed until another frame');};
 await context.refresh(true);assert.equal(scrolled,true,'scroll restoration finishes in the same render task');
 await context.refresh(true);assert.equal(replaceCalls,2,'unchanged data must not reconstruct a page even for an explicit refresh');
 context.page='dashboard';context.names.dashboard='Dashboard';context.renderKey='';await context.refresh(true);const beforeCampusUpdate=replaceCalls;
 context.campusData={latest:{balance:12,observed_ms:1234}};await context.refresh(true);
 assert.equal(replaceCalls,beforeCampusUpdate,'campus-only refresh must not reconstruct the Dashboard');
 console.log('Atomic page replacement, same-frame scroll and unchanged-data reuse checks passed');})().catch(e=>{console.error(e);process.exitCode=1;});
