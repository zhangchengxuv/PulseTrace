'use strict';
globalThis.PulseI18n=(()=>{
 let locale=(globalThis.navigator?.language||'en-US').toLowerCase().startsWith('zh')?'zh-CN':'en-US';
 function t(key,params={}){const text=PulseLocales[locale]?.[key]??PulseLocales['en-US'][key];if(text===undefined)throw new Error('Missing translation: '+key);return text.replace(/\{([a-zA-Z0-9_]+)\}/g,(all,k)=>Object.hasOwn(params,k)?String(params[k]):all);}
 function use(value){const next=value==='zh-CN'?'zh-CN':'en-US';const changed=next!==locale;locale=next;if(typeof document!=='undefined')document.documentElement.lang=locale;return changed;}
 const sourceKeys=new Map(Object.entries(PulseLocales['zh-CN']).map(([key,value])=>[value,key]));
 // Exactly two supported locales: retain two formatters instead of constructing one per keycap/value.
 const numberFormats=new Map();
 function number(value){if(!numberFormats.has(locale))numberFormats.set(locale,new Intl.NumberFormat(locale));return numberFormats.get(locale).format(Number(value)||0);}
 return {t,use,label(value){const key=sourceKeys.get(value);return key?t(key):value;},get locale(){return locale;},number,date(value,options){return new Date(value).toLocaleString(locale,options);}};
})();
globalThis.tr=(key,params)=>PulseI18n.t(key,params);
