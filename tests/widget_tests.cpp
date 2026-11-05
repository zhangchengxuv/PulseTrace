#include "pulsetrace/desktop/widget_ipc.hpp"
#include "pulsetrace/desktop/taskbar_layout.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace pulsetrace;
int main(){int checks=0;auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Widget protocol/layout regression");};try{
 for(UINT dpi:{96u,120u,144u}){RECT bar{0,1000,2400,1000+MulDiv(48,dpi,96)},notify{2100,1000,2400,bar.bottom},out{};check(taskbar_slot(bar,notify,{{800,1000,1600,bar.bottom}},dpi,out));check(inside_rect(out,bar));check(out.right<notify.left);check(!taskbar_slot(bar,notify,{out},dpi,out));}
 RECT out{};check(!taskbar_slot({0,0,60,1000},{0,900,60,1000},{},96,out));check(!taskbar_slot({0,0,1000,48},{900,0,1100,48},{},96,out));
 WidgetShared data;WidgetSnapshot snapshot;snapshot.upload=25;snapshot.download=50;publish_snapshot(data.data_sequence,data.data,snapshot);WidgetSnapshot copy;check(read_snapshot(data.data_sequence,data.data,copy)&&copy.upload==25&&copy.download==50);check(valid_snapshot(copy));copy.version=99;check(!valid_snapshot(copy));copy=snapshot;copy.upload=std::numeric_limits<double>::infinity();check(!valid_snapshot(copy));copy=snapshot;copy.download=-1;check(!valid_snapshot(copy));
 InterlockedIncrement(&data.data_sequence);check(!read_snapshot(data.data_sequence,data.data,copy));InterlockedIncrement(&data.data_sequence);
 std::cout<<checks<<" widget geometry/IPC checks passed\n";return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
