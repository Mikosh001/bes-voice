from pathlib import Path
import array, math, random, json, wave, subprocess

root=Path(__file__).resolve().parent
flags=0x08000000
exe=root/'BES.exe' if (root/'BES.exe').exists() else root/'build/BES.exe'
test=subprocess.run([str(exe),'--audio-test',str(root/'audio-results.json')],creationflags=flags,timeout=15)
if test.returncode:
    print((root/'audio-results.json').read_text())
    raise SystemExit(test.returncode)
with wave.open(str(root/'.deps/speech_orig.wav'),'rb') as source:
    assert source.getnchannels()==1 and source.getsampwidth()==2 and source.getframerate()==48000
    original=array.array('h',source.readframes(source.getnframes()))
# Low-pass FIR before integer 48k -> 16k decimation, independent of app DSP.
kernel=[]
for i in range(63):
    x=i-31
    sinc=2*.145 if x==0 else math.sin(2*math.pi*.145*x)/(math.pi*x)
    kernel.append(sinc*(.54-.46*math.cos(2*math.pi*i/62)))
norm=sum(kernel);kernel=[v/norm for v in kernel]
speech=[sum(original[max(0,min(len(original)-1,i+j-31))]*kernel[j] for j in range(63)) for i in range(0,len(original),3)]
speech=[v*.65 for v in speech]
padding=16000*3
reference=[0.]*padding+speech+[0.]*padding
reference=reference[:len(reference)//320*320]
rng=random.Random(835)
noise=[rng.gauss(0,530)+210*math.sin(2*math.pi*100*i/16000) for i in range(len(reference))]
noisy=array.array('h',(int(max(-32768,min(32767,s+n))) for s,n in zip(reference,noise)))
(root/'noise-input.pcm').write_bytes(noisy.tobytes())
metrics={}
for mode in [0,1,2]:
    destination=root/f'noise-mode-{mode}.pcm'
    result=subprocess.run([str(exe),'--process-audio',str(root/'noise-input.pcm'),str(destination),str(mode)],creationflags=flags,timeout=20)
    assert result.returncode==0
    processed=array.array('h');processed.frombytes(destination.read_bytes());assert len(processed)==len(noisy)
    # Filter has overlap-add delay. Estimate it using real, nonstationary speech.
    a=padding+16000;b=min(len(reference)-padding-640,padding+len(speech)-640)
    best=max(range(0,641,16),key=lambda delay:sum(reference[i]*processed[i+delay] for i in range(a,b,8)))
    dot=sum(reference[i]*processed[i+best] for i in range(a,b))
    energy=sum(reference[i]**2 for i in range(a,b));out_energy=sum(processed[i+best]**2 for i in range(a,b))
    correlation=dot/math.sqrt(max(1,energy*out_energy))
    gain=dot/max(1,energy)
    start=16000*2;stop=padding-640
    noise_in=sum(noisy[i]**2 for i in range(start,stop));noise_out=sum(processed[i]**2 for i in range(start,stop))
    attenuation=10*math.log10((noise_in+1)/(noise_out+1))
    metrics[str(mode)]={'stationaryNoiseReductionDb':round(attenuation,2),'speechCorrelation':round(correlation,4),'speechGainDb':round(20*math.log10(max(abs(gain),1e-8)),2),'estimatedFilterDelayMs':best/16}
passed=metrics['1']['stationaryNoiseReductionDb']>8 and metrics['1']['speechCorrelation']>.65 and metrics['1']['speechGainDb']>-9 and metrics['2']['stationaryNoiseReductionDb']>=metrics['1']['stationaryNoiseReductionDb'] and (root/'noise-mode-0.pcm').read_bytes()==noisy.tobytes()
report={'passed':passed,'sampleSource':'https://www.opus-codec.org/examples/ (speech_orig.wav, local evaluation only; not redistributed)','scope':'16 kHz real speech plus deterministic stationary white noise and hum; not a test of all background sounds or actual microphone hardware','modes':metrics,'dspChecks':json.loads((root/'audio-results.json').read_text())}
(root/'speech-filter-results.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
if not passed:raise SystemExit(2)
