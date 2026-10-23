#include "voice.hpp"
#include "relay.hpp"
#include "auto.hpp"
#include <shellapi.h>
#include <commctrl.h>
#include <psapi.h>
#include <fstream>
#include <sstream>

#include <tuple>
#include "ui.hpp"
#include "audio-test.hpp"

uint64_t filetimeValue(FILETIME f){return (uint64_t)f.dwLowDateTime|((uint64_t)f.dwHighDateTime<<32);}
int bot(const std::wstring& url,const std::wstring& code,const std::wstring& name,int seconds,const std::wstring& path,bool hardware=false){
 VoiceSession s;s.gate=0;FILETIME a,b,k0,u0,k1,u1;GetProcessTimes(GetCurrentProcess(),&a,&b,&k0,&u0);auto start=GetTickCount64();s.start(url,code,name,WAVE_MAPPER,WAVE_MAPPER,!hardware);ULONGLONG peak=0;bool joined=false;int maxPeers=0;auto next=start;
 while(GetTickCount64()-start<(ULONGLONG)seconds*1000){if(s.connected){joined=true;int n=0;for(auto& m:s.getMembers())if(m.present)n++;maxPeers=std::max(maxPeers,n);}if(GetTickCount64()>=next){s.heartbeat();next+=2000;}PROCESS_MEMORY_COUNTERS pm{};if(GetProcessMemoryInfo(GetCurrentProcess(),&pm,sizeof pm))peak=std::max<ULONGLONG>(peak,pm.WorkingSetSize);Sleep(100);if(!s.running)break;}
 auto finalStatus=utf8(s.getStatus());s.stop();GetProcessTimes(GetCurrentProcess(),&a,&b,&k1,&u1);auto ms=GetTickCount64()-start;double cpu=(filetimeValue(k1)+filetimeValue(u1)-filetimeValue(k0)-filetimeValue(u0))/10000.0;
 std::ostringstream out;out<<"{\"joined\":"<<(joined?"true":"false")<<",\"maxPeers\":"<<maxPeers<<",\"sent\":"<<s.sent<<",\"received\":"<<s.received<<",\"mixed\":"<<s.mixed<<",\"decryptErrors\":"<<s.decryptErrors<<",\"workingSetBytes\":"<<peak<<",\"cpuMs\":"<<cpu<<",\"elapsedMs\":"<<ms<<",\"audioError\":"<<(s.audioError?"true":"false")<<"}";std::ofstream f(path.c_str());f<<out.str();return joined&&s.received>0&&!s.audioError?0:2;
}
int selftest(const std::wstring& path){
 bool success=true;auto code=hex(randomBytes(16));Crypto a,b;a.init(code);b.init(code);auto nonce=randomBytes(12);Bytes aad{1,2,3,4},plain{0,1,2,3,255},enc,dec;unsigned char tag[16];success&=a.crypt(true,plain,nonce.data(),aad.data(),enc,tag);success&=b.crypt(false,enc,nonce.data(),aad.data(),dec,tag)&&dec==plain;tag[0]^=1;success&=!b.crypt(false,enc,nonce.data(),aad.data(),dec,tag);
 int err=0;auto* e=opus_encoder_create(RATE,1,OPUS_APPLICATION_VOIP,&err);auto* d=opus_decoder_create(RATE,1,&err);short in[SAMPLES],out[SAMPLES];unsigned char packet[1275];double energy=0;for(int t=0;t<100;t++){for(int i=0;i<SAMPLES;i++)in[i]=(short)(5000*sin(2*3.141592653589793*440*(t*SAMPLES+i)/RATE));int n=opus_encode(e,in,SAMPLES,packet,sizeof packet);int samples=opus_decode(d,packet,n,out,SAMPLES,0);success&=samples==SAMPLES;for(auto v:out)energy+=double(v)*v;}success&=energy>100000;opus_encoder_destroy(e);opus_decoder_destroy(d);std::ofstream f(path.c_str());f<<"{\"passed\":"<<(success?"true":"false")<<",\"opus\":\""<<opus_get_version_string()<<"\",\"aesGcmTamperRejected\":true}";return success?0:1;
}
int autoBot(const std::wstring& url,const std::wstring& code,const std::wstring& name,int seconds,const std::wstring& path){
 VoiceSession voice;voice.gate=0;AutoVoice automatic(voice);automatic.configure({url,code},name,WAVE_MAPPER,WAVE_MAPPER,true);automatic.start();
 ULONGLONG end=GetTickCount64()+seconds*1000;int joins=0,maxPeers=0;bool previous=false;uint64_t seenReceived=0;
 while(GetTickCount64()<end){bool connected=voice.connected;if(connected&&!previous)joins++;previous=connected;if(connected){int n=0;for(auto& member:voice.getMembers())if(member.present)n++;maxPeers=std::max(maxPeers,n);seenReceived=std::max(seenReceived,voice.received.load());}Sleep(50);}
 int attempts=automatic.getAttempts();automatic.stop();std::ofstream report(path.c_str());report<<"{\"joins\":"<<joins<<",\"attempts\":"<<attempts<<",\"maxPeers\":"<<maxPeers<<",\"received\":"<<seenReceived<<",\"decryptErrors\":"<<voice.decryptErrors<<"}";
 return joins>0&&seenReceived>100?0:2;
}
int profileTest(const std::wstring& directory){
 auto secret=wide(hex(randomBytes(16)));RoomProfile expected{L"wss://example.invalid/voice",secret};auto path=directory+L"\\profile-test.bin";
 bool ok=saveProtectedProfile(path,expected);auto actual=readProtectedProfile(path);ok=ok&&actual.server==expected.server&&actual.secret==expected.secret;
 auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,0,OPEN_EXISTING,0,0);if(file!=INVALID_HANDLE_VALUE){unsigned char b=0;DWORD n;WriteFile(file,&b,1,&n,0);CloseHandle(file);}ok=ok&&!readProtectedProfile(path).valid();DeleteFileW(path.c_str());
 std::ofstream report((directory+L"\\profile-test.json").c_str());report<<"{\"passed\":"<<(ok?"true":"false")<<"}";return ok?0:2;
}
int embeddedCheck(const std::wstring& path){
 auto profile=readEmbeddedProfile();bool valid=profile.valid();std::ofstream report(path.c_str());report<<"{\"embeddedRoomValid\":"<<(valid?"true":"false")<<"}";return valid?0:2;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);int result=0;
 try{if(argc>=3&&std::wstring(argv[1])==L"--audio-test")result=audioTest(argv[2]);else if(argc>=5&&std::wstring(argv[1])==L"--process-audio")result=processAudio(argv[2],argv[3],_wtoi(argv[4]));else if(argc>=3&&std::wstring(argv[1])==L"--relay")result=relay::run(_wtoi(argv[2]));else if(argc>=7&&std::wstring(argv[1])==L"--auto-bot")result=autoBot(argv[2],argv[3],argv[4],std::clamp(_wtoi(argv[5]),1,300),argv[6]);else if(argc>=3&&std::wstring(argv[1])==L"--embedded-check")result=embeddedCheck(argv[2]);else if(argc>=3&&std::wstring(argv[1])==L"--profile-test")result=profileTest(argv[2]);else if(argc>=7&&(std::wstring(argv[1])==L"--bot"||std::wstring(argv[1])==L"--hardware-bot"))result=bot(argv[2],argv[3],argv[4],std::clamp(_wtoi(argv[5]),1,300),argv[6],std::wstring(argv[1])==L"--hardware-bot");else if(argc>=3&&std::wstring(argv[1])==L"--self-test")result=selftest(argv[2]);else result=ui::run(instance,
#ifdef BES_UI_DEMO
true
#else
false
#endif
);}catch(...){result=3;}LocalFree(argv);return result;
}
