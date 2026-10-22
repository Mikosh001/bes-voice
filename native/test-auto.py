from pathlib import Path
import subprocess,os,secrets,hashlib,time,json

root=Path(__file__).resolve().parent
server=root.parent
exe=root/'build'/'BES.exe'
flags=0x08000000
code=secrets.token_hex(16)
env=dict(os.environ,PORT='41912',BES_ROOM_ID=hashlib.sha256(('BES/room/v1/'+code).encode()).hexdigest())
def relay():return subprocess.Popen(['node',str(server/'server.mjs')],env=env,creationflags=flags,stdout=subprocess.DEVNULL)
def bot(name,seconds):return subprocess.Popen([str(exe),'--auto-bot','ws://127.0.0.1:41912/voice',code,name,str(seconds),str(root/(name+'.json'))],creationflags=flags)
r=relay();bots=[]
try:
    time.sleep(.4)
    bots=[bot('auto'+str(i),18) for i in range(5)]
    time.sleep(4)
    bots[0].terminate();bots[0].wait()
    time.sleep(3)
    r.terminate();r.wait()
    time.sleep(2)
    r=relay()
    exits=[p.wait(timeout=35) for p in bots[1:]]
    reports=[json.loads((root/f'auto{i}.json').read_text()) for i in range(1,5)]
    result={'passed':all(c==0 for c in exits) and all(x['joins']>=2 and x['attempts']>=2 and x['maxPeers']==5 and x['received']>300 and x['decryptErrors']==0 for x in reports),'scenario':'Five clients connect automatically; first leaves; four continue; server stops for two seconds; remaining four reconnect without buttons','clients':reports,'exitCodes':exits}
    (root/'auto-results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2),flush=True)
finally:
    for p in bots:
        if p.poll() is None:p.terminate();p.wait()
    if r.poll() is None:r.terminate();r.wait()
