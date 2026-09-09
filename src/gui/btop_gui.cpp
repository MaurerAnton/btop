// btop-gui v7 — full visual fidelity: nested boxes, proper borders, terminal-accurate layout
#include "btop_gui.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
using namespace std;

namespace Runner{atomic<bool>stopping{false},coreNum_reset{false},active{false},redraw{false};bool pause_output=false;}
namespace Global{extern const string Version="1.4.7-gui";string overlay,exit_error_msg,clock;
    atomic<bool>resized{false},init_conf{true},quitting{false};uid_t real_uid=0,set_uid=0;const vector<array<string,2>>Banner_src;}
namespace Cpu{int width=100,min_width=60,min_height=8;}
namespace Mem{int width=300,min_width=36,min_height=6;bool redraw=true;}
namespace Net{int width=300,min_width=36,min_height=6;bool redraw=true;}
namespace Proc{int width=300,min_width=44,min_height=16;bool redraw=true,shown=true;
    int select_max=1,selected_pid=0,start=0,selected=0,selected_depth=0;string selected_name;}
namespace Gpu{int width=100,min_width=36,count=0,shown=0;}namespace Menu{bool active=false,redraw=false;}
void clean_quit(int){}namespace Input{unordered_map<string,array<int,4>>mouse_mappings;}

// True color scheme from btop's default theme
static wxColour BG(24,24,24),BOX_FILL(29,29,29),BOX_BORDER(52,52,52);
static wxColour TITLE_C(62,184,230),HI_C(98,200,240),MAIN_C(212,212,212);
static wxColour DIV_C(64,64,64),GRAPH_BG(19,19,19);
static wxColour CPU_C(80,180,220),DL_C(60,200,80),UL_C(220,140,40);
static wxColour RAM_C(80,180,80),SWAP_C(200,160,60),DISK_C(140,180,220);
static wxColour TGREY(150,150,150),TGREEN(120,200,120);
static wxColour TEMP_C(220,60,60);

static string hb(uint64_t b){const char*u[]={"B","KB","MB","GB","TB"};double v=b;int i=0;
    while(v>=1000&&i<4){v/=1000;i++;}ostringstream s;s<<fixed<<setprecision(v<10?1:0)<<v<<" "<<u[i];return s.str();}
static string ss(uint64_t b){return hb(b)+"/s";}
static string sf(double s){int d=s/86400,h=((int)s%86400)/3600,m=((int)s%3600)/60;char b[64];
    if(d)snprintf(b,sizeof(b),"%dd%02d:%02d",d,h,m);else snprintf(b,sizeof(b),"%02d:%02d:%02d",h,m,(int)s%60);return b;}

wxBEGIN_EVENT_TABLE(Dashboard,wxScrolledWindow)EVT_PAINT(Dashboard::OnPaint)wxEND_EVENT_TABLE()
Dashboard::Dashboard(wxWindow*p):wxScrolledWindow(p,wxID_ANY){state.loadavg[0]=state.loadavg[1]=state.loadavg[2]=0;
    SetBackgroundStyle(wxBG_STYLE_PAINT);SetScrollRate(8,8);SetBackgroundColour(BG);
    timer=new wxTimer(this);
    timer->Bind(wxEVT_TIMER,[this](wxTimerEvent&){
        if(painting)return; // skip if painting in progress
        Cpu::collect(false);Mem::collect(false);Net::collect(false);Proc::collect(false);
        RefreshState();Refresh(false);});timer->Start(1500);
    try{RefreshState();}catch(...){fprintf(stderr,"btop-gui: RefreshState crashed\n");}}
void Dashboard::RefreshState(){
    auto&cpu=Cpu::current_cpu;if(cpu.cpu_percent.contains("total"s))state.cpu_total=cpu.cpu_percent.at("total"s);
    state.cpu_name=Cpu::cpuName;state.cpu_freq=Cpu::cpuHz;state.cpu_cores=cpu.core_percent;
    if(!cpu.load_avg.empty()){state.loadavg[0]=cpu.load_avg[0];state.loadavg[1]=cpu.load_avg.size()>1?cpu.load_avg[1]:0;
        state.loadavg[2]=cpu.load_avg.size()>2?cpu.load_avg[2]:0;}
    if(Cpu::got_sensors&&!cpu.temp.empty()&&!cpu.temp[0].empty())state.cpu_temp=cpu.temp[0].back();
    if(Cpu::has_battery){auto[p,w,s,st]=Cpu::current_bat;state.battery_pct=p;}
    auto&mem=Mem::current_mem;state.mem_total=Mem::get_totalMem();state.mem_used=mem.stats["used"];
    state.mem_avail=mem.stats["available"];state.mem_cache=mem.stats["cached"];state.mem_free=mem.stats["free"];
    state.mem_anon=mem.stats.contains("anon")?mem.stats["anon"]:0;
    state.swap_total=mem.stats["swap_total"];state.swap_used=mem.stats["swap_used"];state.swap_free=mem.stats["swap_free"];
    state.disks.clear();for(auto&m:mem.disks_order)if(mem.disks.contains(m))state.disks.push_back({m,mem.disks.at(m)});
    if(Net::current_net.contains(Net::selected_iface)){auto&n=Net::current_net.at(Net::selected_iface);
        if(n.bandwidth.contains("download"s)){state.net_dl=n.bandwidth.at("download"s);state.net_dl_speed=n.stat.at("download"s).speed;
            state.net_dl_total=n.stat.at("download"s).total;state.net_dl_avg=n.stat.at("download"s).avg_speed;}
        if(n.bandwidth.contains("upload"s)){state.net_ul=n.bandwidth.at("upload"s);state.net_ul_speed=n.stat.at("upload"s).speed;
            state.net_ul_total=n.stat.at("upload"s).total;state.net_ul_avg=n.stat.at("upload"s).avg_speed;}
        state.net_ip=n.ipv4.empty()?n.ipv6:n.ipv4;state.net_connected=n.connected;state.net_iface=Net::selected_iface;}
    state.procs=Proc::current_procs;}

// ─── Drawing primitives ────────────────────────────────────
void Dashboard::OnPaint(wxPaintEvent&){
    painting=true;
    wxPaintDC dc(this);
    DoPaint(dc);
    painting=false;
}

void Dashboard::DrawBox(wxDC&dc,int x,int y,int w,int h,const wxColour&fill,const wxColour&border){
    if(w<=0||h<=0)return;
    dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(fill));dc.DrawRectangle(x,y,w,h);
    dc.SetPen(wxPen(border,1));dc.SetBrush(*wxTRANSPARENT_BRUSH);dc.DrawRectangle(x,y,w,h);}

void Dashboard::DrawInnerBox(wxDC&dc,int x,int y,int w,int h,const wxString&title){
    DrawBox(dc,x,y,w,h,BOX_FILL,DIV_C);
    // Title in inner box header
    dc.SetTextForeground(TITLE_C);
    dc.SetFont(wxFont(7,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_BOLD));
    dc.DrawText(title,x+2,y+1);}

void Dashboard::Txt(wxDC&dc,int x,int y,const wxString&s,const wxColour&c,int sz,bool b){
    dc.SetTextForeground(c);dc.SetFont(wxFont(sz,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,b?wxFONTWEIGHT_BOLD:wxFONTWEIGHT_NORMAL));dc.DrawText(s,x,y);}

void Dashboard::Bar(wxDC&dc,int x,int y,int w,int h,double p,const wxColour&c){
    if(w<=0||h<=0)return;
    p=max(0.0,min(1.0,p));int fw=(int)(w*p);
    if(fw>0){dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(c));dc.DrawRectangle(x,y,fw,h);}
    dc.SetPen(wxPen(DIV_C));dc.SetBrush(*wxTRANSPARENT_BRUSH);dc.DrawRectangle(x,y,w,h);}

void Dashboard::LineG(wxDC&dc,int x,int y,int w,int h,const deque<long long>&d,long long mx,const wxColour&c,bool border){
    if(w<=4||h<=4)return;
    dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(GRAPH_BG));dc.DrawRectangle(x,y,w,h);
    if(d.size()>=2){long long m=mx?mx:max(1ll,*max_element(d.begin(),d.end()));m=max(m,1ll);int n=d.size();
        dc.SetPen(wxPen(c,1));double xs=(double)w/max(1,n-1),ys=(double)h/(double)m;int lx=-1,ly=-1;
        for(int i=0;i<n;i++){int px=x+(int)(i*xs),py=y+h-(int)(d[i]*ys);py=max(y,min(y+h,py));
            if(lx>=0)dc.DrawLine(lx,ly,px,py);lx=px;ly=py;}}
    if(border){dc.SetPen(wxPen(DIV_C));dc.SetBrush(*wxTRANSPARENT_BRUSH);dc.DrawRectangle(x,y,w,h);}}

// ─── Layout ───────────────────────────────────────────────
void Dashboard::DoPaint(wxDC&dc){
    wxSize sz=GetClientSize();int W=max(680,sz.x);
    if(W<100)return;
    dc.SetBackground(wxBrush(BG));dc.Clear();
    int gap=2,x=2,y=2;
    int cpu_w=W-4,cpu_h=show_cpu?CpuH(cpu_w):0;
    fprintf(stderr,"cpu_w=%d cpu_h=%d\n",cpu_w,cpu_h);fflush(stderr);
    if(show_cpu){DrawCPU(dc,x,y,cpu_w,cpu_h);y+=cpu_h+gap;}
    fprintf(stderr,"cpu done y=%d\n",y);fflush(stderr);
}

int Dashboard::CpuH(int w){int ncores=max(1,(int)state.cpu_cores.size()),cols=max(1,(w-24)/160);
    return 18+56+3+14+3+((ncores+cols-1)/cols)*11+8;}
int Dashboard::NetH(int) {return 18+3+48+12+3+48+12+8;}
int Dashboard::MemH(int){int d=min(8,(int)state.disks.size())+1;return 18+3+20+2+14+2+14+12+2+16+d*14+8;}
int Dashboard::ProcH(int){int n=min(30,(int)state.procs.size())+1;return 18+14+n*14+8;}

// ─── CPU Box ──────────────────────────────────────────────
void Dashboard::DrawCPU(wxDC&dc,int x,int y,int ow,int oh){
    fprintf(stderr,"DrawCPU: x=%d y=%d ow=%d oh=%d\n",x,y,ow,oh);fflush(stderr);
    DrawBox(dc,x,y,ow,oh,BOX_FILL,BOX_BORDER);
    int title_h=17;
    fprintf(stderr,"DrawCPU: box drawn\n");fflush(stderr);
    string nm=state.cpu_name;
#ifdef __linux__
    if(Shared::physical_cores>0){nm+=" ["+to_string(Shared::physical_cores)+"C";if(Shared::smt_enabled)nm+="/"+to_string(Shared::coreCount)+"T";nm+="]";}
#endif
    int bw=ow*38/100;if(bw<28)bw=28;if(bw>65)bw=65;
    // Title bar in outer box
    fprintf(stderr,"DrawCPU: title bar\n");fflush(stderr);
    dc.SetTextForeground(HI_C);Txt(dc,x+4,y+2,"m",HI_C,8,1);Txt(dc,x+12,y+2,"enu",TITLE_C,8);
    Txt(dc,x+44,y+2,"p",HI_C,8,1);Txt(dc,x+52,y+2,"reset",TITLE_C,8);
    // CPU name centered
    {dc.SetTextForeground(TITLE_C);wxFont f(8,wxFONTFAMILY_TELETYPE,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_BOLD);dc.SetFont(f);
        int tw,th;dc.GetTextExtent(wxString::FromUTF8(nm),&tw,&th);dc.DrawText(wxString::FromUTF8(nm),x+(ow-tw)/2,y+2);}
    if(!state.cpu_freq.empty())Txt(dc,x+ow-bw-70,y+2,wxString::FromUTF8(state.cpu_freq),TGREEN,8);
    y+=title_h;
    // CPU graph
    int gx=x+4,gy=y,gw=ow-8,gh=oh-title_h-16;
    fprintf(stderr,"DrawCPU: graph gx=%d gy=%d gw=%d gh=%d\n",gx,gy,gw,gh);fflush(stderr);
    if(gh>10){
        fprintf(stderr,"DrawCPU: lineg start\n");fflush(stderr);
        LineG(dc,gx,gy,gw,gh,state.cpu_total,100,CPU_C,true);
        fprintf(stderr,"DrawCPU: lineg done\n");fflush(stderr);
    }
    fprintf(stderr,"DrawCPU: after graph\n");fflush(stderr);
    // Stats below graph
    long long pct=state.cpu_total.empty()?0:state.cpu_total.back();
    int sy=gy+gh+4;
    Txt(dc,x+4,sy,wxString::Format("CPU %lld%%",pct),MAIN_C,10,1);
    if(state.cpu_temp>0)Txt(dc,x+120,sy,wxString::Format("Temp %lld°C",state.cpu_temp),TEMP_C,8);
    sy+=14;
    ostringstream si;si<<"Load avg: "<<fixed<<setprecision(2)<<state.loadavg[0]<<" "<<state.loadavg[1]<<" "<<state.loadavg[2];
    si<<"   Up: "<<sf(Tools::system_uptime());
    if(state.battery_pct>=0)si<<"   BAT: "<<state.battery_pct<<"%";
    Txt(dc,x+4,sy,wxString::FromUTF8(si.str()),MAIN_C,8);
    sy+=14;
    // Per-core bars in a grid
    int ncores=max(1,(int)state.cpu_cores.size());
    int cols2=max(1,(ow-8)/160);
    for(int i=0;i<ncores;i++){
        int cx=x+4+(i%cols2)*(ow/cols2),cy=sy+(i/cols2)*11;
        if(cy>y+oh-10)break;
        Txt(dc,cx,cy,wxString::Format("C%d",i),TGREY,7);
        double cp=0;if(i<(int)state.cpu_cores.size()&&!state.cpu_cores[i].empty())cp=state.cpu_cores[i].back();
        wxColour cb=cp>90?wxColour(220,50,50):cp>75?wxColour(240,150,30):wxColour(60,180,75);
        Bar(dc,cx+22,cy+1,(ow/cols2)-50,8,cp/100.0,cb);
        Txt(dc,cx+(ow/cols2)-26,cy,wxString::Format("%.0f%%",cp),MAIN_C,7);
    }
}

// ─── Net Box ──────────────────────────────────────────────
void Dashboard::DrawNet(wxDC&dc,int x,int y,int w,int h){
    DrawBox(dc,x,y,w,h,BOX_FILL,BOX_BORDER);int pad=3,title_h=18;
    string iface=state.net_iface,ip=state.net_ip,conn=state.net_connected?"●":"○";
    // Title: [b] iface [n]ext [z]ero [a]uto [y]sync
    Txt(dc,x+4,y+2,"b",HI_C,8,1);Txt(dc,x+12,y+2," "+iface,MAIN_C,8);
    Txt(dc,x+20+iface.size()*7,y+2,"n",HI_C,8,1);
    Txt(dc,x+44,y+2,"z",HI_C,8,1);Txt(dc,x+50,y+2,"ero",TITLE_C,8);
    Txt(dc,x+76,y+2,"a",HI_C,8,1);Txt(dc,x+82,y+2,"uto",TITLE_C,8);
    Txt(dc,x+108,y+2,"y",HI_C,8,1);Txt(dc,x+114,y+2,"sync",TITLE_C,8);
    if(!ip.empty()){dc.SetTextForeground(TITLE_C);dc.SetFont(wxFont(8,wxFONTFAMILY_TELETYPE,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_BOLD));
        int tw,th;dc.GetTextExtent(wxString::FromUTF8(conn+" "+ip),&tw,&th);dc.DrawText(wxString::FromUTF8(conn+" "+ip),x+w-tw-4,y+2);}
    y+=title_h;int gh=(h-title_h-40)/2;if(gh<20)gh=20;
    // DL
    Txt(dc,x+pad,y,"▼ DL",DL_C,8,1);y+=10;
    LineG(dc,x+pad,y,w-2*pad,gh,state.net_dl,0,DL_C,true);y+=gh;
    Txt(dc,x+pad,y,wxString::Format("Spd:%s Tot:%s 5m:%s",ss(state.net_dl_speed),hb(state.net_dl_total),ss(state.net_dl_avg)),MAIN_C,7);y+=16;
    // UL
    Txt(dc,x+pad,y,"▲ UL",UL_C,8,1);y+=10;
    LineG(dc,x+pad,y,w-2*pad,gh,state.net_ul,0,UL_C,true);y+=gh;
    Txt(dc,x+pad,y,wxString::Format("Spd:%s Tot:%s 5m:%s",ss(state.net_ul_speed),hb(state.net_ul_total),ss(state.net_ul_avg)),MAIN_C,7);}

// ─── Mem Box ──────────────────────────────────────────────
void Dashboard::DrawMem(wxDC&dc,int x,int y,int w,int h){
    DrawBox(dc,x,y,w,h,BOX_FILL,BOX_BORDER);int pad=3,title_h=18;
    Txt(dc,x+4,y+2,"d",HI_C,8,1);Txt(dc,x+10,y+2,"isks",TITLE_C,8);
    {dc.SetTextForeground(TITLE_C);wxFont f(8,wxFONTFAMILY_TELETYPE,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_BOLD);
        dc.SetFont(f);int tw,th;wxString tt="Memory & Disks";dc.GetTextExtent(tt,&tw,&th);dc.DrawText(tt,x+(w-tw)/2,y+2);}
    y+=title_h+4;
    double rp=state.mem_total>0?(double)state.mem_used/state.mem_total:0;
    Txt(dc,x+pad,y,wxString::Format("RAM  %s / %s",hb(state.mem_used),hb(state.mem_total)),MAIN_C,8);
    Txt(dc,x+w-44,y,wxString::Format("%.0f%%",rp*100),MAIN_C,8);y+=13;
    Bar(dc,x+pad,y,w-2*pad,16,rp,RAM_C);y+=18;
    Txt(dc,x+pad,y,wxString::Format("av:%s ca:%s fr:%s",hb(state.mem_avail),hb(state.mem_cache),hb(state.mem_free)),TGREY,8);y+=14;
    if(state.mem_anon>0){Txt(dc,x+pad,y,wxString::Format("anon:%s",hb(state.mem_anon)),TGREY,8);y+=14;}
    if(state.swap_total>0){double sp=(double)state.swap_used/state.swap_total;
        Txt(dc,x+pad,y,wxString::Format("Swap %s / %s",hb(state.swap_used),hb(state.swap_total)),MAIN_C,8);
        Txt(dc,x+w-44,y,wxString::Format("%.0f%%",sp*100),MAIN_C,8);y+=14;
        Bar(dc,x+pad,y,w-2*pad,8,sp,SWAP_C);y+=14;}
    Txt(dc,x+pad,y,"Disks",DISK_C,8,1);y+=16;
    int dc2=min(8,(int)state.disks.size());
    for(int i=0;i<dc2;i++){auto&[mnt,d]=state.disks[i];(void)mnt;Txt(dc,x+pad,y,wxString::FromUTF8(d.name),MAIN_C,8);
        double dp=d.total>0?(double)d.used/d.total:0;wxColour c2=dp>0.9?wxColour(220,50,50):dp>0.75?wxColour(240,150,30):RAM_C;
        Bar(dc,x+90,y+2,w-200,10,dp,c2);ostringstream di;di<<hb(d.used)<<"/"<<hb(d.total)<<" ("<<d.used_percent<<"%)";
        Txt(dc,x+w-130,y,wxString::FromUTF8(di.str()),TGREY,8);y+=14;}}

// ─── Proc Box ──────────────────────────────────────────────
void Dashboard::DrawProc(wxDC&dc,int x,int y,int w,int h){
    DrawBox(dc,x,y,w,h,BOX_FILL,BOX_BORDER);int pad=4,title_h=18,nshow=min(30,(int)state.procs.size()),row_h=14;
    Txt(dc,x+4,y+2,"f",HI_C,8,1);Txt(dc,x+10,y+2,"ilter",TITLE_C,8);
    Txt(dc,x+60,y+2,"t",HI_C,8,1);Txt(dc,x+66,y+2,"ree",TITLE_C,8);
    Txt(dc,x+94,y+2,"s",HI_C,8,1);Txt(dc,x+100,y+2,"ort",TITLE_C,8);
    Txt(dc,x+128,y+2,"k",HI_C,8,1);Txt(dc,x+134,y+2,"ill",TITLE_C,8);
    Txt(dc,x+162,y+2,"e",HI_C,8,1);Txt(dc,x+168,y+2,"dit",TITLE_C,8);
    Txt(dc,x+196,y+2,"d",HI_C,8,1);Txt(dc,x+202,y+2,"etail",TITLE_C,8);
    {dc.SetTextForeground(TITLE_C);wxFont f(8,wxFONTFAMILY_TELETYPE,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_BOLD);dc.SetFont(f);
        int tw,th;wxString tt=wxString::Format("Processes (%zu)",state.procs.size());dc.GetTextExtent(tt,&tw,&th);
        dc.DrawText(tt,x+w-tw-4,y+2);}
    y+=title_h;
    Txt(dc,x+pad,y,"PID",TGREY,8,1);Txt(dc,x+70,y,"Name",TGREY,8,1);Txt(dc,x+w-160,y,"CPU%",TGREY,8,1);
    Txt(dc,x+w-100,y,"MEM",TGREY,8,1);Txt(dc,x+w-260,y,"User",TGREY,8,1);Txt(dc,x+w-310,y,"Thr",TGREY,8,1);y+=14;
    for(int i=0;i<nshow;i++){auto&p=state.procs[i];
        wxColour tc=p.cpu_p>50?wxColour(255,140,100):p.cpu_p>20?wxColour(240,220,140):MAIN_C;
        Txt(dc,x+pad,y,wxString::Format("%zu",p.pid),tc,8);string nm=p.name;if(nm.size()>35)nm=nm.substr(0,32)+"...";
        Txt(dc,x+70,y,wxString::FromUTF8(nm),tc,8);Txt(dc,x+w-160,y,wxString::Format("%.1f",p.cpu_p),tc,8);
        Txt(dc,x+w-100,y,wxString::FromUTF8(hb(p.mem)),tc,8);string us=p.user;if(us.size()>12)us=us.substr(0,11);
        Txt(dc,x+w-260,y,wxString::FromUTF8(us),tc,8);Txt(dc,x+w-310,y,wxString::Format("%zu",p.threads),tc,8);y+=row_h;}}

wxBEGIN_EVENT_TABLE(MainFrame,wxFrame)EVT_CLOSE(MainFrame::OnClose)EVT_KEY_DOWN(MainFrame::OnKeyDown)wxEND_EVENT_TABLE()
MainFrame::MainFrame():wxFrame(nullptr,wxID_ANY,"btop++ GUI",wxDefaultPosition,wxSize(960,800)){SetBackgroundColour(BG);dash=new Dashboard(this);Maximize();}
void MainFrame::TakeScreenshot(const wxString&p){wxSize sz=GetClientSize();wxBitmap bmp(sz.x,sz.y);wxMemoryDC mdc(bmp);
    mdc.SetBackground(wxBrush(BG));mdc.Clear();dash->DoPaint(mdc);mdc.SelectObject(wxNullBitmap);bmp.SaveFile(p,wxBITMAP_TYPE_PNG);}
void MainFrame::OnClose(wxCloseEvent&){Destroy();}
void MainFrame::OnKeyDown(wxKeyEvent&e){int k=e.GetKeyCode();
    if(k=='1'){dash->show_cpu=!dash->show_cpu;dash->Refresh();}else if(k=='2'){dash->show_mem=!dash->show_mem;dash->Refresh();}
    else if(k=='3'){dash->show_net=!dash->show_net;dash->Refresh();}else if(k=='4'){dash->show_proc=!dash->show_proc;dash->Refresh();}
    else if(k=='S'&&e.ControlDown())TakeScreenshot("/tmp/btop-gui-screenshot.png");e.Skip();}
bool BtopApp::OnInit(){
    fprintf(stderr,"btop-gui: OnInit start\n");fflush(stderr);
    bool shot=false;for(int i=1;i<argc;i++)if(string(argv[i])=="--screenshot")shot=true;
    try{
        fprintf(stderr,"btop-gui: Shared::init...\n");fflush(stderr);
        Shared::init();
        fprintf(stderr,"btop-gui: collecting data...\n");fflush(stderr);
        Cpu::collect(false);Mem::collect(false);Net::collect(false);Proc::collect(false);
    }catch(const exception&e){
        fprintf(stderr,"btop-gui: ERROR in init: %s\n",e.what());fflush(stderr);
        return false;
    }catch(...){
        fprintf(stderr,"btop-gui: UNKNOWN ERROR in init\n");fflush(stderr);
        return false;
    }
    fprintf(stderr,"btop-gui: creating frame...\n");fflush(stderr);
    MainFrame*f=new MainFrame();
    fprintf(stderr,"btop-gui: showing...\n");fflush(stderr);
    f->Show(true);
    if(shot){wxTimer*st=new wxTimer(f);
        st->Bind(wxEVT_TIMER,[f,st](wxTimerEvent&){f->dash->RefreshState();f->dash->Refresh();f->Update();
            wxTimer*s2=new wxTimer(f);s2->Bind(wxEVT_TIMER,[f,s2](wxTimerEvent&){f->TakeScreenshot("/tmp/btop-gui-screenshot.png");f->Close();});s2->Start(500);st->Stop();});st->Start(2000);}
    fprintf(stderr,"btop-gui: OnInit done\n");fflush(stderr);
    return true;}int BtopApp::OnExit(){return 0;}wxIMPLEMENT_APP(BtopApp);
