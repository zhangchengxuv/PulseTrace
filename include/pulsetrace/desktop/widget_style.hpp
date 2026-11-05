#pragma once
#include "pulsetrace/desktop/widget_ipc.hpp"
#include <array>
#include <algorithm>
#include <sstream>
#include <iomanip>
namespace pulsetrace {
inline constexpr std::array<const wchar_t *,4> widget_fonts={L"Segoe UI Variable Text",L"Segoe UI",L"Microsoft YaHei UI",L"Consolas"};
inline std::wstring widget_rate(double value,unsigned unit,unsigned precision){
    if(!std::isfinite(value)||value<0||unit>3||precision>3)return L"—";
    unsigned scale=unit;const wchar_t *suffix;
    if(unit==0){scale=0;while(value>=1024&&scale<3){value/=1024;++scale;}suffix=std::array{L"B/s",L"KiB/s",L"MiB/s",L"GiB/s"}[scale];}
    else {const unsigned power=unit-1;for(unsigned n=0;n<power;n++)value/=1024;suffix=std::array{L"B/s",L"KiB/s",L"MiB/s"}[power];}
    std::wostringstream text;text<<std::fixed<<std::setprecision(precision)<<value<<L" "<<suffix;return text.str();
}
inline int widget_width(const WidgetSnapshot &s){return ((s.unit==1?184:s.unit==2?160:144)+static_cast<int>(s.precision)*7)*static_cast<int>(s.font_size)/10;}
inline bool widget_style_changed(const WidgetSnapshot &a,const WidgetSnapshot &b){return a.font!=b.font||a.font_size!=b.font_size||a.unit!=b.unit||a.precision!=b.precision||a.upload_color!=b.upload_color||a.download_color!=b.download_color||a.offset_x!=b.offset_x||a.offset_y!=b.offset_y;}
}
