#include "cloud_internal.hpp"
#include <winhttp.h>
#include <array>
namespace pulsetrace {
CloudResponse cloud_https_request(const CloudRequest &r,std::atomic_bool &cancel){
 using namespace cloud_detail;
 const auto url=wide(r.url);URL_COMPONENTS u{};u.dwStructSize=sizeof(u);u.dwHostNameLength=u.dwUrlPathLength=u.dwExtraInfoLength=u.dwUserNameLength=u.dwPasswordLength=static_cast<DWORD>(-1);
 if(!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&u)||u.nScheme!=INTERNET_SCHEME_HTTPS||u.dwUserNameLength||u.dwPasswordLength||u.dwExtraInfoLength)throw std::runtime_error("CLOUD_HTTPS_REQUIRED");
 struct Handle{HINTERNET h{};~Handle(){if(h)WinHttpCloseHandle(h);}} session,connection,request;
 const auto check=[&](bool success){if(!success)throw std::runtime_error(cancel?"CLOUD_CANCELLED":"CLOUD_NETWORK_OR_TLS_FAILED");if(cancel)throw std::runtime_error("CLOUD_CANCELLED");};
 session.h=WinHttpOpen(L"PulseTrace WebDAV/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);check(session.h!=nullptr);
 check(WinHttpSetTimeouts(session.h,5000,5000,10000,10000));DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2|WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;if(!WinHttpSetOption(session.h,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols))){protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;check(WinHttpSetOption(session.h,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols)));}
 connection.h=WinHttpConnect(session.h,std::wstring(u.lpszHostName,u.dwHostNameLength).c_str(),u.nPort,0);check(connection.h!=nullptr);
 const auto path=u.dwUrlPathLength?std::wstring(u.lpszUrlPath,u.dwUrlPathLength):L"/";
 request.h=WinHttpOpenRequest(connection.h,wide(r.method).c_str(),path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);check(request.h!=nullptr);
 DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;check(WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects)));DWORD logon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;check(WinHttpSetOption(request.h,WINHTTP_OPTION_AUTOLOGON_POLICY,&logon,sizeof(logon)));
 const auto account=wide(r.account),password=wide(r.password);check(WinHttpSetCredentials(request.h,WINHTTP_AUTH_TARGET_SERVER,WINHTTP_AUTH_SCHEME_BASIC,account.c_str(),password.c_str(),nullptr));
 std::wstring headers=L"Depth: "+std::wstring(r.depth==1?L"1":L"0")+L"\r\nContent-Type: application/octet-stream\r\n";if(r.create_only)headers+=L"If-None-Match: *\r\n";
 check(WinHttpSendRequest(request.h,headers.c_str(),static_cast<DWORD>(headers.size()),WINHTTP_NO_REQUEST_DATA,0,static_cast<DWORD>(r.body.size()),0));
 const auto deadline=GetTickCount64()+180000;
 for(size_t offset=0;offset<r.body.size();){DWORD written{},chunk=static_cast<DWORD>(std::min<size_t>(65536,r.body.size()-offset));check(WinHttpWriteData(request.h,r.body.data()+offset,chunk,&written));if(!written||GetTickCount64()>deadline)throw std::runtime_error("CLOUD_UPLOAD_INTERRUPTED");offset+=written;}
 check(WinHttpReceiveResponse(request.h,nullptr));DWORD status{},size=sizeof(status);check(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX));
 CloudResponse result{static_cast<unsigned>(status),{}};std::array<unsigned char,65536> buffer{};
 for(;;){DWORD read{};check(WinHttpReadData(request.h,buffer.data(),static_cast<DWORD>(buffer.size()),&read));if(!read)break;const auto limit=r.method=="PROPFIND"?2*1024*1024:max_bytes+52;if(result.body.size()+read>limit||GetTickCount64()>deadline)throw std::runtime_error("CLOUD_RESPONSE_LIMIT");result.body.insert(result.body.end(),buffer.begin(),buffer.begin()+read);}
 return result;
}
}
