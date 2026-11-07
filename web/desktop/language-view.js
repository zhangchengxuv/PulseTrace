'use strict';
function syncLanguage(){
 names=translatedNames();
 for(const node of document.querySelectorAll('[data-i18n]'))node.textContent=tr(node.dataset.i18n);
 for(const node of document.querySelectorAll('[data-i18n-aria]'))node.setAttribute('aria-label',tr(node.dataset.i18nAria));
 for(const b of document.querySelectorAll('nav button')){b.title=names[b.dataset.page];b.setAttribute('aria-label',b.title);b.querySelector('.nav-text').textContent=b.title;}
 if(!PulseDatePicker.current&&rangeRoot){rangeRoot.remove();rangeRoot=null;}
 renderKey='';
}
function renderLanguageSettings(c,compact=false){
 const input=el('select',{'aria-label':tr('language.label')},el('option',{value:'system'},tr('language.system')),el('option',{value:'zh-CN'},'简体中文'),el('option',{value:'en-US'},'English'));input.value=state.config?.language||'system';
 input.onchange=async()=>{try{await rpc('set_config',{language:input.value});await refresh(true);}catch(e){error(e);}};
 c.append(compact?el('label',{},el('strong',{},tr('language.label')),input):panel(tr('language.label'),input));
}
