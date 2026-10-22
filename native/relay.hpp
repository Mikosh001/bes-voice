#pragma once
#include "common.hpp"

// One nonblocking event loop. No decoded audio or recordings are stored here.
namespace relay {
struct Peer {SOCKET fd=INVALID_SOCKET;Bytes in,out;bool upgraded=false,joined=false,dead=false;std::string room,name;int id=-1;ULONGLONG born=GetTickCount64(),last=born,rateAt=born;int rate=0;};
inline Bytes frame(const Bytes& p,int op=2){Bytes b{(unsigned char)(0x80|op)};if(p.size()<126)b.push_back((unsigned char)p.size());else{b.push_back(126);b.push_back((unsigned char)(p.size()>>8));b.push_back((unsigned char)p.size());}b.insert(b.end(),p.begin(),p.end());return b;}
inline void send(Peer& p,const Bytes& data,bool audio=false){if(p.dead)return;if(audio&&p.out.size()>4096)return;if(p.out.size()>32768){p.dead=true;return;}auto f=frame(data);p.out.insert(p.out.end(),f.begin(),f.end());}
inline void error(Peer& p,const std::string& s){Bytes b{0x7f};b.insert(b.end(),s.begin(),s.end());send(p,b);p.joined=false;}
inline void roster(std::vector<std::unique_ptr<Peer>>& peers,const std::string& room){
 Bytes b{3,0};for(auto& p:peers)if(p->joined&&!p->dead&&p->room==room){b[1]++;b.push_back((unsigned char)p->id);b.push_back((unsigned char)p->name.size());b.insert(b.end(),p->name.begin(),p->name.end());}
 for(auto& p:peers)if(p->joined&&!p->dead&&p->room==room)send(*p,b);
}
inline std::string lower(std::string s){for(auto& c:s)c=(char)tolower((unsigned char)c);return s;}
inline void message(Peer& p,const Bytes& b,std::vector<std::unique_ptr<Peer>>& peers){
 if(b.empty()){p.dead=true;return;}
 auto now=GetTickCount64();if(now-p.rateAt>1000){p.rateAt=now;p.rate=0;}if(++p.rate>100){p.dead=true;return;}
 if(!p.joined){
  if(b.size()<35||b[0]!=1||b[1]!=1||b[34]<1||b[34]>80||b.size()!=size_t(35+b[34])){p.dead=true;return;}
  std::string name((char*)b.data()+35,b[34]);auto wn=wide(name);if(wn.empty()||wn.size()>32){p.dead=true;return;}
  for(auto c:wn)if(c<32||c==127){p.dead=true;return;}
  std::string room((char*)b.data()+2,32);bool used[5]={};int total=0;
  for(auto& q:peers)if(q->joined&&!q->dead){total++;if(q->room==room)used[q->id]=true;}
  int id=0;while(id<5&&used[id])id++;
  if(id==5||total>=20){error(p,"ROOM_FULL");return;}
  p.joined=true;p.room=room;p.id=id;p.name=name;send(p,Bytes{2,(unsigned char)id});roster(peers,room);return;
 }
 if(b[0]==0x20&&b.size()==5){auto pong=b;pong[0]=0x21;send(p,pong);return;}
 if(b[0]==0x10&&b.size()>=34&&b.size()<=1400){
   Bytes out{0x11,(unsigned char)p.id};out.insert(out.end(),b.begin()+1,b.end());
   for(auto& q:peers)if(q.get()!=&p&&q->joined&&!q->dead&&q->room==p.room)send(*q,out,true);return;
 }
 p.dead=true;
}
inline void parse(Peer& p,std::vector<std::unique_ptr<Peer>>& peers){
 if(!p.upgraded){
  if(p.in.size()>8192){p.dead=true;return;}
  std::string req(p.in.begin(),p.in.end());auto end=req.find("\r\n\r\n");if(end==std::string::npos)return;
  if(req.rfind("GET /health ",0)==0){std::string r="HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";::send(p.fd,r.data(),(int)r.size(),0);p.dead=true;return;}
  std::string l=lower(req);auto k=l.find("\r\nsec-websocket-key:");
  if(req.rfind("GET /voice ",0)!=0||k==std::string::npos||l.find("upgrade: websocket")==std::string::npos||l.find("sec-websocket-version: 13")==std::string::npos){p.dead=true;return;}
  k+=20;while(k<req.size()&&req[k]==' ')k++;auto ke=req.find("\r\n",k);auto key=req.substr(k,ke-k);if(key.size()!=24){p.dead=true;return;}
  auto sha=hash(key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11",BCRYPT_SHA1_ALGORITHM);DWORD n=0;CryptBinaryToStringA(sha.data(),(DWORD)sha.size(),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,0,&n);std::string accept(n,0);CryptBinaryToStringA(sha.data(),(DWORD)sha.size(),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,accept.data(),&n);accept.resize(n);
  std::string response="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+accept+"\r\n\r\n";p.out.insert(p.out.end(),response.begin(),response.end());p.upgraded=true;p.in.erase(p.in.begin(),p.in.begin()+end+4);
 }
 while(p.in.size()>=2&&!p.dead){
   auto* d=p.in.data();if(!(d[0]&0x80)||(d[0]&0x70)||!(d[1]&0x80)){p.dead=true;return;}
   int op=d[0]&15;size_t n=d[1]&127,h=2;if(n==126){if(p.in.size()<4)return;n=(d[2]<<8)|d[3];h=4;}else if(n==127){p.dead=true;return;}
   if(n>1500||(op>=8&&n>125)){p.dead=true;return;}if(p.in.size()<h+4+n)return;
   Bytes b(n);for(size_t i=0;i<n;i++)b[i]=d[h+4+i]^d[h+i%4];p.in.erase(p.in.begin(),p.in.begin()+h+4+n);p.last=GetTickCount64();
   if(op==8){p.dead=true;return;}if(op==9){auto pong=frame(b,10);p.out.insert(p.out.end(),pong.begin(),pong.end());continue;}if(op==10)continue;if(op!=2){p.dead=true;return;}message(p,b,peers);
 }
}
inline int run(int port){
 WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w))return 1;SOCKET server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons((u_short)port);
 if(bind(server,(sockaddr*)&addr,sizeof addr)||listen(server,16)){closesocket(server);WSACleanup();return 2;}u_long nonblock=1;ioctlsocket(server,FIONBIO,&nonblock);std::vector<std::unique_ptr<Peer>> peers;logLine("BES_RELAY_READY "+std::to_string(port));
 for(;;){fd_set rd,wr;FD_ZERO(&rd);FD_ZERO(&wr);FD_SET(server,&rd);for(auto& p:peers){FD_SET(p->fd,&rd);if(!p->out.empty())FD_SET(p->fd,&wr);}timeval tv{0,20000};if(select(0,&rd,&wr,0,&tv)==SOCKET_ERROR)break;
  if(FD_ISSET(server,&rd)){SOCKET s=accept(server,0,0);if(s!=INVALID_SOCKET){if(peers.size()>=24)closesocket(s);else{ioctlsocket(s,FIONBIO,&nonblock);BOOL yes=TRUE;setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(char*)&yes,sizeof yes);auto p=std::make_unique<Peer>();p->fd=s;peers.push_back(std::move(p));}}}
  for(auto& p:peers){if(FD_ISSET(p->fd,&rd)){unsigned char buf[4096];int n=recv(p->fd,(char*)buf,sizeof buf,0);if(n<=0)p->dead=true;else{p->in.insert(p->in.end(),buf,buf+n);if(p->in.size()>16384)p->dead=true;else parse(*p,peers);}}
   if(!p->dead&&!p->out.empty()&&FD_ISSET(p->fd,&wr)){int n=::send(p->fd,(char*)p->out.data(),(int)p->out.size(),0);if(n>0)p->out.erase(p->out.begin(),p->out.begin()+n);else if(WSAGetLastError()!=WSAEWOULDBLOCK)p->dead=true;}
   auto now=GetTickCount64();if((!p->joined&&now-p->born>5000)||(p->joined&&now-p->last>30000))p->dead=true;
  }
  std::vector<std::string> changed;for(auto& p:peers)if(p->dead){if(p->joined)changed.push_back(p->room);closesocket(p->fd);}
  peers.erase(std::remove_if(peers.begin(),peers.end(),[](auto& p){return p->dead;}),peers.end());for(auto& room:changed)roster(peers,room);
 }for(auto& p:peers)closesocket(p->fd);closesocket(server);WSACleanup();return 0;
}
}
