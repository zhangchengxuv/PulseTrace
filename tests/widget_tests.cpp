#include "pulsetrace/desktop/widget_ipc.hpp"
#include "pulsetrace/desktop/taskbar_layout.hpp"
#include "pulsetrace/desktop/widget_surface.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace pulsetrace;
int main(){int checks=0;auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Widget protocol/layout regression");};try{
 for(UINT dpi:{96u,120u,144u}){RECT bar{0,1000,2400,1000+MulDiv(48,dpi,96)},notify{2100,1000,2400,bar.bottom},out{};check(taskbar_slot(bar,notify,{{800,1000,1600,bar.bottom}},dpi,out));check(inside_rect(out,bar));check(out.right<notify.left);check(!taskbar_slot(bar,notify,{out},dpi,out));}
 RECT out{};check(!taskbar_slot({0,0,60,1000},{0,900,60,1000},{},96,out));check(!taskbar_slot({0,0,1000,48},{900,0,1100,48},{},96,out));
 WidgetShared data;WidgetSnapshot snapshot;snapshot.upload=25;snapshot.download=50;publish_snapshot(data.data_sequence,data.data,snapshot);WidgetSnapshot copy;check(read_snapshot(data.data_sequence,data.data,copy)&&copy.upload==25&&copy.download==50);check(valid_snapshot(copy));copy.version=99;check(!valid_snapshot(copy));copy=snapshot;copy.upload=std::numeric_limits<double>::infinity();check(!valid_snapshot(copy));copy=snapshot;copy.download=-1;check(!valid_snapshot(copy));
 InterlockedIncrement(&data.data_sequence);check(!read_snapshot(data.data_sequence,data.data,copy));InterlockedIncrement(&data.data_sequence);
 copy=snapshot;copy.background_alpha=97;check(!valid_snapshot(copy));copy.background_alpha=32;check(valid_snapshot(copy));
 for(unsigned alpha:{0u,1u,32u,127u,254u,255u})for(unsigned coverage:{0u,1u,64u,128u,254u,255u}){
  const auto p=widget_over(RGB(139,196,234),coverage,RGB(10,38,57),alpha),a=p>>24;
  check((p&255)<=a&&((p>>8)&255)<=a&&((p>>16)&255)<=a);
  if(alpha==0&&coverage==0)check(p==0);
  if(coverage==255)check(a==255);
 }
 const auto gdi_before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 {WidgetSurface surface;for(UINT dpi:{96u,120u,144u,192u})for(bool dark:{false,true})for(unsigned background:{0u,32u}){
   check(surface.prepare(MulDiv(138,dpi,96),MulDiv(36,dpi,96),dpi));
   surface.draw(L"\u2193 342.5 KiB/s",L"\u2191 91.7 KiB/s",dark,background);
   check(surface.alpha(0,0)==background);check(surface.opaque>0&&surface.partial>0);
   check(background==0?surface.transparent>surface.pixels().size()/2:surface.transparent==0);
   bool valid=true,gray=true;for(auto p:surface.pixels()){const auto a=p>>24;valid&=(p&255)<=a&&((p>>8)&255)<=a&&((p>>16)&255)<=a&&(a!=0||p==0);}
   for(auto p:surface.mask())gray&=(p&255)==((p>>8)&255)&&(p&255)==((p>>16)&255);
   check(valid&&gray);
  }
  surface.draw(L"Paused",L"PulseTrace",true);const auto before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
  for(int i=0;i<500;++i){surface.prepare(276,72,192);surface.draw(i%2?L"Paused":L"Rate unavailable",L"PulseTrace",true);}
  check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=before+1);
  check(!surface.prepare(0,0,96));
 }
 check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi_before+2);
 std::cout<<checks<<" widget geometry/IPC/alpha/resource checks passed\n";return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
