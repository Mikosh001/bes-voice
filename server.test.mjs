import test from 'node:test';
import assert from 'node:assert/strict';
import {randomBytes} from 'node:crypto';
import {WebSocket} from 'ws';
import {createRelay} from './server.mjs';

const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function wait(peer,predicate){
 for(let i=0;i<300;i++){const n=peer.messages.findIndex(predicate);if(n>=0)return peer.messages.splice(n,1)[0];await delay(10);}
 throw new Error('expected message not received');
}
async function connect(url,room,name){
 const ws=new WebSocket(url),messages=[];ws.on('message',data=>messages.push(Buffer.from(data)));
 await new Promise((resolve,reject)=>{ws.once('open',resolve);ws.once('error',reject);});
 const n=Buffer.from(name);ws.send(Buffer.concat([Buffer.from([1,1]),room,Buffer.from([n.length]),n]));return {ws,messages};
}
test('five participants, host departure, room identity and slot reuse',async()=>{
 const room=randomBytes(32),relay=createRelay({roomId:room.toString('hex'),host:'127.0.0.1',port:0});
 const address=await relay.listen(),url=`ws://127.0.0.1:${address.port}/voice`,peers=[];
 try{
  const health=await fetch(`http://127.0.0.1:${address.port}/health`);assert.equal(await health.text(),'ok');
  for(let i=0;i<5;i++){const p=await connect(url,room,'friend'+i);assert.equal((await wait(p,b=>b[0]===2))[1],i);peers.push(p);}
  for(const p of peers)await wait(p,b=>b[0]===3&&b[1]===5);
  const full=await connect(url,room,'sixth');peers.push(full);assert.equal((await wait(full,b=>b[0]===127)).subarray(1).toString(),'ROOM_FULL');
  const wrong=await connect(url,randomBytes(32),'stranger');peers.push(wrong);assert.equal((await wait(wrong,b=>b[0]===127)).subarray(1).toString(),'WRONG_ROOM');
  peers[0].ws.close();for(let i=1;i<5;i++)await wait(peers[i],b=>b[0]===3&&b[1]===4);
  const voice=Buffer.concat([Buffer.from([16,1,0,0,0]),randomBytes(12),randomBytes(60),randomBytes(16)]);peers[1].ws.send(voice);
  for(let i=2;i<5;i++){const b=await wait(peers[i],b=>b[0]===17);assert.equal(b[1],1);assert.deepEqual(b.subarray(2),voice.subarray(1));}
  assert(!peers[1].messages.some(b=>b[0]===17));
  const dtx=Buffer.concat([Buffer.from([16,2,0,0,0]),randomBytes(12),randomBytes(1),randomBytes(16)]);peers[1].ws.send(dtx);
  for(let i=2;i<5;i++){const b=await wait(peers[i],b=>b[0]===17);assert.equal(b.length,35);assert.deepEqual(b.subarray(2),dtx.subarray(1));}
  const next=await connect(url,room,'new friend');peers.push(next);assert.equal((await wait(next,b=>b[0]===2))[1],0);
  next.ws.send(Buffer.from([32,1,2,3,4]));assert.deepEqual(await wait(next,b=>b[0]===33),Buffer.from([33,1,2,3,4]));
 }finally{for(const p of peers)p.ws.terminate();await relay.close();}
});
