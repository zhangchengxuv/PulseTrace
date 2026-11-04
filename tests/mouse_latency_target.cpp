// Visual target for external click-to-photon A/B testing. No hook, injection,
// coordinate access, timestamps, or event log. Only an in-memory click count.
#include <windows.h>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
std::uint64_t clicks{};
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_LBUTTONDOWN:
        ++clicks; InvalidateRect(window, nullptr, FALSE); UpdateWindow(window); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client);
        const auto brush = CreateSolidBrush(clicks % 2 ? RGB(245, 245, 245) : RGB(16, 16, 16));
        FillRect(dc, &client, brush); DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, clicks % 2 ? RGB(16, 16, 16) : RGB(245, 245, 245));
        const auto text = L"Click-to-photon target. Clicks: " + std::to_wstring(clicks);
        DrawTextW(dc, text.c_str(), -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(window, &paint); return 0;
    }
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}
}
int main() {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = procedure;
    wc.lpszClassName = L"PulseTrace.LatencyTarget"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) return 1;
    // No CS_DBLCLKS: every left DOWN changes color, including rapid second clicks.
    const auto window = CreateWindowW(wc.lpszClassName, L"PulseTrace latency validation target", WS_OVERLAPPEDWINDOW,
                                      CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, instance, nullptr);
    if (!window) { UnregisterClassW(wc.lpszClassName, instance); return 1; }
    ShowWindow(window, SW_SHOW);
    MSG msg{}; int status{};
    while ((status = GetMessageW(&msg, nullptr, 0, 0)) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    UnregisterClassW(wc.lpszClassName, instance);
    std::cout << "Target aggregate clicks: " << clicks << ". Measure click-to-photon delay externally; this number is not a latency result.\n";
    return status == -1 ? 1 : 0;
}
