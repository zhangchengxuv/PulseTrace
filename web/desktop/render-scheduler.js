'use strict';
// ResizeObserver already runs in the browser's rendering cycle. Avoid an extra frame
// for size changes; RAF is used only for initialization, resuming and DPR changes.
globalThis.PulseRender=(()=>{
 const entries=new Set();let nativeVisible=true,resolution=null;
 const visible=()=>nativeVisible&&!document.hidden;
 const invalidate=()=>{for(const entry of entries)entry.schedule();};
 function setVisible(value){nativeVisible=value;for(const entry of entries){if(!visible())entry.cancel();else entry.schedule();}}
 function observe(node,draw){
  let frame=0,disposed=false,force=false,lastWidth=-1,lastHeight=-1,lastDpi=0;
  const cancel=()=>{if(frame)cancelAnimationFrame(frame);frame=0;};
  const paint=()=>{
   if(disposed||!visible()||!node.isConnected)return;
   const width=node.clientWidth,height=node.clientHeight,dpi=devicePixelRatio||1;
   if(!force&&width===lastWidth&&height===lastHeight&&dpi===lastDpi)return;
   force=false;lastWidth=width;lastHeight=height;lastDpi=dpi;draw();
  };
  const schedule=(refresh=true)=>{force=force||refresh;if(frame||disposed||!visible())return;frame=requestAnimationFrame(()=>{frame=0;paint();});};
  const observer=new ResizeObserver(()=>{cancel();paint();}),entry={schedule,cancel,disconnect(){if(disposed)return;disposed=true;cancel();observer.disconnect();entries.delete(entry);}};
  entries.add(entry);observer.observe(node);schedule();return entry;
 }
 function dpiChanged(){armDpi();invalidate();}
 function armDpi(){if(resolution)resolution.removeEventListener('change',dpiChanged);resolution=matchMedia(`(resolution: ${devicePixelRatio||1}dppx)`);resolution.addEventListener('change',dpiChanged);}
 const visibilityChanged=()=>setVisible(nativeVisible);
 document.addEventListener('visibilitychange',visibilityChanged);armDpi();
 window.addEventListener('pagehide',()=>{for(const entry of [...entries])entry.disconnect();resolution?.removeEventListener('change',dpiChanged);document.removeEventListener('visibilitychange',visibilityChanged);},{once:true});
 return {observe,setVisible};
})();
