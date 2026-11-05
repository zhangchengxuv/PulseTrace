#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <span>
namespace pulsetrace {
// Top-down BGRA, premultiplied sRGB. GDI is used only for a grayscale glyph mask:
// ClearType's RGB fringes and GDI's undefined alpha never reach the compositor.
inline std::uint32_t widget_over(COLORREF foreground, unsigned coverage, COLORREF background, unsigned background_alpha) {
    const auto mul=[](unsigned a,unsigned b){return (a*b+127)/255;};
    coverage=std::min(coverage,255u);background_alpha=std::min(background_alpha,255u);
    const auto channel=[&](unsigned f,unsigned b){return mul(f,coverage)+mul(mul(b,background_alpha),255-coverage);};
    const auto alpha=coverage+mul(background_alpha,255-coverage);
    return (alpha<<24)|(channel(GetRValue(foreground),GetRValue(background))<<16)|
           (channel(GetGValue(foreground),GetGValue(background))<<8)|channel(GetBValue(foreground),GetBValue(background));
}
class WidgetSurface {
    HDC color_dc_{},mask_dc_{};HBITMAP color_bitmap_{},mask_bitmap_{};
    HGDIOBJ old_color_{},old_mask_{},old_font_{};HFONT font_{};
    std::uint32_t *color_{},*mask_{};int width_{},height_{};UINT dpi_{};
    void reset() noexcept {
        if(old_font_)SelectObject(mask_dc_,old_font_);
        if(font_)DeleteObject(font_);
        if(old_color_)SelectObject(color_dc_,old_color_);
        if(old_mask_)SelectObject(mask_dc_,old_mask_);
        if(color_bitmap_)DeleteObject(color_bitmap_);
        if(mask_bitmap_)DeleteObject(mask_bitmap_);
        if(color_dc_)DeleteDC(color_dc_);
        if(mask_dc_)DeleteDC(mask_dc_);
        color_dc_=mask_dc_=nullptr;color_bitmap_=mask_bitmap_=nullptr;
        old_color_=old_mask_=old_font_=nullptr;font_=nullptr;color_=mask_=nullptr;width_=height_=0;dpi_=0;
    }
public:
    unsigned transparent{},opaque{},partial{};
    WidgetSurface()=default;
    WidgetSurface(const WidgetSurface &)=delete;WidgetSurface &operator=(const WidgetSurface &)=delete;
    ~WidgetSurface(){reset();}
    bool prepare(int width,int height,UINT dpi) {
        if(width==width_&&height==height_&&dpi==dpi_&&color_)return true;
        reset();if(width<=0||height<=0||width>4096||height>1024||dpi<72||dpi>768)return false;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
        info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        color_dc_=CreateCompatibleDC(nullptr);mask_dc_=CreateCompatibleDC(nullptr);
        color_bitmap_=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,reinterpret_cast<void **>(&color_),nullptr,0);
        mask_bitmap_=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,reinterpret_cast<void **>(&mask_),nullptr,0);
        if(!color_dc_||!mask_dc_||!color_bitmap_||!mask_bitmap_){reset();return false;}
        old_color_=SelectObject(color_dc_,color_bitmap_);old_mask_=SelectObject(mask_dc_,mask_bitmap_);
        const auto make=[&](const wchar_t *face){return CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,face);};
        font_=make(L"Segoe UI Variable Text");if(!font_){reset();return false;}
        old_font_=SelectObject(mask_dc_,font_);wchar_t face[128]{};GetTextFaceW(mask_dc_,128,face);
        if(wcsncmp(face,L"Segoe UI",8)){SelectObject(mask_dc_,old_font_);DeleteObject(font_);font_=make(L"Segoe UI");if(!font_){reset();return false;}SelectObject(mask_dc_,font_);}
        SetBkMode(mask_dc_,TRANSPARENT);SetTextColor(mask_dc_,RGB(255,255,255));
        width_=width;height_=height;dpi_=dpi;return true;
    }
    void draw(const std::wstring &first,const std::wstring &second,bool dark,unsigned background_alpha=0) {
        std::fill(mask_,mask_+width_*height_,0u);
        const std::wstring *lines[]={&first,&second};bool arrow[2]{};const int gutter=MulDiv(24,dpi_,96),pad=MulDiv(5,dpi_,96);
        for(int row=0;row<2;++row){RECT r{pad,row*height_/2,width_-pad,(row+1)*height_/2};const auto &line=*lines[row];
            arrow[row]=line.size()>1&&(line[0]==L'\x2193'||line[0]==L'\x2191');
            if(arrow[row]){RECT icon=r;icon.right=gutter;DrawTextW(mask_dc_,line.c_str(),1,&icon,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);r.left=gutter;DrawTextW(mask_dc_,line.c_str()+2,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);}
            else DrawTextW(mask_dc_,line.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
        }
        GdiFlush();transparent=opaque=partial=0;
        const auto background=dark?RGB(10,38,57):RGB(232,241,247),text=dark?RGB(241,245,249):RGB(10,38,57);
        for(int y=0;y<height_;++y)for(int x=0;x<width_;++x){const int index=y*width_+x,row=y<height_/2?0:1;
            const auto c=mask_[index];const unsigned coverage=((c&255)+((c>>8)&255)+((c>>16)&255)+1)/3;
            const auto ink=arrow[row]&&x<gutter?(dark?(row==0?RGB(139,196,234):RGB(52,152,219)):RGB(25,96,144)):text;
            color_[index]=widget_over(ink,coverage,background,background_alpha);const auto a=color_[index]>>24;
            if(a==0)++transparent;else if(a==255)++opaque;else ++partial;
        }
    }
    bool present(HWND window) const {
        SIZE size{width_,height_};POINT source{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        // Destination remains in the parent coordinate space managed by SetWindowPos.
        // No SetLayeredWindowAttributes call is permitted on these fresh ULW windows.
        return UpdateLayeredWindow(window,nullptr,nullptr,&size,color_dc_,&source,0,&blend,ULW_ALPHA)!=FALSE;
    }
    int width()const{return width_;}int height()const{return height_;}
    unsigned alpha(int x,int y)const{return x>=0&&y>=0&&x<width_&&y<height_?color_[y*width_+x]>>24:0;}
    std::span<const std::uint32_t> pixels()const{return {color_,static_cast<std::size_t>(width_*height_)};}
    std::span<const std::uint32_t> mask()const{return {mask_,static_cast<std::size_t>(width_*height_)};}
};
}
