const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
let checks=0;const check=(value,message)=>{checks++;assert.ok(value,message);};
class Events{constructor(){this.listeners=new Map();}addEventListener(k,fn){if(!this.listeners.has(k))this.listeners.set(k,new Set());this.listeners.get(k).add(fn);}removeEventListener(k,fn){this.listeners.get(k)?.delete(fn);}fire(k){for(const fn of [...this.listeners.get(k)||[]])fn();}}
const window=new Events(),document=new Events(),media=[],observers=[],frames=new Map();let sequence=0;
document.hidden=false;
const context=vm.createContext({window,document,Set,devicePixelRatio:1,requestAnimationFrame:fn=>{frames.set(++sequence,fn);return sequence;},cancelAnimationFrame:id=>frames.delete(id),matchMedia:query=>{const m=new Events();m.query=query;media.push(m);return m;},ResizeObserver:class{constructor(fn){this.fn=fn;this.disconnected=false;observers.push(this);}observe(){}disconnect(){this.disconnected=true;}}});
vm.runInContext(fs.readFileSync('web/desktop/render-scheduler.js','utf8'),context);const api=context.PulseRender;
const flush=()=>{const queued=[...frames.values()];frames.clear();queued.forEach(fn=>fn());};
const node={isConnected:true,clientWidth:400,clientHeight:180};let paints=0;const subscription=api.observe(node,()=>paints++);
for(let i=0;i<10;i++){observers[0].fn();window.fire('resize');}
check(frames.size===0&&paints===1,'initial + unchanged ResizeObserver deliveries paint once');flush();check(paints===1,'initial RAF canceled after observer paints');
subscription.disconnect();check(frames.size===0&&observers[0].disconnected,'disconnect cancels pending RAF');flush();check(paints===1,'disposed view never paints');
const live=api.observe(node,()=>paints++);api.setVisible(false);check(frames.size===0,'native hide cancels frames');window.fire('resize');check(frames.size===0,'hidden resize does not paint');api.setVisible(true);flush();check(paints===2,'native reopen paints once');
document.hidden=true;document.fire('visibilitychange');observers[1].fn();check(frames.size===0,'document hide suppresses paint');document.hidden=false;document.fire('visibilitychange');flush();check(paints===3,'document visible resumes');
api.setVisible(false);document.fire('visibilitychange');check(frames.size===0,'document visibility cannot override native hide');api.setVisible(true);flush();
context.devicePixelRatio=2;media.at(-1).fire('change');check(media.at(-1).query==='(resolution: 2dppx)','DPI query rearmed');check(media[0].listeners.get('change').size===0,'old DPI listener removed');flush();check(paints===5,'DPR-only change repaints');
node.isConnected=false;node.clientWidth=450;observers[1].fn();flush();check(paints===5,'detached canvas not drawn');node.isConnected=true;observers[1].fn();flush();check(paints===6,'reattached resized canvas redraws');
node.clientWidth=500;observers[1].fn();check(paints===7,'size change paints without adding a frame');
api.setVisible(false);api.setVisible(true);window.fire('pagehide');check(frames.size===0&&observers[1].disconnected,'page destruction cleans observers/frames');check(media.at(-1).listeners.get('change').size===0,'global DPI listener cleaned');live.disconnect();
let constructed=0;const IntlShim={NumberFormat:class extends Intl.NumberFormat{constructor(...args){super(...args);constructed++;}}};
const i18n=vm.createContext({Intl:IntlShim});vm.runInContext(fs.readFileSync('web/desktop/locales.js','utf8'),i18n);vm.runInContext(fs.readFileSync('web/desktop/i18n.js','utf8'),i18n);
for(const locale of ['en-US','zh-CN','en-US']){i18n.PulseI18n.use(locale);for(const value of [0,1,12345.67,-100,NaN,undefined,Infinity])check(i18n.PulseI18n.number(value)===new Intl.NumberFormat(locale).format(Number(value)||0),'formatter output unchanged');}
check(constructed===2,'bounded to two formatters across repeated locale switches');
console.log(checks+' rendering lifecycle/DPI/formatting checks passed');
