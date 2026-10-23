#pragma once
#include "audio.hpp"
#include <winhttp.h>
#include <mmsystem.h>
#include <avrt.h>
#include <condition_variable>

struct Member {bool present=false;std::wstring name;ULONGLONG spoke=0;};
struct AudioPriority {DspFloatScope floatingPoint;HANDLE task=nullptr;AudioPriority(){DWORD index=0;task=AvSetMmThreadCharacteristicsW(L"Audio",&index);}~AudioPriority(){if(task)AvRevertMmThreadCharacteristics(task);}};
class VoiceSession {
 std::thread network,capture,playback,transmit;std::mutex sendMutex,peerMutex,statusMutex,queueMutex;std::condition_variable sendWake;
 HINTERNET ws=0;std::unique_ptr<Crypto> tx,rx;Bytes prefix;uint32_t sequence=0;std::deque<Bytes> outbound;
 PlayoutBuffer remotes[5];std::array<Member,5> members{};std::wstring status=L"Қосылмаған";
 std::wstring url,code,name;unsigned inputDevice=WAVE_MAPPER,outputDevice=WAVE_MAPPER;bool synthetic=false;
 std::atomic<ULONGLONG> lastReceive{0};
 bool send(const Bytes& b){std::lock_guard<std::mutex> lock(sendMutex);if(!running||!ws)return false;return WinHttpWebSocketSend(ws,WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,(void*)b.data(),(DWORD)b.size())==NO_ERROR;}
 void setStatus(const std::wstring& value){std::lock_guard<std::mutex> lock(statusMutex);status=value;}
 void disconnect(const std::wstring& why){setStatus(why);running=false;sendWake.notify_all();std::lock_guard<std::mutex> lock(sendMutex);if(ws)WinHttpWebSocketShutdown(ws,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,nullptr,0);}
 WAVEFORMATEX format(){WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=1;f.nSamplesPerSec=RATE;f.wBitsPerSample=16;f.nBlockAlign=2;f.nAvgBytesPerSec=RATE*2;return f;}
 void transmitLoop(){
  while(running){Bytes packet;{std::unique_lock<std::mutex> lock(queueMutex);sendWake.wait_for(lock,std::chrono::milliseconds(50),[&]{return !running||!outbound.empty();});if(!running)break;if(outbound.empty())continue;packet=std::move(outbound.front());outbound.pop_front();}
   if(!send(packet)){disconnect(L"Байланыс үзілді. Өзі қайта қосылады.");break;}sent++;bytesSent+=packet.size();
  }
 }
 void encodeFrame(OpusEncoder* encoder,NoiseFilter& filter,SpeechGate& speech,AudioFrame pcm){
  int raw=audioRms(pcm.data());filter.process(pcm,noiseMode.load());micLevel=audioRms(pcm.data());rawMicLevel=raw;
  bool allowed=!muted&&(!pushToTalk||(GetAsyncKeyState(pttKey.load())&0x8000));
  bool active=speech.process(pcm,gate.load(),allowed,GetTickCount64());wasTalking=active&&micLevel.load()>=100;
  // Encode zeroed gated/muted frames too: keep the codec and playout clocks continuous.
  unsigned char compressed[1275];int n=opus_encode(encoder,pcm.data(),SAMPLES,compressed,sizeof compressed);if(n<=0)return;
  uint32_t seq=sequence++;if(sequence==0){disconnect(L"Байланыс жаңартылуда…");return;}
  Bytes packet{0x10};put32(packet,seq);Bytes nonce=prefix;put32(nonce,seq);packet.insert(packet.end(),nonce.begin(),nonce.end());Bytes encoded(compressed,compressed+n),encrypted;unsigned char tag[16];
  if(!tx->crypt(true,encoded,nonce.data(),packet.data()+1,encrypted,tag))return;
  packet.insert(packet.end(),encrypted.begin(),encrypted.end());packet.insert(packet.end(),tag,tag+16);
  {std::lock_guard<std::mutex> lock(queueMutex);if(outbound.size()>=3){outbound.pop_front();sendDropped++;}outbound.push_back(std::move(packet));}sendWake.notify_one();
 }
 void captureLoop(){
  AudioPriority priority;auto* encoder=createVoiceEncoder();if(!encoder){audioError=true;disconnect(L"Дауыс кодегі ашылмады");return;}
  NoiseFilter filter;SpeechGate speech;
  if(synthetic){AudioFrame pcm;uint64_t sample=0;auto tick=std::chrono::steady_clock::now();while(running){for(int i=0;i<SAMPLES;i++,sample++)pcm[i]=(short)(5000*sin(2*3.141592653589793*440*sample/RATE));encodeFrame(encoder,filter,speech,pcm);tick+=std::chrono::milliseconds(20);if(std::chrono::steady_clock::now()-tick>std::chrono::milliseconds(60))tick=std::chrono::steady_clock::now();std::this_thread::sleep_until(tick);}opus_encoder_destroy(encoder);return;}
  HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);HWAVEIN input=nullptr;auto f=format();MMRESULT result=waveInOpen(&input,inputDevice,&f,(DWORD_PTR)event,0,CALLBACK_EVENT|WAVE_FORMAT_DIRECT);
  if(result!=MMSYSERR_NOERROR)result=waveInOpen(&input,inputDevice,&f,(DWORD_PTR)event,0,CALLBACK_EVENT);
  if(result!=MMSYSERR_NOERROR){audioError=true;disconnect(L"Микрофон ашылмады. Құрылғы мен Windows рұқсатын тексеріңіз.");CloseHandle(event);opus_encoder_destroy(encoder);return;}
  WAVEHDR headers[8]{};AudioFrame buffers[8]{};bool ready=true;int prepared=0;
  for(int i=0;i<8;i++){headers[i].lpData=(LPSTR)buffers[i].data();headers[i].dwBufferLength=sizeof(AudioFrame);if(waveInPrepareHeader(input,&headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR){ready=false;break;}prepared++;if(waveInAddBuffer(input,&headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR){ready=false;break;}}
  if(!ready||waveInStart(input)!=MMSYSERR_NOERROR){audioError=true;disconnect(L"Микрофоннан дыбыс оқу мүмкін болмады");}
  int next=0;
  while(running){WaitForSingleObject(event,60);int pending=0;for(auto& header:headers)if(header.dwFlags&WHDR_DONE)pending++;
   for(int visited=0;visited<8&&running;visited++){int i=next;if(!(headers[i].dwFlags&WHDR_DONE))break;next=(next+1)%8;
    if(pending>3){captureDropped++;}else if(headers[i].dwBytesRecorded==sizeof(AudioFrame))encodeFrame(encoder,filter,speech,buffers[i]);pending--;
    headers[i].dwBytesRecorded=0;if(waveInAddBuffer(input,&headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR){audioError=true;disconnect(L"Микрофон ажыратылды. Қайта қосылуда…");}
   }
  }
  waveInStop(input);waveInReset(input);for(int i=0;i<prepared;i++)waveInUnprepareHeader(input,&headers[i],sizeof(WAVEHDR));waveInClose(input);CloseHandle(event);opus_encoder_destroy(encoder);micLevel=rawMicLevel=0;wasTalking=false;
 }
 AudioFrame mixFrame(MixLimiter& limiter){
  std::array<int,SAMPLES> mix{};bool any=false;
  {std::lock_guard<std::mutex> lock(peerMutex);for(int id=0;id<5;id++){AudioFrame pcm;if(remotes[id].pull(pcm,GetTickCount64())){if(audioRms(pcm.data())>=100)members[id].spoke=GetTickCount64();int gain=peerVolume[id];for(int i=0;i<SAMPLES;i++)mix[i]+=pcm[i]*gain/100;any=true;}}}
  if(any)mixed++;return limiter.process(mix,deafened?0:volume.load());
 }
 void playbackLoop(){
  AudioPriority priority;MixLimiter limiter;
  if(synthetic){auto tick=std::chrono::steady_clock::now();while(running){mixFrame(limiter);tick+=std::chrono::milliseconds(20);if(std::chrono::steady_clock::now()-tick>std::chrono::milliseconds(60))tick=std::chrono::steady_clock::now();std::this_thread::sleep_until(tick);}return;}
  HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);HWAVEOUT output=nullptr;auto f=format();
  if(waveOutOpen(&output,outputDevice,&f,(DWORD_PTR)event,0,CALLBACK_EVENT)!=MMSYSERR_NOERROR){audioError=true;disconnect(L"Құлаққап ашылмады. Дыбыс құрылғысын тексеріңіз.");CloseHandle(event);return;}
  WAVEHDR headers[4]{};AudioFrame buffers[4]{};int prepared=0;
  for(int i=0;i<4;i++){headers[i].lpData=(LPSTR)buffers[i].data();headers[i].dwBufferLength=sizeof(AudioFrame);if(waveOutPrepareHeader(output,&headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR){audioError=true;disconnect(L"Дыбыс буфері ашылмады");break;}prepared++;}
  bool started=false;
  while(running){int queued=0;for(auto& header:headers)if(header.dwFlags&WHDR_INQUEUE)queued++;if(started&&queued==0)outputUnderruns++;
   for(int i=0;i<4&&queued<2&&running;i++)if(!(headers[i].dwFlags&WHDR_INQUEUE)){buffers[i]=mixFrame(limiter);if(waveOutWrite(output,&headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR){audioError=true;disconnect(L"Дыбыс құрылғысы ажыратылды. Қайта қосылуда…");break;}queued++;started=true;}
   WaitForSingleObject(event,30);
  }
  waveOutReset(output);for(int i=0;i<prepared;i++)waveOutUnprepareHeader(output,&headers[i],sizeof(WAVEHDR));waveOutClose(output);CloseHandle(event);
 }
 void onMessage(const Bytes& packet){
  if(packet.empty())return;lastReceive=GetTickCount64();
  if(packet[0]==2&&packet.size()==2&&packet[1]<5){if(connected)return;self=packet[1];connected=true;setStatus(L"Бөлмеге қосылдыңыз");transmit=std::thread([this]{transmitLoop();});capture=std::thread([this]{captureLoop();});playback=std::thread([this]{playbackLoop();});return;}
  if(packet[0]==3&&packet.size()>=2&&packet[1]<=5){std::lock_guard<std::mutex> lock(peerMutex);std::array<Member,5> next{};size_t position=2;for(int i=0;i<packet[1];i++){if(position+2>packet.size())return;int id=packet[position++],length=packet[position++];if(id>4||position+length>packet.size())return;next[id]={true,wide(std::string((char*)packet.data()+position,length)),members[id].spoke};position+=length;}if(position!=packet.size())return;for(int i=0;i<5;i++)if(!next[i].present||members[i].name!=next[i].name){remotes[i].reset();peerVolume[i]=100;}members=next;return;}
  if(packet[0]==127){setStatus(packet.size()>1&&std::string((char*)packet.data()+1,packet.size()-1)=="ROOM_FULL"?L"Бөлме толы: ең көбі 5 адам":L"Сервер қосылуды қабылдамады");running=false;return;}
  if(packet[0]==33&&packet.size()==5){ping=(DWORD)GetTickCount()-get32(packet.data()+1);return;}
  if(packet[0]!=17||packet.size()<35||packet[1]>4)return;int id=packet[1];uint32_t seq=get32(packet.data()+2);Bytes encoded(packet.begin()+18,packet.end()-16),decoded;unsigned char tag[16];memcpy(tag,packet.data()+packet.size()-16,16);
  if(!rx->crypt(false,encoded,packet.data()+6,packet.data()+2,decoded,tag)){decryptErrors++;return;}
  std::lock_guard<std::mutex> lock(peerMutex);if(!members[id].present)return;if(remotes[id].push(std::move(decoded),seq,GetTickCount64())){received++;bytesReceived+=packet.size();}
 }
 void networkLoop(){
  HINTERNET session=nullptr,connection=nullptr,request=nullptr;
  try{
   tx=std::make_unique<Crypto>();rx=std::make_unique<Crypto>();if(!tx->init(utf8(code))||!rx->init(utf8(code)))throw std::runtime_error("crypto");prefix=randomBytes(8);sequence=0;
   std::wstring http=url;if(http.rfind(L"wss://",0)==0)http=L"https://"+http.substr(6);else if(http.rfind(L"ws://",0)==0)http=L"http://"+http.substr(5);else throw std::runtime_error("url");
   URL_COMPONENTS parsed{};parsed.dwStructSize=sizeof parsed;parsed.dwHostNameLength=parsed.dwUrlPathLength=(DWORD)-1;if(!WinHttpCrackUrl(http.c_str(),0,0,&parsed))throw std::runtime_error("url");std::wstring host(parsed.lpszHostName,parsed.dwHostNameLength),path(parsed.lpszUrlPath,parsed.dwUrlPathLength);if(path.empty()||path==L"/")path=L"/voice";
   if(parsed.nScheme!=INTERNET_SCHEME_HTTPS&&host!=L"127.0.0.1")throw std::runtime_error("secure connection required");
   session=WinHttpOpen(L"BES/0.4",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!session)throw std::runtime_error("session");WinHttpSetTimeouts(session,5000,5000,4000,12000);
   connection=WinHttpConnect(session,host.c_str(),parsed.nPort,0);if(!connection)throw std::runtime_error("connect");request=WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,parsed.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0);if(!request)throw std::runtime_error("request");
   WinHttpSetOption(request,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,nullptr,0);if(!WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,nullptr,0,0,0)||!WinHttpReceiveResponse(request,nullptr))throw std::runtime_error("handshake");
   HINTERNET socket=WinHttpWebSocketCompleteUpgrade(request,0);if(!socket)throw std::runtime_error("upgrade");DWORD timeout=2000;WinHttpSetOption(socket,WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT,&timeout,sizeof timeout);
   {std::lock_guard<std::mutex> lock(sendMutex);if(running)ws=socket;else WinHttpCloseHandle(socket);}if(!running)throw std::runtime_error("cancel");
   auto room=hash("BES/room/v1/"+utf8(code));auto nickname=utf8(name);Bytes hello{1,1};hello.insert(hello.end(),room.begin(),room.end());hello.push_back((unsigned char)nickname.size());hello.insert(hello.end(),nickname.begin(),nickname.end());if(!send(hello))throw std::runtime_error("hello");
   unsigned char buffer[2048];Bytes packet;
   while(running){DWORD length=0;WINHTTP_WEB_SOCKET_BUFFER_TYPE type;DWORD error=WinHttpWebSocketReceive(socket,buffer,sizeof buffer,&length,&type);if(error||type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)break;if(type!=WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE&&type!=WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)break;packet.insert(packet.end(),buffer,buffer+length);if(packet.size()>2048)break;if(type==WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE){onMessage(packet);packet.clear();}}
  }catch(...){if(running)setStatus(L"Серверге қосылмады. Өзі қайта қосылады.");}
  bool active=running.exchange(false);connected=false;if(active)setStatus(L"Байланыс жаңартылуда…");sendWake.notify_all();
  if(capture.joinable())capture.join();if(playback.joinable())playback.join();if(transmit.joinable())transmit.join();
  {std::lock_guard<std::mutex> lock(sendMutex);if(ws){WinHttpWebSocketClose(ws,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,nullptr,0);WinHttpCloseHandle(ws);ws=nullptr;}}
  if(request)WinHttpCloseHandle(request);if(connection)WinHttpCloseHandle(connection);if(session)WinHttpCloseHandle(session);
  {std::lock_guard<std::mutex> lock(peerMutex);for(auto& remote:remotes)remote.reset();members={};}
 }
public:
 std::atomic<bool> running{false},connected{false},muted{false},deafened{false},pushToTalk{false},wasTalking{false},audioError{false};
 std::atomic<int> pttKey{VK_XBUTTON1},gate{0},volume{100},noiseMode{0},micLevel{0},rawMicLevel{0},self{-1};std::atomic<int> peerVolume[5]{{100},{100},{100},{100},{100}};std::atomic<DWORD> ping{0};
 std::atomic<uint64_t> sent{0},received{0},mixed{0},decryptErrors{0},bytesSent{0},bytesReceived{0},sendDropped{0},captureDropped{0},outputUnderruns{0};
 ~VoiceSession(){stop();}
 void start(const std::wstring& server,const std::wstring& secret,const std::wstring& nick,unsigned inDev=WAVE_MAPPER,unsigned outDev=WAVE_MAPPER,bool bot=false){stop();url=server;code=secret;name=nick;inputDevice=inDev;outputDevice=outDev;synthetic=bot;self=-1;sent=received=mixed=decryptErrors=bytesSent=bytesReceived=sendDropped=captureDropped=outputUnderruns=0;audioError=false;micLevel=rawMicLevel=0;lastReceive=GetTickCount64();{std::lock_guard<std::mutex> lock(queueMutex);outbound.clear();}running=true;setStatus(L"Серверге қосылуда…");network=std::thread([this]{networkLoop();});}
 void stop(){running=false;sendWake.notify_all();{std::lock_guard<std::mutex> lock(sendMutex);if(ws)WinHttpWebSocketShutdown(ws,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,nullptr,0);}if(network.joinable())network.join();connected=false;}
 void heartbeat(){if(connected){if(GetTickCount64()-lastReceive>12000){disconnect(L"Сервер жауап бермеді. Қайта қосылуда…");return;}Bytes packet{32};put32(packet,(DWORD)GetTickCount());if(!send(packet))disconnect(L"Байланыс жаңартылуда…");}}
 std::wstring getStatus(){std::lock_guard<std::mutex> lock(statusMutex);return status;}
 std::array<Member,5> getMembers(){std::lock_guard<std::mutex> lock(peerMutex);return members;}
};
