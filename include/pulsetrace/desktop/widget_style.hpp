#pragma once
#include "pulsetrace/desktop/widget_ipc.hpp"
#include <array>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <vector>
namespace pulsetrace {
inline constexpr std::array<const wchar_t *,4> widget_fonts={L"Segoe UI Variable Text",L"Segoe UI",L"Microsoft YaHei UI",L"Consolas"};
inline std::wstring widget_rate(double value,unsigned unit,unsigned precision){
    if(!std::isfinite(value)||value<0||unit>3||precision>3)return L"—";
    unsigned scale=unit;const wchar_t *suffix;
    if(unit==0){scale=0;while(value>=1024&&scale<3){value/=1024;++scale;}suffix=std::array{L"B/s",L"KiB/s",L"MiB/s",L"GiB/s"}[scale];}
    else {const unsigned power=unit-1;for(unsigned n=0;n<power;n++)value/=1024;suffix=std::array{L"B/s",L"KiB/s",L"MiB/s"}[power];}
    std::wostringstream text;text<<(value>=10000?std::scientific:std::fixed)<<std::setprecision(precision)<<value<<L" "<<suffix;return text.str();
}
inline std::vector<int> widget_advances(HDC dc,const std::wstring &text){
    int digit=0;for(wchar_t c=L'0';c<=L'9';++c){SIZE size{};GetTextExtentPoint32W(dc,&c,1,&size);digit=std::max(digit,static_cast<int>(size.cx));}
    std::vector<int> out;out.reserve(text.size());for(const auto c:text){SIZE size{};GetTextExtentPoint32W(dc,&c,1,&size);out.push_back(c>=L'0'&&c<=L'9'?digit:static_cast<int>(size.cx));}return out;
}
inline int widget_text_width(HDC dc,const std::wstring &text){int width=0;for(const int n:widget_advances(dc,text))width+=n;return width;}
inline SIZE widget_dimensions(const WidgetSnapshot &s,UINT dpi){
    auto dc=CreateCompatibleDC(nullptr);auto font=CreateFontW(-MulDiv(s.font_size,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,widget_fonts[std::min(s.font,3u)]);
    auto old=SelectObject(dc,font);TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);
    const auto suffix=s.unit==1?L"B/s":s.unit==2?L"KiB/s":s.unit==3?L"MiB/s":L"GiB/s";
    const auto decimal=s.precision?L"."+std::wstring(s.precision,L'9'):L"";
    const auto number=std::max(widget_text_width(dc,L"9999"+decimal),widget_text_width(dc,L"9"+decimal+L"e+308"));
    const int pad=MulDiv(4,dpi,96),gap=MulDiv(3,dpi,96),arrow=std::max(widget_text_width(dc,L"\u2191"),widget_text_width(dc,L"\u2193"));
    const int width=std::max(number+widget_text_width(dc,L" "+std::wstring(suffix))+arrow+gap,widget_text_width(dc,L"Rate unavailable"));
    SelectObject(dc,old);DeleteObject(font);DeleteDC(dc);return {width+2*pad,2*tm.tmHeight+2*pad};
}
inline int widget_width(const WidgetSnapshot &s){return widget_dimensions(s,96).cx;}
inline bool widget_style_changed(const WidgetSnapshot &a,const WidgetSnapshot &b){return a.font!=b.font||a.font_size!=b.font_size||a.unit!=b.unit||a.precision!=b.precision||a.upload_color!=b.upload_color||a.download_color!=b.download_color||a.offset_x!=b.offset_x||a.offset_y!=b.offset_y;}
}
