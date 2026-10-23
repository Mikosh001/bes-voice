from pathlib import Path
import array,math,random,json,wave,subprocess

root=Path(__file__).resolve().parent
exe=root/'BES.exe' if (root/'BES.exe').exists() else root/'build/BES.exe'
flags=0x08000000
def run(option,*args):
    p=subprocess.run([str(exe),option,*map(str,args)],creationflags=flags,timeout=40)
    if p.returncode:raise RuntimeError(f'{option} failed: {p.returncode}')
run('--audio-test',root/'audio-results.json')
with wave.open(str(root/'.deps/speech_orig.wav'),'rb') as source:
    assert source.getnchannels()==1 and source.getsampwidth()==2 and source.getframerate()==48000
    speech=array.array('h');speech.frombytes(source.readframes(source.getnframes()))
rate=48000;frame=960
reference=[0.]* (rate*3)+[s*.65 for s in speech]+[0.]*(rate*3)
reference=reference[:len(reference)//frame*frame]
rng=random.Random(835)
noisy=array.array('h',(int(max(-32768,min(32767,s+rng.gauss(0,530)+210*math.sin(2*math.pi*100*i/rate)))) for i,s in enumerate(reference)))
clean=array.array('h',map(int,reference))
(root/'natural-reference.pcm').write_bytes(clean.tobytes());(root/'noise-input-48.pcm').write_bytes(noisy.tobytes())

def compare(original,processed,start,end,maxdelay):
    delay=max(range(0,maxdelay+1,24),key=lambda d:sum(original[i]*processed[i+d] for i in range(start,end,48)))
    dot=sum(original[i]*processed[i+delay] for i in range(start,end));power=sum(original[i]**2 for i in range(start,end));out=sum(processed[i+delay]**2 for i in range(start,end))
    return {'delayMs':round(delay/48,2),'correlation':round(dot/math.sqrt(max(1,power*out)),5),'gainDb':round(20*math.log10(max(1e-9,abs(dot/max(1,power)))),2)}
start=rate*4;end=len(reference)-rate*3-2400
results={}
for mode in [0,1,2]:
    dst=root/f'noise04-{mode}.pcm';run('--process-audio',root/'noise-input-48.pcm',dst,mode)
    data=array.array('h');data.frombytes(dst.read_bytes());assert len(data)==len(noisy)
    score=compare(reference,data,start,end,1200)
    noise_start=rate*2;noise_end=rate*3-2400
    before=sum(noisy[i]**2 for i in range(noise_start,noise_end));after=sum(data[i]**2 for i in range(noise_start,noise_end))
    score['stationaryNoiseReductionDb']=round(10*math.log10((before+1)/(after+1)),2);results[str(mode)]=score
    if mode==0:assert data==noisy

run('--roundtrip-audio',root/'natural-reference.pcm',root/'codec-roundtrip-48.pcm',0)
decoded=array.array('h');decoded.frombytes((root/'codec-roundtrip-48.pcm').read_bytes())
codec=compare(reference,decoded,start,end,1200)
tone=array.array('h',(int(4000*math.sin(2*math.pi*1000*i/rate)+4000*math.sin(2*math.pi*12000*i/rate)) for i in range(rate*2)))
(root/'fullband-probe.pcm').write_bytes(tone.tobytes());run('--roundtrip-audio',root/'fullband-probe.pcm',root/'fullband-probe-decoded.pcm',0)
probe=array.array('h');probe.frombytes((root/'fullband-probe-decoded.pcm').read_bytes())
n=rate
amplitude12=2*math.hypot(sum(probe[i]*math.sin(2*math.pi*12000*i/rate) for i in range(n,2*n)),sum(probe[i]*math.cos(2*math.pi*12000*i/rate) for i in range(n,2*n)))/n
passed=(codec['correlation']>.96 and codec['gainDb']>-1.5 and amplitude12>2000 and results['1']['stationaryNoiseReductionDb']>3 and results['1']['correlation']>.65 and results['2']['stationaryNoiseReductionDb']>results['1']['stationaryNoiseReductionDb'])
result={'passed':passed,'sampleRate':rate,'frameSamples':frame,'targetBitrate':64000,'defaultProfile':'Natural: filter and gate bypass; Opus AUDIO fullband; continuous 20ms packets including silence', 'cleanSpeechCodecRoundtrip':codec,'twelveKHzProbeAmplitude':round(amplitude12,2),'noiseModes':results,'dsp':json.loads((root/'audio-results.json').read_text()),'scope':'Offline public speech sample and generated probes, not a hardware microphone or perceptual listening test','sampleSource':'https://www.opus-codec.org/examples/; original audio not redistributed'}
(root/'voice-quality-results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2),flush=True)
if not passed:raise SystemExit(2)
