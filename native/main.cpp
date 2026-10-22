#include "voice.hpp"
#include "relay.hpp"
#include "auto.hpp"
#include <shellapi.h>
#include <commctrl.h>
#include <psapi.h>
#include <fstream>
#include <sstream>

namespace ui {
constexpr COLORREF BG=RGB(13,18,28),PANEL=RGB(20,27,40),CARD=RGB(25,34,48),INK=RGB(231,238,246),MUTED=RGB(138,155,177),ACCENT=RGB(78,224,183),LINE=RGB(41,55,72),RED=RGB(255,120,128);
enum {NAME=101,INVITE,CONNECT,HOST,COPY,MIC,OUTPUT,PTT,KEY,GATE,VOLUME,MUTE,DEAFEN,TRAY=201};
HWND window=0,nameEdit=0,inviteEdit=0,connectBtn=0,hostBtn=0,copyBtn=0,micCombo=0,outCombo=0,pttBtn=0,keyCombo=0,gateCombo=0,volCombo=0,muteBtn=0,deafenBtn=0;
VoiceSession voice;AutoVoice automatic(voice);RoomProfile room;float scale=1;HFONT normal=0,small=0,bold=0,title=0,brand=0;HBRUSH bgBrush=0,editBrush=0;bool preview=false,tray=false;std::wstring previousInvite,lastStatus;int ticks=0;std::array<Member,5> current{};
int sx(int v){return (int)std::round(v*scale);}RECT rect(int x,int y,int w,int h){return {sx(x),sx(y),sx(x+w),sx(y+h)};}
HFONT font(int size,int weight=FW_NORMAL){return CreateFontW(-sx(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");}
void box(HDC dc,int x,int y,int w,int h,COLORREF color,int radius=12,COLORREF border=0){auto r=rect(x,y,w,h);auto b=CreateSolidBrush(color);auto p=CreatePen(PS_SOLID,sx(1),border?border:color);auto ob=SelectObject(dc,b),op=SelectObject(dc,p);RoundRect(dc,r.left,r.top,r.right,r.bottom,sx(radius),sx(radius));SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(b);DeleteObject(p);}
void text(HDC dc,const std::wstring& s,int x,int y,int w,int h,HFONT f,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){auto r=rect(x,y,w,h);SelectObject(dc,f);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,s.c_str(),(int)s.size(),&r,flags);}
std::wstring read(HWND w){int n=GetWindowTextLengthW(w);std::wstring s(n+1,0);GetWindowTextW(w,s.data(),n+1);s.resize(n);return s;}
void write(HWND w,const std::wstring& s){SetWindowTextW(w,s.c_str());}
HWND control(LPCWSTR cls,const std::wstring& label,int id,int x,int y,int w,int h,DWORD style){auto a=CreateWindowExW(0,cls,label.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,sx(x),sx(y),sx(w),sx(h),window,(HMENU)(INT_PTR)id,GetModuleHandleW(0),0);SendMessageW(a,WM_SETFONT,(WPARAM)normal,TRUE);return a;}
HWND button(const std::wstring& label,int id,int x,int y,int w,int h){return control(L"BUTTON",label,id,x,y,w,h,BS_OWNERDRAW);}
HWND combo(int id,int x,int y,int w){return control(L"COMBOBOX",L"",id,x,y,w,230,CBS_DROPDOWNLIST|WS_VSCROLL);}
void add(HWND c,const std::wstring& name){SendMessageW(c,CB_ADDSTRING,0,(LPARAM)name.c_str());}
int selected(HWND c){return (int)SendMessageW(c,CB_GETCURSEL,0,0);}
void select(HWND c,int i){SendMessageW(c,CB_SETCURSEL,i,0);}
std::wstring iniPath(){wchar_t path[32768];auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);std::wstring d=n?std::wstring(path):executableDir();d+=L"\\BES";CreateDirectoryW(d.c_str(),0);return d+L"\\settings.ini";}
void save(){auto p=iniPath();WritePrivateProfileStringW(L"BES",L"name",read(nameEdit).c_str(),p.c_str());WritePrivateProfileStringW(L"BES",L"ptt",voice.pushToTalk?L"1":L"0",p.c_str());WritePrivateProfileStringW(L"BES",L"key",std::to_wstring(selected(keyCombo)).c_str(),p.c_str());WritePrivateProfileStringW(L"BES",L"gate",std::to_wstring(selected(gateCombo)).c_str(),p.c_str());WritePrivateProfileStringW(L"BES",L"volume",std::to_wstring(selected(volCombo)).c_str(),p.c_str());}
void copy(const std::wstring& s){if(s.empty())return;if(OpenClipboard(window)){EmptyClipboard();auto m=GlobalAlloc(GMEM_MOVEABLE,(s.size()+1)*sizeof(wchar_t));if(m){auto p=GlobalLock(m);memcpy(p,s.c_str(),(s.size()+1)*sizeof(wchar_t));GlobalUnlock(m);if(!SetClipboardData(CF_UNICODETEXT,m))GlobalFree(m);}CloseClipboard();write(copyBtn,L"Көшірілді ✓");}}
void showTray(bool show){NOTIFYICONDATAW n{};n.cbSize=sizeof n;n.hWnd=window;n.uID=1;n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;n.uCallbackMessage=WM_APP+1;n.hIcon=LoadIconW(GetModuleHandleW(0),MAKEINTRESOURCEW(1));wcscpy_s(n.szTip,L"BES — дауыстық чат");if(show&&!tray){Shell_NotifyIconW(NIM_ADD,&n);tray=true;}if(!show&&tray){Shell_NotifyIconW(NIM_DELETE,&n);tray=false;}}
void syncSettings(){int keys[]={VK_XBUTTON1,VK_XBUTTON2,VK_LMENU,'V'};int gates[]={0,120,200,450,900};int volumes[]={25,50,75,100,125,150};voice.pttKey=keys[std::clamp(selected(keyCombo),0,3)];voice.gate=gates[std::clamp(selected(gateCombo),0,4)];voice.volume=volumes[std::clamp(selected(volCombo),0,5)];save();}
std::wstring roomStore(){auto path=iniPath();return path.substr(0,path.find_last_of(L"\\"))+L"\\room.bin";}
void loadRoom(){
 room=readPortableProfile(executableDir()+L"\\BES-room.ini");
 if(!room.valid())room=readEmbeddedProfile();
 if(room.valid()){if(!preview)saveProtectedProfile(roomStore(),room);}
 else room=readProtectedProfile(roomStore());
}
void applyConnection(){
 if(!room.valid())return;
 auto nickname=read(nameEdit);if(nickname.empty()||nickname.size()>32||utf8(nickname).size()>80)return;
 for(auto c:nickname)if(c<32||c==127)return;
 unsigned in=selected(micCombo)>0?selected(micCombo)-1:WAVE_MAPPER;
 unsigned out=selected(outCombo)>0?selected(outCombo)-1:WAVE_MAPPER;
 save();automatic.configure(room,nickname,in,out);
}
void connect(){
 if(!room.valid()){MessageBoxW(window,L"Бұл жинақта сервер баптауы жоқ. BES-room.ini файлы бар жинақты пайдаланыңыз.",L"Бөлме баптауы қажет",MB_OK|MB_ICONINFORMATION);return;}
 if(automatic.isEnabled())automatic.setEnabled(false);else applyConnection();
 write(connectBtn,automatic.isEnabled()?L"Байланысты тоқтату":L"Автоматты қосылу");
 InvalidateRect(window,0,FALSE);
}
void draw(HDC dc){RECT all{};GetClientRect(window,&all);FillRect(dc,&all,bgBrush);auto left=rect(0,0,300,760);auto b=CreateSolidBrush(PANEL);FillRect(dc,&left,b);DeleteObject(b);
 box(dc,28,28,46,46,ACCENT,14);for(int i=0;i<3;i++)box(dc,40+i*9,47-(i==1?6:0),4,i==1?20:8,BG,4);
 text(dc,L"BES",86,23,170,38,brand,INK);text(dc,L"Ойынға жеңіл дауыс",29,86,245,22,small,MUTED);
 text(dc,L"СІЗДІҢ АТЫҢЫЗ",28,129,244,20,small,MUTED);text(dc,L"БӨЛМЕ",28,218,244,20,small,MUTED);
 box(dc,28,247,244,40,CARD,8);
 text(dc,room.valid()?L"Жеке бөлме · автоматты":L"Сервер баптауы қажет",40,248,220,38,normal,room.valid()?ACCENT:MUTED);
 text(dc,L"Қосымшаны ашыңыз — бөлмеге өзі қосылады.\n\nИнтернет үзілсе, байланысты өзі қалпына келтіреді.",28,384,244,160,normal,MUTED,DT_LEFT|DT_WORDBREAK);
 box(dc,28,590,244,94,BG,12);text(dc,L"5 адам · тек дауыс",44,603,210,24,bold,INK);
 text(dc,L"Бөлме бөлек серверде.\nСіз шықсаңыз да жабылмайды.",44,633,210,40,small,MUTED,DT_LEFT|DT_WORDBREAK);
 text(dc,L"v0.2 · автоматты байланыс",28,717,250,20,small,MUTED);
 text(dc,L"Бір бөлме. Бес дос.",334,27,480,45,title,INK);text(dc,L"Ойынға кір. Достарыңмен байланыста бол.",334,77,570,25,normal,MUTED);
 int count=0;for(auto& m:current)if(m.present)count++;text(dc,std::to_wstring(count)+L" / 5",884,34,106,32,bold,ACCENT,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
 auto status=preview?L"Ашылғанда өзі қосылады":(room.valid()?automatic.status():L"Сервер баптауы күтілуде");text(dc,status,334,118,530,22,small,voice.connected?ACCENT:MUTED);
 text(dc,voice.connected?std::to_wstring(voice.ping.load())+L" мс":L"КҮТУ РЕЖИМІ",862,118,128,22,small,MUTED,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
 for(int i=0;i<5;i++){int y=154+i*61;auto& m=current[i];bool self=voice.self==i,talking=m.present&&(self?voice.wasTalking.load():GetTickCount64()-m.spoke<350);box(dc,334,y,656,51,talking?RGB(25,51,48):CARD,12,talking?RGB(47,114,97):CARD);box(dc,347,y+9,34,34,m.present?RGB(38,63,68):RGB(36,47,63),34);text(dc,m.present?m.name.substr(0,1):std::to_wstring(i+1),347,y+9,34,34,bold,m.present?ACCENT:MUTED,DT_CENTER|DT_VCENTER|DT_SINGLELINE);text(dc,m.present?m.name:L"Орын бос",394,y+6,365,38,normal,m.present?INK:MUTED);
  text(dc,!m.present?L"ДОСЫҢЫЗДЫ ШАҚЫРЫҢЫЗ":(self?(voice.muted?L"МИКРОФОН ӨШІК":L"СІЗ"):(talking?L"СӨЙЛЕП ТҰР":L"БӨЛМЕДЕ")),746,y+7,226,36,small,talking?ACCENT:MUTED,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
 }
 text(dc,L"МИКРОФОН",334,484,308,20,small,MUTED);text(dc,L"ҚҰЛАҚҚАП / ДИНАМИК",662,484,328,20,small,MUTED);
 text(dc,L"СӨЙЛЕУ РЕЖИМІ",334,553,172,18,small,MUTED);text(dc,L"ПЕРНЕ",511,553,138,18,small,MUTED);text(dc,L"ШУ ШЕГІ",665,553,140,18,small,MUTED);text(dc,L"ДАУЫС",822,553,168,18,small,MUTED);
 text(dc,L"Құлаққаппен қосылыңыз · Ctrl+Shift+M: микрофон · Ctrl+Shift+D: тыңдау",334,618,656,20,small,MUTED);
 text(dc,L"Opus 24 кбит/с   ·   Бөлме кілтімен шифрлау   ·   Орнатусыз",334,718,656,20,small,MUTED);
}
void ownerDraw(DRAWITEMSTRUCT* d){bool main=d->CtlID==CONNECT;bool active=(d->CtlID==MUTE&&voice.muted)||(d->CtlID==DEAFEN&&voice.deafened)||(d->CtlID==PTT&&voice.pushToTalk);COLORREF c=main?ACCENT:(active?RGB(65,37,47):CARD);if(d->itemState&ODS_SELECTED)c=main?RGB(51,180,146):LINE;auto b=CreateSolidBrush(c);FillRect(d->hDC,&d->rcItem,b);DeleteObject(b);auto r=d->rcItem;InflateRect(&r,-sx(9),0);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,main?BG:(active?RED:INK));SelectObject(d->hDC,bold);auto s=read(d->hwndItem);DrawTextW(d->hDC,s.c_str(),(int)s.size(),&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);if(d->itemState&ODS_FOCUS){auto f=d->rcItem;InflateRect(&f,-3,-3);DrawFocusRect(d->hDC,&f);}}
void snapshot(const std::wstring& path){RECT r;GetClientRect(window,&r);int w=r.right,h=r.bottom;auto screen=GetDC(window);auto dc=CreateCompatibleDC(screen);auto bitmap=CreateCompatibleBitmap(screen,w,h);auto old=SelectObject(dc,bitmap);draw(dc);for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){RECT cr;GetWindowRect(child,&cr);MapWindowPoints(0,window,(POINT*)&cr,2);int saved=SaveDC(dc);SetViewportOrgEx(dc,cr.left,cr.top,0);SendMessageW(child,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN|PRF_ERASEBKGND);RestoreDC(dc,saved);}BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;Bytes pixels(w*h*4);GetDIBits(dc,bitmap,0,h,pixels.data(),&info,DIB_RGB_COLORS);BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof header+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+(DWORD)pixels.size();auto f=CreateFileW(path.c_str(),GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);if(f!=INVALID_HANDLE_VALUE){DWORD n;WriteFile(f,&header,sizeof header,&n,0);WriteFile(f,&info.bmiHeader,sizeof info.bmiHeader,&n,0);WriteFile(f,pixels.data(),(DWORD)pixels.size(),&n,0);CloseHandle(f);}SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,screen);}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_CREATE:{window=hwnd;normal=font(15);small=font(12);bold=font(14,FW_SEMIBOLD);title=font(28,FW_SEMIBOLD);brand=font(30,FW_BOLD);bgBrush=CreateSolidBrush(BG);editBrush=CreateSolidBrush(CARD);
  nameEdit=control(L"EDIT",L"",NAME,28,157,244,40,ES_AUTOHSCROLL|WS_BORDER);SendMessageW(nameEdit,EM_SETLIMITTEXT,32,0);
  connectBtn=button(L"Байланысты тоқтату",CONNECT,28,307,244,44);
  micCombo=combo(MIC,334,513,308);outCombo=combo(OUTPUT,662,513,328);add(micCombo,L"Windows әдепкі микрофоны");for(UINT i=0;i<waveInGetNumDevs();i++){WAVEINCAPSW c{};if(!waveInGetDevCapsW(i,&c,sizeof c))add(micCombo,c.szPname);}add(outCombo,L"Windows әдепкі дыбысы");for(UINT i=0;i<waveOutGetNumDevs();i++){WAVEOUTCAPSW c{};if(!waveOutGetDevCapsW(i,&c,sizeof c))add(outCombo,c.szPname);}select(micCombo,0);select(outCombo,0);
  pttBtn=button(L"Авто дауыс",PTT,334,578,162,30);keyCombo=combo(KEY,511,578,138);for(auto s:{L"Тінтуір 4",L"Тінтуір 5",L"Сол Alt",L"V"})add(keyCombo,s);gateCombo=combo(GATE,665,578,140);for(auto s:{L"Өшірулі",L"Тыныш",L"Орташа",L"Шулы",L"Өте шулы"})add(gateCombo,s);volCombo=combo(VOLUME,822,578,168);for(auto s:{L"25%",L"50%",L"75%",L"100%",L"125%",L"150%"})add(volCombo,s);
  muteBtn=button(L"Микрофон қосулы",MUTE,334,659,316,42);deafenBtn=button(L"Тыңдау қосулы",DEAFEN,668,659,322,42);
  auto p=iniPath();wchar_t nick[128];GetPrivateProfileStringW(L"BES",L"name",L"",nick,128,p.c_str());if(!nick[0]){auto random=hex(randomBytes(2));auto generated=L"Дос-"+wide(random);wcsncpy_s(nick,generated.c_str(),_TRUNCATE);}write(nameEdit,preview?L"Ойыншы":nick);voice.pushToTalk=GetPrivateProfileIntW(L"BES",L"ptt",0,p.c_str())!=0;write(pttBtn,voice.pushToTalk?L"Басып сөйлеу":L"Авто дауыс");select(keyCombo,std::clamp((int)GetPrivateProfileIntW(L"BES",L"key",0,p.c_str()),0,3));select(gateCombo,std::clamp((int)GetPrivateProfileIntW(L"BES",L"gate",2,p.c_str()),0,4));select(volCombo,std::clamp((int)GetPrivateProfileIntW(L"BES",L"volume",3,p.c_str()),0,5));
  loadRoom();if(!preview){syncSettings();automatic.start();applyConnection();}RegisterHotKey(hwnd,1,MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT,'M');RegisterHotKey(hwnd,2,MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT,'D');SetTimer(hwnd,1,250,0);return 0;
 }
 case WM_ERASEBKGND:return 1;
 case WM_PRINTCLIENT:draw((HDC)wp);return 0;
 case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);auto mem=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(mem,bitmap);draw(mem);BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);EndPaint(hwnd,&ps);return 0;}
 case WM_DRAWITEM:ownerDraw((DRAWITEMSTRUCT*)lp);return TRUE;
 case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor((HDC)wp,INK);SetBkColor((HDC)wp,CARD);return (LRESULT)editBrush;
 case WM_CTLCOLORSTATIC:SetTextColor((HDC)wp,MUTED);SetBkColor((HDC)wp,PANEL);return (LRESULT)bgBrush;
 case WM_COMMAND:{
  int id=LOWORD(wp),notification=HIWORD(wp);
  if(id==CONNECT)connect();
  else if(id==MUTE){voice.muted=!voice.muted;write(muteBtn,voice.muted?L"Микрофон өшірулі":L"Микрофон қосулы");}
  else if(id==DEAFEN){voice.deafened=!voice.deafened;write(deafenBtn,voice.deafened?L"Тыңдау өшірулі":L"Тыңдау қосулы");}
  else if(id==PTT){voice.pushToTalk=!voice.pushToTalk;write(pttBtn,voice.pushToTalk?L"Басып сөйлеу":L"Авто дауыс");save();}
  else if((id==KEY||id==GATE||id==VOLUME)&&notification==CBN_SELCHANGE)syncSettings();
  else if((id==MIC||id==OUTPUT)&&notification==CBN_SELCHANGE&&automatic.isEnabled())applyConnection();
  else if(id==NAME&&notification==EN_KILLFOCUS&&!preview){if(automatic.isEnabled())applyConnection();else save();}
  InvalidateRect(hwnd,0,FALSE);return 0;
 }
 case WM_HOTKEY:SendMessageW(hwnd,WM_COMMAND,wp==1?MUTE:DEAFEN,0);return 0;
 case WM_TIMER:{ticks++;current=voice.getMembers();auto status=room.valid()?automatic.status():L"Сервер баптауы күтілуде";
  if((status!=lastStatus||voice.connected)&&!IsIconic(hwnd)&&IsWindowVisible(hwnd))InvalidateRect(hwnd,0,FALSE);
  lastStatus=status;return 0;
 }
 case WM_SIZE:if(wp==SIZE_MINIMIZED){showTray(true);ShowWindow(hwnd,SW_HIDE);}return 0;
 case WM_APP+1:if(lp==WM_LBUTTONDBLCLK){showTray(false);ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);}else if(lp==WM_RBUTTONUP){auto m=CreatePopupMenu();AppendMenuW(m,MF_STRING,1,L"BES ашу");AppendMenuW(m,MF_STRING,2,L"Шығу");POINT p;GetCursorPos(&p);SetForegroundWindow(hwnd);int i=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,0);DestroyMenu(m);if(i==1){showTray(false);ShowWindow(hwnd,SW_RESTORE);}if(i==2)DestroyWindow(hwnd);}return 0;
 case WM_CLOSE:DestroyWindow(hwnd);return 0;
 case WM_DESTROY:KillTimer(hwnd,1);showTray(false);UnregisterHotKey(hwnd,1);UnregisterHotKey(hwnd,2);automatic.stop();voice.stop();for(auto f:{normal,small,bold,title,brand})DeleteObject(f);DeleteObject(bgBrush);DeleteObject(editBrush);PostQuitMessage(0);return 0;
 }return DefWindowProcW(hwnd,msg,wp,lp);
}
int run(HINSTANCE instance,const std::wstring& screenshot=L""){
 preview=!screenshot.empty();SetProcessDPIAware();RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);auto screen=GetDC(0);scale=std::min({GetDeviceCaps(screen,LOGPIXELSX)/96.0f,(work.right-work.left-40)/1024.f,(work.bottom-work.top-70)/760.f});ReleaseDC(0,screen);scale=std::max(scale,0.7f);
 WNDCLASSEXW c{};c.cbSize=sizeof c;c.style=CS_HREDRAW|CS_VREDRAW;c.lpfnWndProc=procedure;c.hInstance=instance;c.hCursor=LoadCursorW(0,IDC_ARROW);c.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));c.hIconSm=c.hIcon;c.lpszClassName=L"BESVoiceWindow";RegisterClassExW(&c);RECT r{0,0,sx(1024),sx(760)};DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;AdjustWindowRect(&r,style,FALSE);auto hwnd=CreateWindowExW(0,c.lpszClassName,L"BES — достарыңның дауысы",style,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,0,0,instance,0);if(!hwnd)return 1;ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);
 if(preview){ShowWindow(hwnd,SW_SHOWNOACTIVATE);RedrawWindow(hwnd,0,0,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);MSG m;while(PeekMessageW(&m,0,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}snapshot(screenshot);DestroyWindow(hwnd);return 0;}
 MSG m;while(GetMessageW(&m,0,0,0)>0){if(!IsDialogMessageW(hwnd,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}return (int)m.wParam;
}
}

uint64_t filetimeValue(FILETIME f){return (uint64_t)f.dwLowDateTime|((uint64_t)f.dwHighDateTime<<32);}
int bot(const std::wstring& url,const std::wstring& code,const std::wstring& name,int seconds,const std::wstring& path,bool hardware=false){
 VoiceSession s;s.gate=0;FILETIME a,b,k0,u0,k1,u1;GetProcessTimes(GetCurrentProcess(),&a,&b,&k0,&u0);auto start=GetTickCount64();s.start(url,code,name,WAVE_MAPPER,WAVE_MAPPER,!hardware);ULONGLONG peak=0;bool joined=false;int maxPeers=0;auto next=start;
 while(GetTickCount64()-start<(ULONGLONG)seconds*1000){if(s.connected){joined=true;int n=0;for(auto& m:s.getMembers())if(m.present)n++;maxPeers=std::max(maxPeers,n);}if(GetTickCount64()>=next){s.heartbeat();next+=2000;}PROCESS_MEMORY_COUNTERS pm{};if(GetProcessMemoryInfo(GetCurrentProcess(),&pm,sizeof pm))peak=std::max<ULONGLONG>(peak,pm.WorkingSetSize);Sleep(100);if(!s.running)break;}
 auto finalStatus=utf8(s.getStatus());s.stop();GetProcessTimes(GetCurrentProcess(),&a,&b,&k1,&u1);auto ms=GetTickCount64()-start;double cpu=(filetimeValue(k1)+filetimeValue(u1)-filetimeValue(k0)-filetimeValue(u0))/10000.0;
 std::ostringstream out;out<<"{\"joined\":"<<(joined?"true":"false")<<",\"maxPeers\":"<<maxPeers<<",\"sent\":"<<s.sent<<",\"received\":"<<s.received<<",\"mixed\":"<<s.mixed<<",\"decryptErrors\":"<<s.decryptErrors<<",\"workingSetBytes\":"<<peak<<",\"cpuMs\":"<<cpu<<",\"elapsedMs\":"<<ms<<",\"audioError\":"<<(s.audioError?"true":"false")<<"}";std::ofstream f(path.c_str());f<<out.str();return joined&&s.received>0&&!s.audioError?0:2;
}
int selftest(const std::wstring& path){
 bool success=true;auto code=hex(randomBytes(16));Crypto a,b;a.init(code);b.init(code);auto nonce=randomBytes(12);Bytes aad{1,2,3,4},plain{0,1,2,3,255},enc,dec;unsigned char tag[16];success&=a.crypt(true,plain,nonce.data(),aad.data(),enc,tag);success&=b.crypt(false,enc,nonce.data(),aad.data(),dec,tag)&&dec==plain;tag[0]^=1;success&=!b.crypt(false,enc,nonce.data(),aad.data(),dec,tag);
 int err=0;auto* e=opus_encoder_create(RATE,1,OPUS_APPLICATION_VOIP,&err);auto* d=opus_decoder_create(RATE,1,&err);short in[SAMPLES],out[SAMPLES];unsigned char packet[1275];double energy=0;for(int t=0;t<100;t++){for(int i=0;i<SAMPLES;i++)in[i]=(short)(5000*sin(2*3.141592653589793*440*(t*SAMPLES+i)/RATE));int n=opus_encode(e,in,SAMPLES,packet,sizeof packet);int samples=opus_decode(d,packet,n,out,SAMPLES,0);success&=samples==SAMPLES;for(auto v:out)energy+=double(v)*v;}success&=energy>100000;opus_encoder_destroy(e);opus_decoder_destroy(d);std::ofstream f(path.c_str());f<<"{\"passed\":"<<(success?"true":"false")<<",\"opus\":\""<<opus_get_version_string()<<"\",\"aesGcmTamperRejected\":true}";return success?0:1;
}
int autoBot(const std::wstring& url,const std::wstring& code,const std::wstring& name,int seconds,const std::wstring& path){
 VoiceSession voice;voice.gate=0;AutoVoice automatic(voice);automatic.configure({url,code},name,WAVE_MAPPER,WAVE_MAPPER,true);automatic.start();
 ULONGLONG end=GetTickCount64()+seconds*1000;int joins=0,maxPeers=0;bool previous=false;uint64_t seenReceived=0;
 while(GetTickCount64()<end){bool connected=voice.connected;if(connected&&!previous)joins++;previous=connected;if(connected){int n=0;for(auto& member:voice.getMembers())if(member.present)n++;maxPeers=std::max(maxPeers,n);seenReceived=std::max(seenReceived,voice.received.load());}Sleep(50);}
 int attempts=automatic.getAttempts();automatic.stop();std::ofstream report(path.c_str());report<<"{\"joins\":"<<joins<<",\"attempts\":"<<attempts<<",\"maxPeers\":"<<maxPeers<<",\"received\":"<<seenReceived<<",\"decryptErrors\":"<<voice.decryptErrors<<"}";
 return joins>0&&seenReceived>100?0:2;
}
int profileTest(const std::wstring& directory){
 auto secret=wide(hex(randomBytes(16)));RoomProfile expected{L"wss://example.invalid/voice",secret};auto path=directory+L"\\profile-test.bin";
 bool ok=saveProtectedProfile(path,expected);auto actual=readProtectedProfile(path);ok=ok&&actual.server==expected.server&&actual.secret==expected.secret;
 auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,0,OPEN_EXISTING,0,0);if(file!=INVALID_HANDLE_VALUE){unsigned char b=0;DWORD n;WriteFile(file,&b,1,&n,0);CloseHandle(file);}ok=ok&&!readProtectedProfile(path).valid();DeleteFileW(path.c_str());
 std::ofstream report((directory+L"\\profile-test.json").c_str());report<<"{\"passed\":"<<(ok?"true":"false")<<"}";return ok?0:2;
}
int embeddedCheck(const std::wstring& path){
 auto profile=readEmbeddedProfile();bool valid=profile.valid();std::ofstream report(path.c_str());report<<"{\"embeddedRoomValid\":"<<(valid?"true":"false")<<"}";return valid?0:2;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);int result=0;
 try{if(argc>=3&&std::wstring(argv[1])==L"--relay")result=relay::run(_wtoi(argv[2]));else if(argc>=7&&std::wstring(argv[1])==L"--auto-bot")result=autoBot(argv[2],argv[3],argv[4],std::clamp(_wtoi(argv[5]),1,300),argv[6]);else if(argc>=3&&std::wstring(argv[1])==L"--embedded-check")result=embeddedCheck(argv[2]);else if(argc>=3&&std::wstring(argv[1])==L"--profile-test")result=profileTest(argv[2]);else if(argc>=7&&(std::wstring(argv[1])==L"--bot"||std::wstring(argv[1])==L"--hardware-bot"))result=bot(argv[2],argv[3],argv[4],std::clamp(_wtoi(argv[5]),1,300),argv[6],std::wstring(argv[1])==L"--hardware-bot");else if(argc>=3&&std::wstring(argv[1])==L"--self-test")result=selftest(argv[2]);else if(argc>=3&&std::wstring(argv[1])==L"--preview")result=ui::run(instance,argv[2]);else result=ui::run(instance);}catch(...){result=3;}LocalFree(argv);return result;
}
