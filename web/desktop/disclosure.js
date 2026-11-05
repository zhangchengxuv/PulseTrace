'use strict';
const disclosureState=new Map();
function disclosure(key,title,count,...children){
 const node=el('details',{class:'data-disclosure','data-disclosure':key},el('summary',{},el('span',{},title),count==null?null:el('small',{},fmt(count))),...children);node.open=disclosureState.get(key)?.open||false;
 node.addEventListener('toggle',()=>{if(node.isConnected)disclosureState.set(key,{...disclosureState.get(key),open:node.open});});return node;
}
function pagedTable(key,headers,values,format=x=>x){
 const host=el('div',{class:'paged-data'}),size=25;
 const draw=()=>{const v=disclosureState.get(key)||{},offset=Math.max(0,Math.min(v.offset||0,Math.max(0,Math.ceil(values.length/size)-1)*size));v.offset=offset;disclosureState.set(key,v);host.replaceChildren(table(headers,values.slice(offset,offset+size).map(format)),el('div',{class:'actions'},el('button',{class:'secondary',disabled:offset===0,onclick:()=>{v.offset-=size;draw();}},tr('ui.c9b9ae7a6144')),el('small',{},`${values.length?offset+1:0}–${Math.min(offset+size,values.length)} / ${values.length}`),el('button',{class:'secondary',disabled:offset+size>=values.length,onclick:()=>{v.offset+=size;draw();}},tr('ui.8a8542f69648'))));};draw();return host;
}

function validateDisclosureControls(){
 if(!state.self_test)return;const fixture=Array.from({length:57},(_,i)=>[String(i)]),host=pagedTable('selftest.pages',['fixture'],fixture);$('content').append(host);
 if(host.querySelectorAll('tbody tr').length!==25)throw new Error('Pagination exceeded DOM row bound');host.querySelectorAll('.actions button')[1].click();if(host.querySelector('tbody td').textContent!=='25')throw new Error('Pagination next failed');
 host.remove();const a=disclosure('selftest.fold','fixture',57,host);$('content').append(a);a.open=true;a.dispatchEvent(new Event('toggle'));const b=disclosure('selftest.fold','fixture',57);if(!b.open)throw new Error('Fold state lost on refresh');a.remove();disclosureState.delete('selftest.fold');disclosureState.delete('selftest.pages');
}
