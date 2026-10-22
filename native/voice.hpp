#pragma once
#include "common.hpp"
#include <winhttp.h>
#include <mmsystem.h>
#include <opus.h>
#include <functional>

constexpr int RATE=16000, SAMPLES=320;
struct Member {bool present=false;std::wstring name;ULONGLONG spoke=0;};
class VoiceSession {
 std::thread network,capture,playback;std::mutex sendMutex,peerMutex,statusMutex;
 HINTERNET ws=0;std::unique_ptr<Crypto> tx,rx;Bytes prefix;uint32_t sequence=0;
 struct Remote {OpusDecoder* decoder=0;std::deque<std::array<short,SAMPLES>> queue;bool primed=false;uint32_t sequence=0;bool received=false;};
 Remote remotes[5];std::array<Member,5> members{};std::wstring status=L"Қосылмаған";
 std::wstring url,code,name;unsigned inputDevice=WAVE_MAPPER,outputDevice=WAVE_MAPPER;bool synthetic=false;
 bool send(const Bytes& b){std::lock_guard<std::mutex> l(sendMutex);if(!running||!ws)return false;return WinHttpWebSocketSend(ws,WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,(void*)b.data(),(DWORD)b.size())==NO_ERROR;}
 void setStatus(const std::wstring& s){std::lock_guard<std::mutex> l(statusMutex);status=s;}
 WAVEFORMATEX format(){WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=1;f.nSamplesPerSec=RATE;f.wBitsPerSample=16;f.nBlockAlign=2;f.nAvgBytesPerSec=RATE*2;return f;}
 void encodeFrame(OpusEncoder* enc,const short* pcm){
   long long energy=0;for(int j=0;j<SAMPLES;j++)energy+=(long long)pcm[j]*pcm[j];int level=(int)std::sqrt(double(energy)/SAMPLES);micLevel=level;
   bool allowed=!muted&&(!pushToTalk||(GetAsyncKeyState(pttKey.load())&0x8000));
   if(!allowed){wasTalking=false;return;}bool talking=level>=gate.load();
   if(talking)lastSpeech=GetTickCount64();if(!talking&&GetTickCount64()-lastSpeech>220){wasTalking=false;return;}
   wasTalking=true;unsigned char compressed[1275];int n=opus_encode(enc,pcm,SAMPLES,compressed,sizeof compressed);if(n<=2)return;
   uint32_t seq=sequence++;if(sequence==0){running=false;return;}Bytes b{0x10};put32(b,seq);Bytes nonce=prefix;put32(nonce,seq);b.insert(b.end(),nonce.begin(),nonce.end());Bytes encoded(compressed,compressed+n),encrypted;unsigned char tag[16];
   if(!tx->crypt(true,encoded,nonce.data(),b.data()+1,encrypted,tag))return;
   b.insert(b.end(),encrypted.begin(),encrypted.end());b.insert(b.end(),tag,tag+16);if(send(b)){sent++;bytesSent+=b.size();}
 }
 void captureLoop(){
   int err=0;auto* enc=opus_encoder_create(RATE,1,OPUS_APPLICATION_VOIP,&err);if(!enc){audioError=true;return;}
   opus_encoder_ctl(enc,OPUS_SET_BITRATE(24000));opus_encoder_ctl(enc,OPUS_SET_COMPLEXITY(2));opus_encoder_ctl(enc,OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));opus_encoder_ctl(enc,OPUS_SET_DTX(synthetic?0:1));opus_encoder_ctl(enc,OPUS_SET_VBR(1));
   if(synthetic){std::array<short,SAMPLES> pcm;uint64_t t=0;auto tick=std::chrono::steady_clock::now();while(running){for(int i=0;i<SAMPLES;i++,t++)pcm[i]=(short)(5000*sin(2*3.141592653589793*440*t/RATE));encodeFrame(enc,pcm.data());tick+=std::chrono::milliseconds(20);std::this_thread::sleep_until(tick);}opus_encoder_destroy(enc);return;}
   HANDLE event=CreateEventW(0,FALSE,FALSE,0);HWAVEIN in=0;auto f=format();MMRESULT mr=waveInOpen(&in,inputDevice,&f,(DWORD_PTR)event,0,CALLBACK_EVENT|WAVE_FORMAT_DIRECT);
   if(mr!=MMSYSERR_NOERROR)mr=waveInOpen(&in,inputDevice,&f,(DWORD_PTR)event,0,CALLBACK_EVENT);
   if(mr!=MMSYSERR_NOERROR){setStatus(L"Микрофон ашылмады. Windows рұқсатын тексеріңіз.");audioError=true;CloseHandle(event);opus_encoder_destroy(enc);return;}
   WAVEHDR headers[8]{};short buffers[8][SAMPLES]{};for(int i=0;i<8;i++){headers[i].lpData=(LPSTR)buffers[i];headers[i].dwBufferLength=sizeof buffers[i];waveInPrepareHeader(in,&headers[i],sizeof(WAVEHDR));waveInAddBuffer(in,&headers[i],sizeof(WAVEHDR));}waveInStart(in);
   while(running){WaitForSingleObject(event,100);for(int i=0;i<8&&running;i++)if(headers[i].dwFlags&WHDR_DONE){if(headers[i].dwBytesRecorded==sizeof buffers[i])encodeFrame(enc,buffers[i]);headers[i].dwBytesRecorded=0;waveInAddBuffer(in,&headers[i],sizeof(WAVEHDR));}}
   waveInStop(in);waveInReset(in);for(auto& h:headers)waveInUnprepareHeader(in,&h,sizeof h);waveInClose(in);CloseHandle(event);opus_encoder_destroy(enc);micLevel=0;wasTalking=false;
 }
 void playbackLoop(){
   HWAVEOUT out=0;auto f=format();MMRESULT mr=MMSYSERR_NOERROR;if(!synthetic)mr=waveOutOpen(&out,outputDevice,&f,0,0,CALLBACK_NULL);if(mr!=MMSYSERR_NOERROR){setStatus(L"Құлаққап ашылмады. Дыбыс құрылғысын таңдаңыз.");audioError=true;return;}
   WAVEHDR headers[4]{};short buffers[4][SAMPLES]{};for(int i=0;i<4;i++){headers[i].lpData=(LPSTR)buffers[i];headers[i].dwBufferLength=sizeof buffers[i];if(out)waveOutPrepareHeader(out,&headers[i],sizeof(WAVEHDR));}
   int index=0;auto tick=std::chrono::steady_clock::now();
   while(running){std::array<int,SAMPLES> mix{};bool any=false;
    {std::lock_guard<std::mutex> l(peerMutex);for(auto& r:remotes){if(r.queue.empty()){r.primed=false;continue;}if(!r.primed){if(r.queue.size()<2)continue;r.primed=true;}auto pcm=r.queue.front();r.queue.pop_front();for(int i=0;i<SAMPLES;i++)mix[i]+=pcm[i];any=true;}}
    auto& h=headers[index];if(synthetic){if(any)mixed++;}else if(!(h.dwFlags&WHDR_INQUEUE)){
      for(int i=0;i<SAMPLES;i++){int v=deafened?0:mix[i]*volume.load()/100;buffers[index][i]=(short)std::clamp(v,-32768,32767);}if(any||outActive){waveOutWrite(out,&h,sizeof h);outActive=any;}index=(index+1)%4;
    }
    tick+=std::chrono::milliseconds(20);auto now=std::chrono::steady_clock::now();if(now-tick>std::chrono::milliseconds(60))tick=now;std::this_thread::sleep_until(tick);
   }
   if(out){waveOutReset(out);for(auto& h:headers)waveOutUnprepareHeader(out,&h,sizeof h);waveOutClose(out);}
 }
 bool outActive=false;ULONGLONG lastSpeech=0;
 void onMessage(const Bytes& b){
   if(b.empty())return;
   if(b[0]==2&&b.size()==2&&b[1]<5){if(connected)return;self=b[1];connected=true;setStatus(L"Бөлмеге қосылдыңыз");capture=std::thread([this]{captureLoop();});playback=std::thread([this]{playbackLoop();});return;}
   if(b[0]==3&&b.size()>=2&&b[1]<=5){std::lock_guard<std::mutex> l(peerMutex);std::array<Member,5> next{};size_t p=2;for(int i=0;i<b[1];i++){if(p+2>b.size())return;int id=b[p++],n=b[p++];if(id>4||p+n>b.size())return;next[id]={true,wide(std::string((char*)b.data()+p,n)),members[id].spoke};p+=n;}for(int i=0;i<5;i++)if(!next[i].present||members[i].name!=next[i].name){auto& r=remotes[i];if(r.decoder)opus_decoder_destroy(r.decoder);r=Remote{};}members=next;return;}
   if(b[0]==0x7f){setStatus(b.size()>1&&std::string((char*)b.data()+1,b.size()-1)=="ROOM_FULL"?L"Бөлме толы: ең көбі 5 адам":L"Сервер қосылуды қабылдамады");running=false;return;}
   if(b[0]==0x21&&b.size()==5){ping=(DWORD)GetTickCount()-get32(b.data()+1);return;}
   if(b[0]!=0x11||b.size()<35||b[1]>4)return;int id=b[1];uint32_t seq=get32(b.data()+2);Bytes encoded(b.begin()+18,b.end()-16),decoded;unsigned char tag[16];memcpy(tag,b.data()+b.size()-16,16);
   if(!rx->crypt(false,encoded,b.data()+6,b.data()+2,decoded,tag)){decryptErrors++;return;}
   std::lock_guard<std::mutex> l(peerMutex);auto& r=remotes[id];if(!members[id].present)return;if(r.received&&seq<=r.sequence)return;r.received=true;r.sequence=seq;
   if(!r.decoder){int err;r.decoder=opus_decoder_create(RATE,1,&err);}if(!r.decoder)return;
   std::array<short,SAMPLES> pcm{};int n=opus_decode(r.decoder,decoded.data(),(int)decoded.size(),pcm.data(),SAMPLES,0);if(n!=SAMPLES)return;
   if(r.queue.size()>=5)r.queue.pop_front();r.queue.push_back(pcm);members[id].spoke=GetTickCount64();received++;bytesReceived+=b.size();
 }
 void networkLoop(){
   HINTERNET session=0,connection=0,request=0;std::wstring why=L"Байланыс үзілді. Қайта қосылып көріңіз.";
   try{
    tx=std::make_unique<Crypto>();rx=std::make_unique<Crypto>();if(!tx->init(utf8(code))||!rx->init(utf8(code)))throw std::runtime_error("crypto");prefix=randomBytes(8);sequence=0;
    std::wstring http=url;if(http.rfind(L"wss://",0)==0)http=L"https://"+http.substr(6);else if(http.rfind(L"ws://",0)==0)http=L"http://"+http.substr(5);else throw std::runtime_error("url");
    URL_COMPONENTS u{};u.dwStructSize=sizeof u;u.dwHostNameLength=(DWORD)-1;u.dwUrlPathLength=(DWORD)-1;if(!WinHttpCrackUrl(http.c_str(),0,0,&u))throw std::runtime_error("url");std::wstring host(u.lpszHostName,u.dwHostNameLength),path(u.lpszUrlPath,u.dwUrlPathLength);if(path.empty()||path==L"/")path=L"/voice";
    if(u.nScheme!=INTERNET_SCHEME_HTTPS&&host!=L"127.0.0.1"&&host!=L"localhost"){why=L"Интернетке қауіпсіз wss:// мекенжайы қажет";throw std::runtime_error("tls required");}
    session=WinHttpOpen(L"BES/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!session)throw std::runtime_error("session");WinHttpSetTimeouts(session,5000,5000,5000,15000);
    connection=WinHttpConnect(session,host.c_str(),u.nPort,0);if(!connection)throw std::runtime_error("connect");request=WinHttpOpenRequest(connection,L"GET",path.c_str(),0,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,u.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0);if(!request)throw std::runtime_error("request");
    WinHttpSetOption(request,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,0,0);
    if(!WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,0,0,0,0)||!WinHttpReceiveResponse(request,0))throw std::runtime_error("handshake");
    HINTERNET socket=WinHttpWebSocketCompleteUpgrade(request,0);if(!socket)throw std::runtime_error("upgrade");
    {std::lock_guard<std::mutex> l(sendMutex);if(running)ws=socket;else WinHttpCloseHandle(socket);}if(!running)throw std::runtime_error("cancel");
    auto room=hash("BES/room/v1/"+utf8(code));auto n=utf8(name);Bytes hello{1,1};hello.insert(hello.end(),room.begin(),room.end());hello.push_back((unsigned char)n.size());hello.insert(hello.end(),n.begin(),n.end());if(!send(hello))throw std::runtime_error("hello");
    unsigned char buf[2048];Bytes packet;
    while(running){DWORD n=0;WINHTTP_WEB_SOCKET_BUFFER_TYPE type;DWORD e=WinHttpWebSocketReceive(socket,buf,sizeof buf,&n,&type);if(e)break;if(type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)break;if(type!=WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE&&type!=WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)break;packet.insert(packet.end(),buf,buf+n);if(packet.size()>2048)break;if(type==WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE){onMessage(packet);packet.clear();}}
   }catch(...){if(!connected&&running)why=L"Серверге қосылмады. Шақыруды тексеріп, қайта көріңіз.";}
   bool wasRunning=running.exchange(false);connected=false;if(wasRunning)setStatus(why);
   if(capture.joinable())capture.join();if(playback.joinable())playback.join();
   {std::lock_guard<std::mutex> l(sendMutex);if(ws){WinHttpWebSocketClose(ws,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,0,0);WinHttpCloseHandle(ws);ws=0;}}
   if(request)WinHttpCloseHandle(request);if(connection)WinHttpCloseHandle(connection);if(session)WinHttpCloseHandle(session);
   {std::lock_guard<std::mutex> l(peerMutex);for(auto& r:remotes){if(r.decoder)opus_decoder_destroy(r.decoder);r=Remote{};}members={};}
 }
public:
 std::atomic<bool> running{false},connected{false},muted{false},deafened{false},pushToTalk{false},wasTalking{false},audioError{false};
 std::atomic<int> pttKey{VK_XBUTTON1},gate{200},volume{100},micLevel{0},self{-1};std::atomic<DWORD> ping{0};
 std::atomic<uint64_t> sent{0},received{0},mixed{0},decryptErrors{0},bytesSent{0},bytesReceived{0};
 ~VoiceSession(){stop();}
 void start(const std::wstring& server,const std::wstring& secret,const std::wstring& nick,unsigned inDev=WAVE_MAPPER,unsigned outDev=WAVE_MAPPER,bool bot=false){stop();url=server;code=secret;name=nick;inputDevice=inDev;outputDevice=outDev;synthetic=bot;self=-1;sent=0;received=0;mixed=0;decryptErrors=0;bytesSent=0;bytesReceived=0;audioError=false;outActive=false;lastSpeech=0;running=true;setStatus(L"Серверге қосылуда…");network=std::thread([this]{networkLoop();});}
 void stop(){running=false;{std::lock_guard<std::mutex> l(sendMutex);if(ws)WinHttpWebSocketShutdown(ws,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,0,0);}if(network.joinable())network.join();connected=false;setStatus(L"Қосылмаған");}
 void heartbeat(){if(connected){Bytes b{0x20};put32(b,(DWORD)GetTickCount());send(b);}}
 std::wstring getStatus(){std::lock_guard<std::mutex> l(statusMutex);return status;}
 std::array<Member,5> getMembers(){std::lock_guard<std::mutex> l(peerMutex);return members;}
};
