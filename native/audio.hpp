#pragma once
#include "common.hpp"
#include <opus.h>
#include <speex/speex_preprocess.h>
#include <xmmintrin.h>

constexpr int RATE=48000, SAMPLES=960, VOICE_BITRATE=64000;
using AudioFrame=std::array<short,SAMPLES>;
// Prevent near-silent floating-point filter state from causing denormal stalls.
struct DspFloatScope {unsigned previous;DspFloatScope():previous(_mm_getcsr()){_mm_setcsr(previous|0x8040);}~DspFloatScope(){_mm_setcsr(previous);}};

inline int audioRms(const short* pcm){double sum=0;for(int i=0;i<SAMPLES;i++)sum+=double(pcm[i])*pcm[i];return int(std::sqrt(sum/SAMPLES));}

class NoiseFilter {
 SpeexPreprocessState* state=nullptr;int mode=-1;float previous=0,high=0;AudioFrame dryDelay{};
 void initialize(){state=speex_preprocess_state_init(SAMPLES,RATE);if(!state)throw std::runtime_error("noise filter");int off=0;speex_preprocess_ctl(state,SPEEX_PREPROCESS_SET_AGC,&off);speex_preprocess_ctl(state,SPEEX_PREPROCESS_SET_VAD,&off);}
public:
 NoiseFilter(){initialize();}
 ~NoiseFilter(){speex_preprocess_state_destroy(state);}
 NoiseFilter(const NoiseFilter&)=delete;
 void process(AudioFrame& pcm,int setting){
  DspFloatScope floatingPoint;
  setting=std::clamp(setting,0,2);
  if(setting!=mode){if(mode==0&&setting!=0){speex_preprocess_state_destroy(state);initialize();dryDelay.fill(0);}int enabled=setting!=0,attenuation=setting==2?-22:-12;speex_preprocess_ctl(state,SPEEX_PREPROCESS_SET_DENOISE,&enabled);speex_preprocess_ctl(state,SPEEX_PREPROCESS_SET_NOISE_SUPPRESS,&attenuation);mode=setting;}
  if(!setting){previous=high=0;dryDelay.fill(0);return;}
  auto dry=pcm;
  for(auto& sample:pcm){float input=sample;high=.9921768f*(high+input-previous);previous=input;sample=(short)std::clamp(high,-32768.f,32767.f);}
  speex_preprocess_run(state,pcm.data());
  // Align the dry path with Speex's 20 ms overlap-add delay to avoid comb filtering.
  float wet=setting==2?.92f:.70f;for(int i=0;i<SAMPLES;i++)pcm[i]=(short)std::clamp(wet*pcm[i]+(1-wet)*dryDelay[i],-32768.f,32767.f);dryDelay=dry;
 }
};

class SpeechGate {
 uint64_t lastSpeech=0;float gain=0;bool open=false;
public:
 bool process(AudioFrame& pcm,int threshold,bool allowed,uint64_t now){
  if(!allowed){gain=0;open=false;pcm.fill(0);return false;}
  int level=audioRms(pcm.data());
  if(threshold==0||level>=(open?threshold*65/100:threshold)){lastSpeech=now;open=true;}
  bool keep=open&&(threshold==0||now-lastSpeech<280);
  float target=keep?1.f:0.f;
  for(auto& sample:pcm){gain+=(target-gain)*(keep?.0067f:.0006f);sample=(short)(sample*gain);}
  if(!keep&&gain<.003f){open=false;gain=0;pcm.fill(0);return false;}
  return true;
 }
};

class MixLimiter {
 float gain=1;
public:
 AudioFrame process(const std::array<int,SAMPLES>& input,int volume){
  double peak=1;for(auto sample:input)peak=std::max(peak,std::abs(double(sample)*volume/100));
  float target=(float)std::min(1.,27000./peak);if(target<gain)gain=target;else gain+=(target-gain)*.025f;
  AudioFrame output{};for(int i=0;i<SAMPLES;i++)output[i]=(short)std::clamp(input[i]*(volume/100.f)*gain,-30000.f,30000.f);return output;
 }
};

class PlayoutBuffer {
 struct Packet{uint32_t sequence;Bytes data;};
 std::deque<Packet> queue;OpusDecoder* decoder=nullptr;
 bool seen=false,primed=false,floorKnown=false;uint32_t latest=0,expected=0,playedFloor=0;uint64_t lastArrival=0,firstArrival=0;
 float variation=0;int missing=0;
public:
 uint64_t concealed=0,dropped=0,recovered=0,outputFrames=0;uint32_t outputSequence=0;size_t peakQueue=0;
 ~PlayoutBuffer(){reset();}
 PlayoutBuffer()=default;PlayoutBuffer(const PlayoutBuffer&)=delete;
 void reset(){if(decoder)opus_decoder_destroy(decoder);decoder=nullptr;queue.clear();seen=primed=floorKnown=false;latest=expected=playedFloor=0;lastArrival=firstArrival=0;variation=0;missing=0;}
 bool push(Bytes data,uint32_t sequence,uint64_t now){
  if(seen&&sequence<=latest)return false;
  if(floorKnown&&sequence<playedFloor){dropped++;return false;}
  if(lastArrival&&now-lastArrival>250){queue.clear();primed=false;missing=0;}
  else if(lastArrival){float delta=std::abs(float(now-lastArrival)-20.f);variation+=.08f*(std::min(delta,80.f)-variation);}
  seen=true;latest=sequence;lastArrival=now;if(queue.empty())firstArrival=now;
  if(queue.size()>=6){queue.pop_front();dropped++;}
  queue.push_back({sequence,std::move(data)});peakQueue=std::max(peakQueue,queue.size());return true;
 }
 bool pull(AudioFrame& output,uint64_t now){
  output.fill(0);int target=variation>16?3:2;
  if(!primed){if(queue.empty()||(queue.size()<size_t(target)&&now-firstArrival<60))return false;expected=queue.front().sequence;primed=true;missing=0;}
  if(!decoder){int error;decoder=opus_decoder_create(RATE,1,&error);if(!decoder)return false;}
  while(queue.size()>size_t(target+1)){queue.pop_front();dropped++;expected=queue.front().sequence;}
  while(!queue.empty()&&queue.front().sequence<expected){queue.pop_front();dropped++;}
  if(!queue.empty()&&queue.front().sequence>expected+3){expected=queue.front().sequence;missing=0;}
  int samples=0;uint32_t playing=expected;
  if(!queue.empty()&&queue.front().sequence==expected){auto packet=std::move(queue.front());queue.pop_front();samples=opus_decode(decoder,packet.data.data(),(int)packet.data.size(),output.data(),SAMPLES,0);expected++;missing=0;}
  else if(!queue.empty()&&queue.front().sequence==expected+1){auto& next=queue.front();samples=opus_decode(decoder,next.data.data(),(int)next.data.size(),output.data(),SAMPLES,1);expected++;missing++;concealed++;recovered++;}
  else if(missing<3){samples=opus_decode(decoder,nullptr,0,output.data(),SAMPLES,0);expected++;missing++;concealed++;float fade=1.f-missing*.23f;for(auto& sample:output)sample=(short)(sample*fade);}
  else {primed=false;return false;}
  if(samples==SAMPLES){outputFrames++;outputSequence=playing;floorKnown=true;playedFloor=playing+1;return true;}return false;
 }
 size_t queued()const{return queue.size();}
};

inline OpusEncoder* createVoiceEncoder(){
 int error=0;auto* encoder=opus_encoder_create(RATE,1,OPUS_APPLICATION_AUDIO,&error);if(!encoder)return nullptr;
 opus_encoder_ctl(encoder,OPUS_SET_BITRATE(VOICE_BITRATE));opus_encoder_ctl(encoder,OPUS_SET_COMPLEXITY(4));opus_encoder_ctl(encoder,OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));opus_encoder_ctl(encoder,OPUS_SET_SIGNAL(OPUS_AUTO));opus_encoder_ctl(encoder,OPUS_SET_DTX(1));opus_encoder_ctl(encoder,OPUS_SET_VBR(1));opus_encoder_ctl(encoder,OPUS_SET_INBAND_FEC(1));opus_encoder_ctl(encoder,OPUS_SET_PACKET_LOSS_PERC(5));return encoder;
}
