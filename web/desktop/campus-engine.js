(function(root){
  'use strict';
  const patterns={
    balance:/余额|可用金额|账户余额|balance|available\s+(?:funds|credit)/i,
    free_quota_bytes:/免费流量|免费额度|free\s+(?:data|traffic|quota|allowance)/i,
    free_remaining_bytes:/免费剩余|免费流量剩余|free\s+remaining|remaining\s+free/i,
    used_free_bytes:/免费已用|已用免费流量|免费流量已使用|free\s+(?:used|usage)/i,
    spendable_balance:/可消费余额|流量余额|可用于流量(?:消费)?的余额|spendable\s+balance|balance\s+for\s+(?:data|usage)/i,
    paid_used_bytes:/付费流量|超额流量|超出免费|paid\s+(?:data|usage)|chargeable\s+(?:data|usage)/i,
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
    const text=normalize(value),match=text.match(/(?<![\d.,，-])((?:\d{1,3}(?:[,，]\d{3})+|\d+)(?:\.\d+)?)\s*(B|KB|MB|GB|TB|KiB|MiB|GiB|TiB)\b/i);
    if(!match||!byteUnit.test(match[2]))return null;
    const displayUnit=Object.keys({B:1,KB:1,MB:1,GB:1,TB:1,KiB:1,MiB:1,GiB:1,TiB:1}).find(u=>u.toLowerCase()===match[2].toLowerCase());
    const factors={B:1,KB:1e3,MB:1e6,GB:1e9,TB:1e12,KiB:1024,MiB:1024**2,GiB:1024**3,TiB:1024**4};
    return {value:numeric(match[1])*factors[displayUnit],unit:'B',display_unit:displayUnit};
  }
  function parseMoney(value){
    const text=normalize(value);
    if(/[-−]\s*\d|\d\s*(?:[KMGT]i?B|%)\b/i.test(text))return null;
    const match=text.match(/(?<![\d.,，])((?:\d{1,3}(?:[,，]\d{3})+|\d+)(?:\.\d+)?)(?![\d.,，])/);
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
  function visible(element){return !element.hidden&&element.getAttribute?.('aria-hidden')!=='true'&&(!element.getClientRects||element.getClientRects().length>0);}
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
  function kindFor(field){return field==='balance'||field==='spendable_balance'||field==='fee'?'currency':field==='billing_cycle'?'text':'bytes';}
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
    const specific=(field,text)=>{
      if(field==='balance'&&patterns.spendable_balance.test(text))return false;
      if(field==='used_bytes'&&(patterns.used_free_bytes.test(text)||patterns.paid_used_bytes.test(text)))return false;
      if(field==='remaining_bytes'&&patterns.free_remaining_bytes.test(text))return false;
      if(field==='quota_bytes'&&patterns.free_quota_bytes.test(text))return false;
      if(field==='free_quota_bytes'&&(patterns.free_remaining_bytes.test(text)||patterns.used_free_bytes.test(text)))return false;
      return true;
    };
    const nodes=[...document.querySelectorAll('body *')].slice(0,2500);
    for(const element of nodes){
      if(!element||!visible(element)||/^(SCRIPT|STYLE|NOSCRIPT|TEMPLATE|INPUT|TEXTAREA|OPTION)$/.test(element.tagName))continue;
      const text=normalize(element.textContent);if(!text||text.length>220)continue;
      const row=element.closest?.('tr,dl,li,section,article,.card,.item,.metric')||element;
      const rowText=normalize(row.textContent).slice(0,220);
      for(const [field,pattern]of Object.entries(patterns)){
        if(!specific(field,rowText))continue;
        if(!pattern.test(rowText)&&!pattern.test(normalize((element.id||'')+' '+(element.className||'')+' '+(element.getAttribute?.('aria-label')||''))))continue;
        const target=text.length<=180?element:row;
        const c=candidate(field,normalize(target.textContent).slice(0,220),target,stableSelector(target),.56);
        if(c)candidates.push(c);
      }
    }
    for(const row of [...document.querySelectorAll('tr')].slice(0,500)){
      if(!visible(row))continue;
      const elements=[...row.querySelectorAll('th,td')],cells=elements.map(cell=>normalize(cell.textContent));if(cells.length<2)continue;
      for(const [field,pattern]of Object.entries(patterns))if(pattern.test(cells[0])||pattern.test(cells[1])){
        if(!specific(field,cells.slice(0,2).join(' ')))continue;
        const c=candidate(field,cells[1],elements[1],stableSelector(elements[1]),.82);if(c)candidates.push(c);
      }
    }
    // Legacy portals often position label and value in separate columns. Match
    // visible leaf values on the same visual line; keep these as suggestions
    // requiring confirmation, and replay the value selector rather than layout.
    const leaves=nodes.filter(element=>visible(element)&&!element.children?.length&&
      !/^(SCRIPT|STYLE|NOSCRIPT|TEMPLATE|INPUT|TEXTAREA|OPTION)$/.test(element.tagName))
      .map(element=>({element,text:normalize(element.textContent),rect:element.getBoundingClientRect?.()}))
      .filter(item=>item.text&&item.text.length<=80&&item.rect?.width>0&&item.rect.height>0);
    const labels=leaves.filter(item=>Object.values(patterns).some(pattern=>pattern.test(item.text))).slice(0,120);
    const values=leaves.filter(item=>!Object.values(patterns).some(pattern=>pattern.test(item.text))&&
      /\d/.test(item.text)).slice(0,300);
    for(const label of labels)for(const [field,pattern]of Object.entries(patterns)){
      if(!pattern.test(label.text)||!specific(field,label.text))continue;
      const kind=kindFor(field);
      // A label already containing its value is handled by the DOM paths above.
      if(kind==='currency'&&/\d/.test(label.text)||kind==='bytes'&&parseBytes(label.text)||kind==='text'&&/\d/.test(label.text))continue;
      const nearby=values.filter(item=>{
        const gap=item.rect.left-label.rect.right;
        const dy=Math.abs((item.rect.top+item.rect.bottom-label.rect.top-label.rect.bottom)/2);
        return gap>=-4&&gap<=320&&dy<=Math.max(3,Math.min(item.rect.height,label.rect.height)*.45)&&
          (kind!=='currency'||/^(?:[¥￥]\s*)?\d[\d,，]*(?:\.\d+)?\s*(?:元|CNY|RMB)?$/i.test(item.text))&&
          parseKind(kind,item.text);
      }).sort((a,b)=>a.rect.left-b.rect.left);
      if(nearby.length){const item=nearby[0],c=candidate(field,item.text,item.element,stableSelector(item.element),.56);if(c)candidates.push(c);}
    }
  }
  function scan(document,rule){
    const login=[...document.querySelectorAll('input[type="password"]')].some(visible);
    if(login)return {candidates:{},rule_valid:false,missing:Object.keys(rule?.fields||{}),values:{},snapshot:null,portal_url:location.origin+location.pathname,auth_required:true};
    const raw=[];addCandidates(document,raw);
    const fields={};
    for(const [field,items]of Object.entries(raw.reduce((groups,item)=>{(groups[item.field]??=[]).push(item);return groups;},{}))){
      items.sort((a,b)=>b.confidence-a.confidence);
      const unique=[];for(const item of items)if(!unique.some(old=>old.selector===item.selector&&old.value===item.value))unique.push(item);
      fields[field]=unique.slice(0,5);
    }
    for(const [field,items]of Object.entries(fields))fields[field].ambiguous=items.some(item=>item.value!==items[0].value&&items[0].confidence-item.confidence<.12);
    const mappings=Object.fromEntries(Object.entries(rule?.fields||{}).filter(([,mapping])=>
      mapping&&(mapping.mode==='auto'||mapping.mode==='selector'||(!mapping.mode&&mapping.selector))));
    const values={},missing=[],mapped=[];
    for(const [field,mapping]of Object.entries(mappings)){
      let element=null;try{element=document.querySelector(mapping.selector);}catch{missing.push(field);continue;}
      if(!element||!visible(element)){missing.push(field);continue;}
      const parsed=parseKind(mapping.kind,normalize(element.textContent));if(!parsed){missing.push(field);continue;}
      values[field]=parsed.value;mapped.push(field);
    }
    const enough=!missing.length&&mapped.length>=1;
    return {candidates:fields,rule_valid:enough,missing,values,
      snapshot:enough?{...values,cycle_text:String(values.billing_cycle||''),mapped_fields:mapped,quality:'confirmed',confidence:Math.min(1,mapped.length/6)}:null,
      portal_url:location.origin+location.pathname,observed_at:Date.now()};
  }
  function installPicker(document,field,send){
    cancelPicker(document);
    if([...document.querySelectorAll('input[type="password"]')].some(visible))return {picker:'login_required',field};
    const handler=event=>{
      event.preventDefault();event.stopPropagation();document.removeEventListener('click',handler,true);root.__pulseCampusPicker=null;
      const element=event.target.closest?.('td,th,dd,dt,li,div,span,strong,b,p')||event.target;
      const text=normalize(element.textContent).slice(0,220),selector=stableSelector(element),parsed=parsedField(field,text);
      send({type:'campus_candidate',field,candidate:parsed?{...parsed,selector,raw:text,confidence:.95,manual:true}:{field,kind:kindFor(field),selector,raw:text,confidence:.25,manual:true}});
    };
    root.__pulseCampusPicker=handler;document.addEventListener('click',handler,true);
    return {picker:'armed',field};
  }
  function cancelPicker(document){
    const previous=root.__pulseCampusPicker;if(previous)document.removeEventListener('click',previous,true);
    root.__pulseCampusPicker=null;return {picker:'cancelled'};
  }
  root.PulseCampusEngine={parseBytes,parseMoney,parseCycle,normalize,scan,installPicker,cancelPicker,stableSelector};
})(globalThis);
