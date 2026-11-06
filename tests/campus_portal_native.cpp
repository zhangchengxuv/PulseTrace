#include "pulsetrace/desktop/campus_portal_host.hpp"
#include "pulsetrace/campus.hpp"
#include "pulsetrace/probe_report.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <iostream>
#include <thread>
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
 private:
  void serve(){
    while(!closed_){
      auto client=accept(listener_,nullptr,nullptr);if(client==INVALID_SOCKET)return;
      char request[4096]{};recv(client,request,sizeof(request)-1,0);
      const std::string body=R"HTML(<!doctype html><html><head><meta charset="utf-8"><title>Campus mock</title></head><body>
<h1>Usage Center</h1><table>
<tr><th>Balance</th><td id="balance">¥ 27.50</td></tr>
<tr><th>Used data</th><td id="used">2.5 GiB</td></tr>
<tr><th>Remaining data</th><td id="remaining">7.5 GiB</td></tr>
<tr><th>Quota</th><td id="quota">10 GiB</td></tr>
<tr><th>Fee</th><td id="fee">¥ 1.20</td></tr>
<tr><th>Billing cycle</th><td id="cycle">2026-11</td></tr>
</table><button id="manual">Balance ¥ 30.00</button><script>setTimeout(()=>document.getElementById('manual').click(),2500);</script></body></html>)HTML";
      const std::string response="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
      send(client,response.data(),static_cast<int>(response.size()),0);closesocket(client);
    }
  }
  SOCKET listener_{INVALID_SOCKET};unsigned short port_{};std::atomic<bool> closed_{};std::thread worker_;
};
}
int wmain(){
  try{
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    PortalServer server;const auto root=executable_data_directory()/("campus-portal-native-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(root);
    Json candidates=nullptr,snapshot=nullptr,manual=nullptr,error=nullptr;
    CampusPortalHost host(GetModuleHandleW(nullptr),root,executable_data_directory().parent_path(),
      [&](Json event){
        if(event.value("type","")=="campus_candidates")candidates=event["candidates"];
        else if(event.value("type","")=="campus_snapshot")snapshot=event["snapshot"];
        else if(event.value("type","")=="campus_candidate")manual=event;
        else if(event.value("type","")=="campus_error")error=event.value("message","");
      },[&](std::string text){if(text.find("failed")!=std::string::npos)error=text;});
    Json rule={{"fields",{
      {"balance",{{"selector","#balance"},{"kind","currency"},{"unit","CNY"},{"confidence",1}}},
      {"used_bytes",{{"selector","#used"},{"kind","bytes"},{"unit","GiB"},{"confidence",1}}},
      {"remaining_bytes",{{"selector","#remaining"},{"kind","bytes"},{"unit","GiB"},{"confidence",1}}},
      {"quota_bytes",{{"selector","#quota"},{"kind","bytes"},{"unit","GiB"},{"confidence",1}}},
      {"fee",{{"selector","#fee"},{"kind","currency"},{"unit","CNY"},{"confidence",1}}},
      {"billing_cycle",{{"selector","#cycle"},{"kind","text"},{"unit","text"},{"confidence",1}}}
    }}};
    host.open(server.url(),5,rule,{campus_origin(server.url())});
    const auto deadline=GetTickCount64()+25000;MSG message{};
    bool picker_started=false;
    while(GetTickCount64()<deadline&&(snapshot.is_null()||candidates.is_null()||manual.is_null())){
      while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
      if(!error.is_null())throw std::runtime_error(error.get<std::string>());
      if(!picker_started&&!candidates.is_null()){picker_started=true;host.pick("balance");}
      Sleep(20);
    }
    host.close();
    if(candidates.is_null()||snapshot.is_null()||manual.is_null())throw std::runtime_error("Campus portal WebView2 scan or manual picker timed out");
    if(snapshot.value("balance",0.0)!=27.5||snapshot.value("used_bytes",0.0)!=2.5*1024.0*1024.0*1024.0||snapshot.value("quota_bytes",0.0)!=10.0*1024.0*1024.0*1024.0)
      throw std::runtime_error("Campus portal parsed values mismatch");
    CoUninitialize();
    std::cout<<"Native campus WebView2 portal recognition passed; candidates="<<candidates.size()<<"\n";return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
