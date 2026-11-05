#pragma once
#include <windows.h>
#include <vector>
namespace pulsetrace {
inline bool inside_rect(const RECT &a,const RECT &b){return a.left>=b.left&&a.top>=b.top&&a.right<=b.right&&a.bottom<=b.bottom&&a.right>a.left&&a.bottom>a.top;}
inline bool taskbar_slot(const RECT &bar,const RECT &notify,const std::vector<RECT> &occupied,UINT dpi,RECT &slot,int offset_x=0,int offset_y=0,int width_dip=138){
 if(dpi<72||dpi>768||bar.right-bar.left<4*(bar.bottom-bar.top)||!inside_rect(notify,bar))return false;
 const int width=MulDiv(width_dip,dpi,96),height=MulDiv(36,dpi,96),gap=MulDiv(4,dpi,96);
 slot={notify.left-gap-width,bar.top+(bar.bottom-bar.top-height)/2,notify.left-gap,bar.top+(bar.bottom-bar.top+height)/2};
 const int dx=MulDiv(offset_x,dpi,96),dy=MulDiv(offset_y,dpi,96);OffsetRect(&slot,dx,dy);
 if(slot.right>notify.left-gap||!inside_rect(slot,bar))return false;RECT overlap{};for(const auto &r:occupied)if(IntersectRect(&overlap,&slot,&r))return false;return true;
}
}
