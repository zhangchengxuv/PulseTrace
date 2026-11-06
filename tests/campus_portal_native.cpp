#include "pulsetrace/desktop/campus_portal_host.hpp"
#include "pulsetrace/campus.hpp"
#include "pulsetrace/protocol.hpp"
#include "pulsetrace/probe_report.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <iostream>
#include <thread>
#include <wtsapi32.h>
using namespace pulsetrace;
namespace {
class PortalServer{
 public:
  PortalServer(){
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data)!=0)throw std::runtime_error("WSAStartup failed");
    listener_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener_==INVALID_SOCKET)throw std::runtime_error("socket failed");
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
    if(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0||listen(listener_,4)!=0)throw std::runtime_error("loopback bind failed");
    int length=sizeof(address);getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&length);port_=ntohs(address.sin_port);
    worker_=std::thread([this]{serve();});
  }
  ~PortalServer(){closed_=true;if(listener_!=INVALID_SOCKET){shutdown(listener_,SD_BOTH);closesocket(listener_);}if(worker_.joinable())worker_.join();WSACleanup();}
  std::string url()const{return "http://127.0.0.1:"+std::to_string(port_)+"/usage";}
  void change_balance(){++revision_;}
  void reject_requests(bool value){reject_=value;}
  unsigned requests()const{return requests_;}
 private:
  void serve(){
    while(!closed_){
      auto client=accept(listener_,nullptr,nullptr);if(client==INVALID_SOCKET)return;
      char request[4096]{};recv(client,request,sizeof(request)-1,0);
      ++requests_;
      if(reject_){closesocket(client);continue;}
      std::string body=R"HTML(<!doctype html><html><head><meta charset="utf-8"><title>Campus mock</title></head><body>
<h1>Usage Center</h1><table>
<tr><th>Balance</th><td id="balance">¥ 27.50</td></tr>
<tr><th>Used data</th><td id="used">2.5 GiB</td></tr>
<tr><th>Remaining data</th><td id="remaining">7.5 GiB</td></tr>
<tr><th>Quota</th><td id="quota">10 GiB</td></tr>
<tr><th>Fee</th><td id="fee">¥ 1.20</td></tr>
<tr><th>Billing cycle</th><td id="cycle">2026-11</td></tr>
</table><button id="manual">Balance ¥ 30.00</button><script>setTimeout(()=>document.getElementById('manual').click(),2500);</script></body></html>)HTML";
      if(revision_)body.replace(body.find("27.50"),5,std::to_string(27.5+revision_.load()));
      const std::string response="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
      send(client,response.data(),static_cast<int>(response.size()),0);closesocket(client);
    }
  }
  SOCKET listener_{INVALID_SOCKET};unsigned short port_{};std::atomic<bool> closed_{},reject_{};std::atomic<unsigned> revision_{},requests_{};std::thread worker_;
};
}
int wmain(){
  try{
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    PortalServer server;const auto root=executable_data_directory()/("campus-portal-native-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(root);
    Json candidates=nullptr,snapshot=nullptr,manual=nullptr,error=nullptr;unsigned picker_ready_count{};std::string ready_field;
    CampusPortalHost host(GetModuleHandleW(nullptr),root,executable_data_directory().parent_path(),
      [&](Json event){
        if(event.value("type","")=="campus_candidates")candidates=event["candidates"];
        else if(event.value("type","")=="campus_snapshot")snapshot=event["snapshot"];
        else if(event.value("type","")=="campus_candidate")manual=event;
        else if(event.value("type","")=="campus_error")error=event.value("message","");
        else if(event.value("type","")=="campus_picker_ready"){++picker_ready_count;ready_field=event.value("field","");}
      },[&](std::string text){if(text.find("failed")!=std::string::npos)error=text;});
    Json rule={{"fields",{
      {"balance",{{"selector","#balance"},{"kind","currency"},{"unit","CNY"},{"confidence",1}}},
      {"used_bytes",{{"selector","#used"},{"kind","bytes"},{"unit","GiB"},{"confidence",1}}},
      {"remaining_bytes",{{"selector","#remaining"},{"kind","bytes"},{"unit","GiB"},{"confidence",1}}},
      {"quota_bytes",{{"selector","#quota"},{"kind","bytes"},{"unit","GiB"},{"confidence",1}}},
      {"fee",{{"selector","#fee"},{"kind","currency"},{"unit","CNY"},{"confidence",1}}},
      {"billing_cycle",{{"selector","#cycle"},{"kind","text"},{"unit","text"},{"confidence",1}}}
    }}};
    host.open(server.url(),5,rule,{campus_origin(server.url())},true);host.close();
    host.open(server.url(),5,rule,{campus_origin(server.url())},true);
    host.open(server.url(),5,rule,{campus_origin(server.url())},true);
    if(host.visible())throw std::runtime_error("Background portal opened a visible window");
    // Click before WebView2 initialization completes. The portal must appear now,
    // with the most recent field armed after loading, rather than throwing.
    host.pick("used_free_bytes");host.pick("balance");
    if(!host.visible())throw std::runtime_error("Early picker request did not show the portal");
    const auto deadline=GetTickCount64()+25000;MSG message{};
    while(GetTickCount64()<deadline&&(snapshot.is_null()||candidates.is_null()||manual.is_null())){
      while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
      if(!error.is_null())throw std::runtime_error(error.get<std::string>());
      Sleep(20);
    }
    if(candidates.is_null()||snapshot.is_null()||manual.is_null())throw std::runtime_error("Campus portal WebView2 scan or manual picker timed out");
    if(ready_field!="balance"||!picker_ready_count)throw std::runtime_error("Latest pending picker field did not become ready");
    if(snapshot.value("balance",0.0)!=27.5||snapshot.value("used_bytes",0.0)!=2.5*1024.0*1024.0*1024.0||snapshot.value("quota_bytes",0.0)!=10.0*1024.0*1024.0*1024.0)
      throw std::runtime_error("Campus portal parsed values mismatch");
    const auto pump_until=[&](auto done){const auto end=GetTickCount64()+10000;while(GetTickCount64()<end&&!done()){
      while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(20);
    }if(!done())throw std::runtime_error("Campus portal update timed out");};
    rule["fields"]["balance"]["selector"]="#manual";snapshot=nullptr;host.update_rule(rule);host.rescan();
    pump_until([&]{return !snapshot.is_null()&&snapshot.value("balance",0.0)==30;});
    rule["fields"]["balance"]["selector"]="#balance";host.update_rule(rule);host.hide();server.change_balance();snapshot=nullptr;host.synchronize();
    pump_until([&]{return !snapshot.is_null()&&snapshot.value("balance",0.0)==28.5;});
    if(host.visible())throw std::runtime_error("Background sync showed the portal");
    const Json context={{"enabled",true},{"portal_url",server.url()},{"allowed_origins",Json::array({campus_origin(server.url())})}};
    if(!host.matches_scope(context))throw std::runtime_error("Unchanged authorized scope was rejected");
    auto disabled=context;disabled["enabled"]=false;
    if(host.matches_scope(disabled))throw std::runtime_error("Disabled monitoring retained authorization");
    HWND portal_window{};EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM target)->BOOL{wchar_t name[128]{};GetClassNameW(h,name,128);if(std::wstring(name)==L"PulseTrace.CampusPortal"){*reinterpret_cast<HWND*>(target)=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&portal_window));
    if(!portal_window)throw std::runtime_error("Own portal test window not found");
    for(const auto field:campus_field_names){
        const auto request=parse_message(Json{{"type","campus_pick_field"},{"request_id",51},{"params",{{"field",field}}}}.dump());
        host.hide();const auto before=picker_ready_count;
        host.pick(request["params"]["field"].get<std::string>());
        pump_until([&]{return picker_ready_count>before;});
        if(!host.visible()||ready_field!=field)throw std::runtime_error("Picker did not open and arm every configured field");
    }
    const auto requests_before_pick=server.requests(),ready_before_pick=picker_ready_count;
    ShowWindow(portal_window,SW_MINIMIZE);host.pick("balance");
    if(!host.visible()||IsIconic(portal_window))throw std::runtime_error("Picker did not restore the minimized portal");
    pump_until([&]{return picker_ready_count>ready_before_pick;});
    if(server.requests()!=requests_before_pick)throw std::runtime_error("Picking reloaded the authenticated page");
    SendMessageW(portal_window,WM_CLOSE,0,0);snapshot=nullptr;server.change_balance();
    SendMessageW(portal_window,WM_TIMER,2,0);
    pump_until([&]{return !snapshot.is_null()&&snapshot.value("balance",0.0)==29.5;});
    snapshot=nullptr;server.change_balance();
    SendMessageW(portal_window,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);
    SendMessageW(portal_window,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
    SendMessageW(portal_window,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
    SendMessageW(portal_window,WM_TIMER,2,0);
    const auto paused=GetTickCount64()+300;while(GetTickCount64()<paused){while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(20);}
    if(!snapshot.is_null())throw std::runtime_error("Unlock resumed scanning while power was suspended");
    SendMessageW(portal_window,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
    pump_until([&]{return !snapshot.is_null()&&snapshot.value("balance",0.0)==30.5;});
    if(host.visible())throw std::runtime_error("Resume showed a hidden portal window");
    server.reject_requests(true);error=nullptr;host.synchronize();pump_until([&]{return !error.is_null();});
    if(snapshot.value("balance",0.0)!=30.5)throw std::runtime_error("Failed query replaced valid data");
    server.reject_requests(false);server.change_balance();SendMessageW(portal_window,WM_TIMER,2,0);
    pump_until([&]{return snapshot.value("balance",0.0)==31.5;});
    host.close();const auto ready_before_reopen=picker_ready_count;host.pick("balance");
    if(!host.visible())throw std::runtime_error("Picker did not recreate the closed portal");
    pump_until([&]{return picker_ready_count>ready_before_reopen;});host.close();
    CoUninitialize();
    std::cout<<"Native campus WebView2 portal recognition passed; candidates="<<candidates.size()<<"\n";return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
