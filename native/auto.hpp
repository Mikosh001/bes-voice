#pragma once
#include "voice.hpp"
#include <condition_variable>

struct RoomProfile {
 std::wstring server,secret;
 bool valid() const {
  if(!validCode(secret)||server.size()>1024)return false;
  if(server.rfind(L"wss://",0)==0)return server.size()>6&&server.find(L'#')==std::wstring::npos;
  return server.rfind(L"ws://127.0.0.1:",0)==0;
 }
};

inline RoomProfile readPortableProfile(const std::wstring& path){
 wchar_t server[1100]{},secret[64]{};
 GetPrivateProfileStringW(L"Room",L"server",L"",server,1100,path.c_str());
 GetPrivateProfileStringW(L"Room",L"key",L"",secret,64,path.c_str());
 RoomProfile p{server,secret};return p.valid()?p:RoomProfile{};
}

inline RoomProfile readEmbeddedProfile(){
 auto module=GetModuleHandleW(0);auto resource=FindResourceW(module,MAKEINTRESOURCEW(201),RT_RCDATA);
 if(!resource)return {};auto length=SizeofResource(module,resource);if(length<1||length>4096)return {};
 auto loaded=LoadResource(module,resource);auto data=(const char*)LockResource(loaded);if(!data)return {};
 std::string text(data,length);auto split=text.find('\n');if(split==std::string::npos)return {};
 RoomProfile profile{wide(text.substr(0,split)),wide(text.substr(split+1))};
 return profile.valid()?profile:RoomProfile{};
}

inline bool saveProtectedProfile(const std::wstring& path,const RoomProfile& p){
 if(!p.valid())return false;auto text=utf8(p.server+L"\n"+p.secret);
 DATA_BLOB in{(DWORD)text.size(),(BYTE*)text.data()},out{};
 if(!CryptProtectData(&in,L"BES room",0,0,0,CRYPTPROTECT_UI_FORBIDDEN,&out))return false;
 auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
 bool ok=false;if(file!=INVALID_HANDLE_VALUE){DWORD written=0;ok=WriteFile(file,out.pbData,out.cbData,&written,0)&&written==out.cbData;CloseHandle(file);}
 SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return ok;
}

inline RoomProfile readProtectedProfile(const std::wstring& path){
 auto file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);if(file==INVALID_HANDLE_VALUE)return {};
 DWORD length=GetFileSize(file,0);if(length<1||length>8192){CloseHandle(file);return {};}
 Bytes bytes(length);DWORD count=0;bool ok=ReadFile(file,bytes.data(),length,&count,0)&&count==length;CloseHandle(file);if(!ok)return {};
 DATA_BLOB in{length,bytes.data()},out{};if(!CryptUnprotectData(&in,0,0,0,0,CRYPTPROTECT_UI_FORBIDDEN,&out))return {};
 std::string text((char*)out.pbData,out.cbData);SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);
 auto split=text.find('\n');if(split==std::string::npos)return {};RoomProfile p{wide(text.substr(0,split)),wide(text.substr(split+1))};SecureZeroMemory(text.data(),text.size());return p.valid()?p:RoomProfile{};
}

class AutoVoice {
 VoiceSession& voice;std::thread worker;std::mutex mutex;std::condition_variable wake;
 RoomProfile profile;std::wstring nick;unsigned input=WAVE_MAPPER,output=WAVE_MAPPER;bool synthetic=false;
 bool enabled=false,quitting=false;uint64_t generation=0;
 std::atomic<int> attempts{0};std::atomic<ULONGLONG> retryAt{0};
 void loop(){
  uint64_t applied=UINT64_MAX;int failures=0;ULONGLONG pingAt=0;bool pending=false;
  for(;;){
   RoomProfile room;std::wstring name;unsigned in,out;bool bot,run;uint64_t revision;
   {std::unique_lock<std::mutex> lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(100));if(quitting)break;room=profile;name=nick;in=input;out=output;bot=synthetic;run=enabled;revision=generation;}
   auto now=GetTickCount64();
   if(applied!=revision){voice.stop();applied=revision;failures=0;pending=false;retryAt=0;pingAt=now;}
   if(!run||!room.valid()||name.empty())continue;
   if(voice.connected){failures=0;pending=false;retryAt=0;if(now>=pingAt){voice.heartbeat();pingAt=now+2000;}continue;}
   if(voice.running)continue;
   if(pending){failures=std::min(failures+1,5);retryAt=now+(ULONGLONG)std::min(15000,1000*(1<<failures));pending=false;}
   if(now<retryAt.load())continue;
   voice.start(room.server,room.secret,name,in,out,bot);attempts++;pending=true;
  }
  voice.stop();
 }
public:
 explicit AutoVoice(VoiceSession& v):voice(v){}
 ~AutoVoice(){stop();}
 void start(){if(!worker.joinable())worker=std::thread([this]{loop();});}
 void configure(const RoomProfile& p,const std::wstring& name,unsigned in=WAVE_MAPPER,unsigned out=WAVE_MAPPER,bool bot=false){
  {std::lock_guard<std::mutex> lock(mutex);if(enabled&&profile.server==p.server&&profile.secret==p.secret&&nick==name&&input==in&&output==out&&synthetic==bot)return;profile=p;nick=name;input=in;output=out;synthetic=bot;enabled=true;generation++;}wake.notify_one();
 }
 void setEnabled(bool on){{std::lock_guard<std::mutex> lock(mutex);if(enabled==on)return;enabled=on;generation++;}wake.notify_one();}
 bool isEnabled(){std::lock_guard<std::mutex> lock(mutex);return enabled;}
 int getAttempts()const{return attempts;}
 std::wstring status(){
  if(!isEnabled())return L"Автоматты байланыс тоқтатылған";
  if(voice.connected)return voice.getStatus();
  auto now=GetTickCount64(),retry=retryAt.load();
  if(retry>now)return L"Өзі қайта қосылады · "+std::to_wstring((retry-now+999)/1000)+L" сек";
  return L"Автоматты қосылуда… Сервердің оянуын күтуі мүмкін.";
 }
 void stop(){
  {std::lock_guard<std::mutex> lock(mutex);quitting=true;}wake.notify_one();if(worker.joinable())worker.join();
 }
};
