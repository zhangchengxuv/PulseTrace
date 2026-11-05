#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <cmath>
namespace pulsetrace {
inline constexpr std::uint32_t widget_protocol=4,widget_magic=0x50545731;
struct WidgetSnapshot {std::uint32_t magic{widget_magic},version{widget_protocol},owner_pid{},mode{},monitor{},language{},background_alpha{};std::uint64_t owner_window{},tick{};double upload{},download{};std::uint32_t flags{},font{},font_size{10},unit{},precision{1},upload_color{RGB(52,152,219)},download_color{RGB(139,196,234)};std::int32_t offset_x{},offset_y{};};
struct WidgetFeedback {std::uint32_t version{widget_protocol},mode{2},attempts{},win32_error{},visibility{},paint_count{},text_samples{},screen_matches{},transparent_pixels{},opaque_pixels{},partial_pixels{},background_alpha{};std::uint64_t hwnd{},parent{};RECT bounds{};wchar_t reason[160]{};};
struct alignas(64) WidgetShared {volatile LONG data_sequence{};WidgetSnapshot data{};alignas(64) volatile LONG feedback_sequence{};WidgetFeedback feedback{};};
template<class T> void publish_snapshot(volatile LONG &sequence,T &storage,const T &value){InterlockedIncrement(&sequence);MemoryBarrier();std::memcpy(&storage,&value,sizeof(T));MemoryBarrier();InterlockedIncrement(&sequence);}
template<class T> bool read_snapshot(volatile LONG &sequence,const T &storage,T &value){for(int i=0;i<8;++i){const auto before=InterlockedCompareExchange(&sequence,0,0);if(before&1)continue;MemoryBarrier();std::memcpy(&value,&storage,sizeof(T));MemoryBarrier();if(before==InterlockedCompareExchange(&sequence,0,0))return true;}return false;}
inline std::wstring widget_mapping_name(DWORD pid){return L"Local\\PulseTrace.Widget."+std::to_wstring(pid);}
inline UINT widget_message(){static const UINT message=RegisterWindowMessageW(L"PulseTrace.Widget.Action.v1");return message;}
inline bool valid_snapshot(const WidgetSnapshot &s){return s.magic==widget_magic&&s.version==widget_protocol&&s.mode<=2&&s.monitor<=32&&s.background_alpha<=96&&s.font<4&&s.font_size>=8&&s.font_size<=12&&s.unit<=3&&s.precision<=3&&s.upload_color<=0xffffff&&s.download_color<=0xffffff&&s.offset_x>=-200&&s.offset_x<=200&&s.offset_y>=-200&&s.offset_y<=200&&std::isfinite(s.upload)&&std::isfinite(s.download)&&s.upload>=0&&s.download>=0;}
} // namespace pulsetrace
