'use strict';
globalThis.PulseDate=(()=>{
 const pad=n=>String(n).padStart(2,'0');
 const days=(y,m)=>new Date(Date.UTC(y,m,0)).getUTCDate();
 function parts(minute){const d=new Date(minute*60000);return {year:d.getFullYear(),month:d.getMonth()+1,day:d.getDate(),hour:d.getHours(),minute:d.getMinutes()};}
 function parse(p){for(const k of ['year','month','day','hour','minute'])if(!Number.isInteger(p[k]))throw new Error(tr("ui.0660a6c6371d"));
  if(p.year<1970||p.year>9999||p.month<1||p.month>12||p.day<1||p.day>days(p.year,p.month)||p.hour<0||p.hour>23||p.minute<0||p.minute>59)throw new Error(tr("ui.6e1c4df5b075"));
  const d=new Date(p.year,p.month-1,p.day,p.hour,p.minute),r=parts(d.getTime()/60000);
  if(Object.keys(r).some(k=>r[k]!==p[k]))throw new Error(tr("ui.783de662709a"));
  return d.getTime()/60000;
 }
 const label=minute=>PulseI18n.date(minute*60000,{year:"numeric",month:"2-digit",day:"2-digit",hour:"2-digit",minute:"2-digit",hourCycle:"h23"});
 function shift(p,delta){const d=new Date(Date.UTC(p.year,p.month-1+delta,1));return {...p,year:d.getUTCFullYear(),month:d.getUTCMonth()+1,day:Math.min(p.day,days(d.getUTCFullYear(),d.getUTCMonth()+1))};}
 return {days,parts,parse,label,shift};
})();
globalThis.PulseDatePicker=(()=>{
 let current=null;
 const node=(tag,text,attrs={})=>{const e=document.createElement(tag);if(text)e.textContent=text;for(const [k,v]of Object.entries(attrs))e.setAttribute(k,v);return e;};
 class Picker{
  constructor(button,getValue,commit){this.button=button;this.getValue=getValue;this.commit=commit;button.addEventListener('click',()=>this.open());}
  open(){if(current)current.close();current=this;this.original=this.getValue();this.draft=PulseDate.parts(this.original);this.view={...this.draft};
   const d=this.dialog=node('dialog','',{class:'datetime-popup','aria-label':this.button.getAttribute('aria-label')||tr("ui.6ac8b7718121")});
   const head=node('div','',{class:'dt-head'}),prev=node('button','‹',{type:'button','aria-label':tr("ui.f9e727223456")}),next=node('button','›',{type:'button','aria-label':tr("ui.3e0b634699d4")});
   this.month=node('strong');this.year=node('input','',{type:'number',min:'1970',max:'9999','aria-label':tr("ui.bff0b9e4a393"),class:'dt-year'});this.year.value=this.view.year;
   const move=delta=>{const p=PulseDate.shift(this.view,delta);if(p.year<1970||p.year>9999)return;this.view=p;this.year.value=p.year;this.draw();};prev.onclick=()=>move(-1);next.onclick=()=>move(1);
   this.year.onchange=()=>{const y=Number(this.year.value);if(Number.isInteger(y)&&y>=1970&&y<=9999){this.view.year=y;this.draw();}};
   head.append(prev,this.month,this.year,next);const week=node('div','',{class:'dt-week'});for(const s of [tr("ui.51a75f4634df"),tr("ui.084b42f6e95e"),tr("ui.a4c3313deb18"),tr("ui.754a9d5828d5"),tr("ui.c9b87f516a38"),tr("ui.de07b5383874"),tr("ui.85217f7aff77")])week.append(node('span',s));this.grid=node('div','',{class:'dt-grid',role:'grid','aria-label':tr("ui.70d0c1b33626")});
   const time=node('div','',{class:'dt-time'});this.hour=node('input','',{type:'text',inputmode:'numeric',maxlength:'2','aria-label':tr("ui.3b6fefc50feb"),pattern:'[0-9]{1,2}'});this.minute=node('input','',{type:'text',inputmode:'numeric',maxlength:'2','aria-label':tr("ui.bd957bc497aa"),pattern:'[0-9]{1,2}'});
   this.hour.value=String(this.draft.hour).padStart(2,'0');this.minute.value=String(this.draft.minute).padStart(2,'0');time.append(node('span',tr("ui.9248356749d6")),this.hour,node('b',':'),this.minute);
   this.error=node('div','',{class:'dt-error',role:'alert'});const actions=node('div','',{class:'dt-actions'}),cancel=node('button',tr("ui.2cd0f3be8738"),{type:'button',class:'secondary'}),ok=node('button',tr("ui.36f33adaf094"),{type:'button'});cancel.onclick=()=>this.close();ok.onclick=()=>this.confirm();actions.append(cancel,ok);
   d.append(head,week,this.grid,time,this.error,actions);document.body.append(d);this.draw();
   d.addEventListener('keydown',e=>{if(e.key==='Escape'){e.preventDefault();this.close();}else if(e.key==='Enter'&&e.target.matches('input')){e.preventDefault();this.confirm();}});
   d.addEventListener('cancel',e=>{e.preventDefault();this.close();});d.addEventListener('pointerdown',e=>{const r=d.getBoundingClientRect();if(e.target===d&&(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom))this.close();});
   this.reposition=()=>this.position();window.addEventListener('resize',this.reposition);window.addEventListener('scroll',this.reposition,{passive:true});d.showModal();this.position();this.grid.querySelector('[tabindex="0"]')?.focus({preventScroll:true});
  }
  draw(){const {year:y,month:m}=this.view;this.month.textContent=new Intl.DateTimeFormat(PulseI18n.locale,{month:"long"}).format(new Date(y,m-1,1));this.grid.replaceChildren();const start=(new Date(Date.UTC(y,m-1,1)).getUTCDay()+6)%7,today=PulseDate.parts(Date.now()/60000);let selected=null;
   for(let i=0;i<start;i++)this.grid.append(node('span'));
   for(let day=1;day<=PulseDate.days(y,m);day++){const b=node('button',String(day),{type:'button',role:'gridcell','data-day':day,tabindex:'-1'});const active=this.draft.year===y&&this.draft.month===m&&this.draft.day===day;
    if(active){b.classList.add('selected');b.setAttribute('aria-selected','true');selected=b;}if(today.year===y&&today.month===m&&today.day===day)b.classList.add('today');
    b.onclick=()=>{this.draft={...this.draft,year:y,month:m,day};this.draw();this.grid.querySelector('.selected')?.focus({preventScroll:true});};
    b.onkeydown=e=>{const deltas={ArrowLeft:-1,ArrowRight:1,ArrowUp:-7,ArrowDown:7,Home:1-day,End:PulseDate.days(y,m)-day};if(!(e.key in deltas))return;e.preventDefault();const p=new Date(Date.UTC(y,m-1,day+deltas[e.key]));if(p.getUTCFullYear()<1970||p.getUTCFullYear()>9999)return;this.draft={...this.draft,year:p.getUTCFullYear(),month:p.getUTCMonth()+1,day:p.getUTCDate()};this.view={...this.draft};this.year.value=this.view.year;this.draw();this.grid.querySelector('.selected')?.focus({preventScroll:true});};this.grid.append(b);
   }(selected||this.grid.querySelector('button')).tabIndex=0;
  }
  position(){if(!this.dialog?.open)return;const r=this.button.getBoundingClientRect(),d=this.dialog,w=Math.min(318,innerWidth-20);d.style.width=w+'px';const h=d.offsetHeight;d.style.left=Math.max(10,Math.min(r.left,innerWidth-w-10))+'px';d.style.top=Math.max(10,Math.min(r.bottom+8,innerHeight-h-10,r.bottom+8+h>innerHeight?r.top-h-8:r.bottom+8))+'px';}
  confirm(){try{if(!/^\d{1,2}$/.test(this.hour.value)||!/^\d{1,2}$/.test(this.minute.value))throw new Error(tr("ui.9c295dcaf5d4"));const value=PulseDate.parse({...this.draft,hour:Number(this.hour.value),minute:Number(this.minute.value)});this.commit(value);this.close();}catch(e){this.error.textContent=e.message;}}
  close(){if(!this.dialog)return;window.removeEventListener('resize',this.reposition);window.removeEventListener('scroll',this.reposition);this.dialog.close();this.dialog.remove();this.dialog=null;if(current===this)current=null;this.button.focus({preventScroll:true});}
 }
 return {Picker,get current(){return current;},close(){current?.close();}};
})();
