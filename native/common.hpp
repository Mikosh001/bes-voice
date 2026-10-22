#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <array>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
using Bytes = std::vector<unsigned char>;
inline std::string utf8(const std::wstring& s){
  if(s.empty()) return {}; int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),0,0,0,0);
  std::string r(n,0); WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),r.data(),n,0,0); return r;
}
inline std::wstring wide(const std::string& s){
  if(s.empty()) return {}; int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),0,0);
  std::wstring r(n,0); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),r.data(),n); return r;
}
inline Bytes randomBytes(size_t n){Bytes b(n); if(BCryptGenRandom(0,b.data(),(ULONG)n,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) throw std::runtime_error("Random generator"); return b;}
inline Bytes hash(const std::string& s,LPCWSTR kind=BCRYPT_SHA256_ALGORITHM){
  BCRYPT_ALG_HANDLE a=0; BCRYPT_HASH_HANDLE h=0;
  if(BCryptOpenAlgorithmProvider(&a,kind,0,0)<0) throw std::runtime_error("Hash provider");
  DWORD n=0,cb=0; BCryptGetProperty(a,BCRYPT_HASH_LENGTH,(PUCHAR)&n,sizeof n,&cb,0);
  Bytes b(n); NTSTATUS st=BCryptCreateHash(a,&h,0,0,0,0,0);
  if(st>=0) st=BCryptHashData(h,(PUCHAR)s.data(),(ULONG)s.size(),0);
  if(st>=0) st=BCryptFinishHash(h,b.data(),n,0);
  if(h) BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(a,0);
  if(st<0) throw std::runtime_error("Hash failed"); return b;
}
inline std::string hex(const Bytes& b){static const char t[]="0123456789abcdef"; std::string s;for(auto x:b){s+=t[x>>4];s+=t[x&15];}return s;}
inline void put32(Bytes& b,uint32_t n){for(int i=0;i<4;i++)b.push_back((unsigned char)(n>>(i*8)));}
inline uint32_t get32(const unsigned char* p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline bool validCode(const std::wstring& s){if(s.size()!=32)return false;for(auto c:s)if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')))return false;return true;}
class Crypto {
  BCRYPT_ALG_HANDLE a=0; BCRYPT_KEY_HANDLE k=0; Bytes object;
public:
  Crypto()=default; Crypto(const Crypto&)=delete;
  ~Crypto(){if(k)BCryptDestroyKey(k);if(a)BCryptCloseAlgorithmProvider(a,0);}
  bool init(const std::string& code){
    if(BCryptOpenAlgorithmProvider(&a,BCRYPT_AES_ALGORITHM,0,0)<0)return false;
    if(BCryptSetProperty(a,BCRYPT_CHAINING_MODE,(PUCHAR)BCRYPT_CHAIN_MODE_GCM,sizeof(BCRYPT_CHAIN_MODE_GCM),0)<0)return false;
    DWORD n=0,cb=0; BCryptGetProperty(a,BCRYPT_OBJECT_LENGTH,(PUCHAR)&n,sizeof n,&cb,0);object.resize(n);
    auto key=hash("BES/audio/v1/"+code);
    return BCryptGenerateSymmetricKey(a,&k,object.data(),n,key.data(),(ULONG)key.size(),0)>=0;
  }
  bool crypt(bool encrypt,const Bytes& in,const unsigned char* nonce,const unsigned char* aad,Bytes& out,unsigned char* tag){
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info; BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce=(PUCHAR)nonce;info.cbNonce=12;info.pbAuthData=(PUCHAR)aad;info.cbAuthData=4;info.pbTag=tag;info.cbTag=16;
    out.resize(in.size());ULONG written=0;
    auto st=encrypt?BCryptEncrypt(k,(PUCHAR)in.data(),(ULONG)in.size(),&info,0,0,out.data(),(ULONG)out.size(),&written,0):BCryptDecrypt(k,(PUCHAR)in.data(),(ULONG)in.size(),&info,0,0,out.data(),(ULONG)out.size(),&written,0);
    if(st<0){out.clear();return false;}out.resize(written);return true;
  }
};
inline std::wstring executableDir(){wchar_t p[32768];GetModuleFileNameW(0,p,32768);std::wstring s=p;return s.substr(0,s.find_last_of(L"\\/"));}
inline void logLine(const std::string& s){fprintf(stdout,"%s\n",s.c_str());fflush(stdout);}
