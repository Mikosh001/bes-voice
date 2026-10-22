import http from 'node:http';
import {timingSafeEqual} from 'node:crypto';
import {TextDecoder} from 'node:util';
import {pathToFileURL} from 'node:url';
import {WebSocketServer,WebSocket} from 'ws';

export function createRelay({roomId=process.env.BES_ROOM_ID,host='0.0.0.0',port=Number(process.env.PORT||10000)}={}){
 if(!/^[a-f0-9]{64}$/.test(roomId||''))throw new Error('BES_ROOM_ID must be a SHA-256 room identifier');
 const expected=Buffer.from(roomId,'hex');
 const clients=new Set();const slots=new Map();const decoder=new TextDecoder('utf-8',{fatal:true});
 const server=http.createServer((request,response)=>{
  response.setHeader('Cache-Control','no-store');
  if(request.url==='/health'){response.writeHead(200,{'Content-Type':'text/plain'});response.end('ok');return;}
  response.writeHead(404);response.end();
 });
 const wss=new WebSocketServer({noServer:true,maxPayload:1500,perMessageDeflate:false,clientTracking:false});
 function send(ws,data,audio=false){
  if(ws.readyState!==WebSocket.OPEN)return;
  if(ws.bufferedAmount>32768){ws.terminate();return;}
  if(audio&&ws.bufferedAmount>4096)return;
  ws.send(data,{binary:true},error=>{if(error)ws.terminate();});
 }
 function roster(){
  const parts=[Buffer.from([3,slots.size])];
  for(const [id,peer] of slots)parts.push(Buffer.from([id,peer.name.length]),peer.name);
  const packet=Buffer.concat(parts);for(const peer of slots.values())send(peer.ws,packet);
 }
 function reject(ws,message){send(ws,Buffer.concat([Buffer.from([127]),Buffer.from(message)]));ws.close(1008,'Join rejected');}
 server.on('upgrade',(request,socket,head)=>{
  if(request.url!=='/voice'||clients.size>=16){socket.end('HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n');return;}
  wss.handleUpgrade(request,socket,head,ws=>wss.emit('connection',ws,request));
 });
 wss.on('connection',ws=>{
  const peer={ws,id:null,name:null,last:Date.now(),rateAt:Date.now(),rate:0};clients.add(peer);
  const joinTimeout=setTimeout(()=>{if(peer.id===null)ws.terminate();},5000);joinTimeout.unref();
  ws.on('error',()=>{});
  ws.on('close',()=>{clearTimeout(joinTimeout);clients.delete(peer);if(peer.id!==null&&slots.get(peer.id)===peer){slots.delete(peer.id);roster();}});
  ws.on('message',(message,binary)=>{
   if(!binary){ws.close(1003);return;}const b=Buffer.from(message);const now=Date.now();
   if(now-peer.rateAt>1000){peer.rateAt=now;peer.rate=0;}if(++peer.rate>100){ws.close(1008);return;}
   peer.last=now;
   if(peer.id===null){
    if(b.length<35||b[0]!==1||b[1]!==1||b[34]<1||b[34]>80||b.length!==35+b[34]){ws.close(1008);return;}
    if(!timingSafeEqual(b.subarray(2,34),expected)){reject(ws,'WRONG_ROOM');return;}
    const name=b.subarray(35);let text;try{text=decoder.decode(name);}catch{ws.close(1008);return;}
    if(!text||text.length>32||/[\x00-\x1f\x7f]/u.test(text)){ws.close(1008);return;}
    let id=0;while(slots.has(id))id++;if(id>=5){reject(ws,'ROOM_FULL');return;}
    clearTimeout(joinTimeout);peer.id=id;peer.name=name;slots.set(id,peer);send(ws,Buffer.from([2,id]));roster();return;
   }
   if(b[0]===32&&b.length===5){const pong=Buffer.from(b);pong[0]=33;send(ws,pong);return;}
   if(b[0]===16&&b.length>=34&&b.length<=1400){
    const packet=Buffer.concat([Buffer.from([17,peer.id]),b.subarray(1)]);
    for(const other of slots.values())if(other!==peer)send(other.ws,packet,true);return;
   }
   ws.close(1008);
  });
 });
 const sweep=setInterval(()=>{for(const peer of clients)if(Date.now()-peer.last>30000)peer.ws.terminate();},5000);sweep.unref();
 function listen(){return new Promise((resolve,reject)=>{server.once('error',reject);server.listen(port,host,()=>{server.off('error',reject);resolve(server.address());});});}
 async function close(){clearInterval(sweep);for(const peer of clients)peer.ws.terminate();await new Promise(resolve=>server.close(resolve));wss.close();}
 return {server,listen,close,clients,slots};
}

if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href){
 const relay=createRelay();await relay.listen();console.log('BES relay ready');
 for(const signal of ['SIGTERM','SIGINT'])process.once(signal,async()=>{await relay.close();process.exit(0);});
}
