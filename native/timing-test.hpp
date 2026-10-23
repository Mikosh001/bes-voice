#pragma once
inline int timingTest(const std::wstring& path){
 DspFloatScope floatingPoint;auto* encoder=createVoiceEncoder();std::vector<Bytes> packets;AudioFrame input,output;unsigned char encoded[1275];
 for(int frame=0;frame<400;frame++){for(int i=0;i<SAMPLES;i++)input[i]=frame>=100&&frame<250?0:(short)(4000*sin((frame*SAMPLES+i)*.041));int n=opus_encode(encoder,input.data(),SAMPLES,encoded,sizeof encoded);if(n<=0)return 2;packets.emplace_back(encoded,encoded+n);}opus_encoder_destroy(encoder);
 struct Result{bool monotonic=true,finished=false;uint64_t concealed=0,dropped=0;size_t peak=0;int maxPause=0;};
 auto simulate=[&](bool disturbed){
  struct Delivery{uint64_t at;uint32_t seq;};std::deque<Delivery> arrivals;uint64_t previous=0;uint32_t random=47;
  for(uint32_t i=0;i<packets.size();i++){if(disturbed&&(i==61||i==62||i==287))continue;random=random*1664525+1013904223;int jitter=int((random>>16)%9)-4;uint64_t when=i*20+40+jitter;if(disturbed&&i>=170&&i<179)when+=180;when=std::max(when,previous);previous=when;arrivals.push_back({when,i});}
  PlayoutBuffer buffer;Result result;bool started=false;uint32_t last=0;int pause=0;
  for(uint64_t now=0;now<9500;now++){
   while(!arrivals.empty()&&arrivals.front().at<=now){auto delivery=arrivals.front();arrivals.pop_front();buffer.push(packets[delivery.seq],delivery.seq,1000+now);}
   if(now%20)continue;
   if(buffer.pull(output,1000+now)){if(started&&buffer.outputSequence<=last)result.monotonic=false;started=true;last=buffer.outputSequence;pause=0;if(last>=399){result.finished=true;break;}}
   else if(started){pause+=20;result.maxPause=std::max(result.maxPause,pause);}
  }
  result.concealed=buffer.concealed;result.dropped=buffer.dropped;result.peak=buffer.peakQueue;return result;
 };
 auto normal=simulate(false),bad=simulate(true);bool passed=normal.finished&&normal.monotonic&&normal.concealed==0&&normal.dropped==0&&bad.finished&&bad.monotonic&&bad.concealed>0&&bad.dropped>0&&bad.peak<=6&&bad.maxPause<=180;
 std::ofstream report(path.c_str());report<<"{\"passed\":"<<(passed?"true":"false")<<",\"normalContinuousSpeechAndSilence\":{\"finished\":"<<(normal.finished?"true":"false")<<",\"concealed\":"<<normal.concealed<<",\"dropped\":"<<normal.dropped<<"},\"packetLossAnd180msStall\":{\"finished\":"<<(bad.finished?"true":"false")<<",\"timelineNeverRewinds\":"<<(bad.monotonic?"true":"false")<<",\"concealed\":"<<bad.concealed<<",\"dropped\":"<<bad.dropped<<",\"peakQueuePackets\":"<<bad.peak<<",\"longestRebufferMs\":"<<bad.maxPause<<"}}";return passed?0:2;
}
