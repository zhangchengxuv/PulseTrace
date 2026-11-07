#include "pulsetrace/cloud_backup.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <stdexcept>
namespace pulsetrace {
namespace {
void ok(NTSTATUS status){if(status<0)throw std::runtime_error("CLOUD_CRYPTO_FAILED");}
struct Alg{BCRYPT_ALG_HANDLE h{};Alg(LPCWSTR name,ULONG flags=0){ok(BCryptOpenAlgorithmProvider(&h,name,nullptr,flags));}~Alg(){if(h)BCryptCloseAlgorithmProvider(h,0);}};
struct Key{BCRYPT_KEY_HANDLE h{};~Key(){if(h)BCryptDestroyKey(h);}};
struct Secret{std::array<unsigned char,32> bytes{};~Secret(){SecureZeroMemory(bytes.data(),bytes.size());}};
constexpr std::array<unsigned char,8> magic={'P','T','D','A','V','0','0','1'};
constexpr size_t header=52,limit=128*1024*1024;
CloudBytes crypt(std::span<const unsigned char> data,const std::string &password,bool encrypt){
 if(password.size()<12||password.size()>1024)throw std::runtime_error("CLOUD_ENCRYPTION_PASSWORD_REQUIRED");
 if(data.size()>(encrypt?limit:limit+header)||(!encrypt&&(data.size()<header||!std::equal(magic.begin(),magic.end(),data.begin()))))throw std::runtime_error("CLOUD_ARCHIVE_FORMAT");
 CloudBytes result(encrypt?data.size()+header:data.size()-header);
 std::array<unsigned char,header> prefix{};
 if(encrypt){std::copy(magic.begin(),magic.end(),prefix.begin());ok(BCryptGenRandom(nullptr,prefix.data()+8,28,BCRYPT_USE_SYSTEM_PREFERRED_RNG));}
 else std::copy_n(data.begin(),header,prefix.begin());
 Alg sha(BCRYPT_SHA256_ALGORITHM,BCRYPT_ALG_HANDLE_HMAC_FLAG);Secret derived;
 ok(BCryptDeriveKeyPBKDF2(sha.h,reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),static_cast<ULONG>(password.size()),prefix.data()+8,16,200000,derived.bytes.data(),32,0));
 Alg aes(BCRYPT_AES_ALGORITHM);ok(BCryptSetProperty(aes.h,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0));
 Key key;ok(BCryptGenerateSymmetricKey(aes.h,&key.h,nullptr,0,derived.bytes.data(),32,0));
 BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;BCRYPT_INIT_AUTH_MODE_INFO(info);
 info.pbNonce=prefix.data()+24;info.cbNonce=12;info.pbTag=prefix.data()+36;info.cbTag=16;info.pbAuthData=prefix.data();info.cbAuthData=24;
 ULONG written{};const auto input=encrypt?data:data.subspan(header);
 const auto code=encrypt?BCryptEncrypt(key.h,const_cast<PUCHAR>(input.data()),static_cast<ULONG>(input.size()),&info,nullptr,0,result.data()+header,static_cast<ULONG>(input.size()),&written,0):
  BCryptDecrypt(key.h,const_cast<PUCHAR>(input.data()),static_cast<ULONG>(input.size()),&info,nullptr,0,result.data(),static_cast<ULONG>(result.size()),&written,0);
 if(code<0){SecureZeroMemory(result.data(),result.size());throw std::runtime_error(encrypt?"CLOUD_CRYPTO_FAILED":"CLOUD_PASSWORD_OR_INTEGRITY_FAILED");}
 if(encrypt)std::copy(prefix.begin(),prefix.end(),result.begin());return result;
}
}
CloudBytes cloud_encrypt(std::span<const unsigned char> data,const std::string &password){return crypt(data,password,true);}
CloudBytes cloud_decrypt(std::span<const unsigned char> data,const std::string &password){return crypt(data,password,false);}
std::string cloud_sha256(std::span<const unsigned char> data){Alg alg(BCRYPT_SHA256_ALGORITHM);std::array<unsigned char,32> out{};ok(BCryptHash(alg.h,nullptr,0,const_cast<PUCHAR>(data.data()),static_cast<ULONG>(data.size()),out.data(),32));constexpr char hex[]="0123456789abcdef";std::string result;for(auto c:out){result+=hex[c>>4];result+=hex[c&15];}return result;}
}
