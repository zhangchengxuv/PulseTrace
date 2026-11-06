(function(root){
  'use strict';
  const patterns={
    balance:/余额|可用金额|账户余额|balance|available\s+(?:funds|credit)/i,
    used_bytes:/已用流量|已使用流量|已用数据|used\s+(?:data|traffic|usage)|data\s+used|(?:^|\s)usage(?:\s|$)/i,
    remaining_bytes:/剩余流量|可用流量|剩余数据|remaining\s+(?:data|traffic)|data\s+remaining/i,
    quota_bytes:/套餐总量|套餐额度|总流量|流量额度|quota|allowance|data\s+plan/i,
    fee:/费用|消费金额|扣费|charge|cost|fee/i,
    billing_cycle:/计费周期|账单周期|账期|billing\s+(?:cycle|period)|cycle/i
  };
  const byteUnit=/^(?:B|KB|MB|GB|TB|KiB|MiB|GiB|TiB)$/i;
  const moneyUnit=/^(?:元|¥|￥|RMB|CNY)$/i;
  const normalize=value=>String(value||'').replace(/\s+/g,' ').trim();
  function numeric(value){
    const n=Number(String(value||'').replace(/[,，]/g,''));
    return Number.isFinite(n)?n:null;
  }
  function parseBytes(value){
    const text=normalize(value),match=text.match(/([0-9]+(?:[.,][0-9]+)?)\s*(B|KB|MB|GB|TB|KiB|MiB|GiB|TiB)\b/i);
    if(!match||!byteUnit.test(match[2]))return null;
    const displayUnit=match[2].replace(/^k/i,'K').replace(/^m/i,'M').replace(/^g/i,'G').replace(/^t/i,'T');
    const factors={B:1,KB:1e3,MB:1e6,GB:1e9,TB:1e12,KiB:1024,MiB:1024**2,GiB:1024**3,TiB:1024**4};
    return {value:numeric(match[1])*factors[displayUnit],unit:'B',display_unit:displayUnit};
  }
  function parseMoney(value){
    const text=normalize(value),match=text.match(/(?:¥|￥)?\s*([0-9]+(?:[.,][0-9]+)?)\s*(元|RMB|CNY|¥|￥)?/i);
    if(!match)return null;return {value:numeric(match[1]),unit:'CNY'};
  }
  function parseCycle(value){
    const text=normalize(value);
    return /\d{4}[-/.年]\d{1,2}|(?:月|monthly|cycle|周期|账期)/i.test(text)?{value:text,unit:'text'}:null;
  }
  function parseKind(kind,value){
    if(kind==='bytes')return parseBytes(value);
    if(kind==='currency')return parseMoney(value);
    if(kind==='text')return parseCycle(value);
    return null;
  }
  function stableSelector(element){
    if(!element||element.nodeType!==1)return '';
    if(element.id&&!/[^\w-]/.test(element.id))return '#'+CSS.escape(element.id);
    const parts=[];
    for(let node=element;node&&node.nodeType===1&&parts.length<6;node=node.parentElement){
      let part=node.tagName.toLowerCase();
      if(node.id&&!/[^\w-]/.test(node.id)){parts.unshift('#'+CSS.escape(node.id));break;}
      const siblings=node.parentElement?[...node.parentElement.children].filter(n=>n.tagName===node.tagName):[];
      if(siblings.length>1)part+=':nth-of-type('+(siblings.indexOf(node)+1)+')';
      parts.unshift(part);
    }
    return parts.join(' > ');
  }
  function kindFor(field){return field==='balance'||field==='fee'?'currency':field==='billing_cycle'?'text':'bytes';}
  function parsedField(field,text){
    const kind=kindFor(field),parsed=parseKind(kind,text);
    if(!parsed)return null;
    return {field,kind,value:parsed.value,unit:parsed.unit};
  }
  function candidate(field,text,element,selector,base){
    const parsed=parsedField(field,text);if(!parsed)return null;
    const hint=normalize((element?.id||'')+' '+(element?.className||'')+' '+(element?.getAttribute?.('aria-label')||''));
    const table=/^(TR|TD|TH|DD|DT)$/.test(element?.tagName||'')||/table|row|item|metric/i.test(hint);
    return {...parsed,selector:selector||stableSelector(element),raw:text.slice(0,180),confidence:Math.min(.99,base+(table?.12:0)+(patterns[field].test(hint)?.08:0))};
  }
  function addCandidates(document,candidates){
    const nodes=[...document.querySelectorAll('body *')].slice(0,2500);
    for(const element of nodes){
      if(!element||/^(SCRIPT|STYLE|NOSCRIPT|TEMPLATE|INPUT|TEXTAREA|OPTION)$/.test(element.tagName))continue;
      const text=normalize(element.textContent);if(!text||text.length>220)continue;
      const row=element.closest?.('tr,dl,li,section,article,.card,.item,.metric')||element;
      const rowText=normalize(row.textContent).slice(0,220);
      for(const [field,pattern]of Object.entries(patterns)){
        if(!pattern.test(rowText)&&!pattern.test(normalize((element.id||'')+' '+(element.className||'')+' '+(element.getAttribute?.('aria-label')||''))))continue;
        const target=text.length<=180?element:row;
        const c=candidate(field,normalize(target.textContent).slice(0,220),target,stableSelector(target),.56);
        if(c)candidates.push(c);
      }
    }
    for(const row of [...document.querySelectorAll('tr')].slice(0,500)){
      const cells=[...row.querySelectorAll('th,td')].map(normalize);if(cells.length<2)continue;
      for(const [field,pattern]of Object.entries(patterns))if(pattern.test(cells[0])||pattern.test(cells[1])){
        const c=candidate(field,cells.slice(0,2).join(' '),row,stableSelector(row),.7);if(c)candidates.push(c);
      }
    }
  }
  function scan(document,rule){
    const raw=[];addCandidates(document,raw);
    const fields={};
    for(const [field,items]of Object.entries(raw.reduce((groups,item)=>{(groups[item.field]??=[]).push(item);return groups;},{}))){
      items.sort((a,b)=>b.confidence-a.confidence);
      const unique=[];for(const item of items)if(!unique.some(old=>old.selector===item.selector&&old.value===item.value))unique.push(item);
      fields[field]=unique.slice(0,5);
    }
    for(const [field,items]of Object.entries(fields))fields[field].ambiguous=items.length>1&&items[0].confidence-items[1].confidence<.12;
    const mappings=rule?.fields||{},values={},missing=[],mapped=[];
    for(const [field,mapping]of Object.entries(mappings)){
      let element=null;try{element=document.querySelector(mapping.selector);}catch{continue;}
      if(!element){missing.push(field);continue;}
      const parsed=parseKind(mapping.kind,normalize(element.textContent));if(!parsed){missing.push(field);continue;}
      values[field]=parsed.value;mapped.push(field);
    }
    const enough=!missing.length&&mapped.length>=1;
    return {candidates:fields,rule_valid:enough,missing,values,
      snapshot:enough?{...values,cycle_text:String(values.billing_cycle||''),mapped_fields:mapped,quality:'confirmed',confidence:Math.min(1,mapped.length/6)}:null,
      portal_url:location.origin+location.pathname,observed_at:Date.now()};
  }
  function installPicker(document,field,send){
    const previous=root.__pulseCampusPicker;if(previous){document.removeEventListener('click',previous,true);}
    const handler=event=>{
      event.preventDefault();event.stopPropagation();document.removeEventListener('click',handler,true);root.__pulseCampusPicker=null;
      const element=event.target.closest?.('td,th,dd,dt,li,div,span,strong,b,p')||event.target;
      const text=normalize(element.textContent).slice(0,220),selector=stableSelector(element),parsed=parsedField(field,text);
      send({type:'campus_candidate',field,candidate:parsed?{...parsed,selector,raw:text,confidence:.95,manual:true}:{field,kind:kindFor(field),selector,raw:text,confidence:.25,manual:true}});
    };
    root.__pulseCampusPicker=handler;document.addEventListener('click',handler,true);
    return {picker:'armed',field};
  }
  root.PulseCampusEngine={parseBytes,parseMoney,parseCycle,normalize,scan,installPicker,stableSelector};
})(globalThis);
