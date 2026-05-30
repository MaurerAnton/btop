// btop-gui v5 — exact terminal btop layout: CPU|Net side by side, Mem full, Proc full
#include "btop_gui.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <atomic>
#include <cmath>
using namespace std;

// ─── Stubs ─────────────────────────────────────────────────
namespace Runner{atomic<bool> stopping{false},coreNum_reset{false},active{false},redraw{false};bool pause_output=false;}
namespace Global{extern const string Version="1.4.7-gui";string overlay,exit_error_msg,clock;
    atomic<bool> resized{false},init_conf{true},quitting{false};uid_t real_uid=0,set_uid=0;
    const vector<array<string,2>> Banner_src;}
namespace Cpu{int width=100,min_width=60,min_height=8;}
namespace Mem{int width=300,min_width=36,min_height=6;bool redraw=true;}
namespace Net{int width=300,min_width=36,min_height=6;bool redraw=true;}
namespace Proc{int width=300,min_width=44,min_height=16;bool redraw=true,shown=true;
    int select_max=1,selected_pid=0,start=0,selected=0,selected_depth=0;string selected_name;}
namespace Gpu{int width=100,min_width=36,count=0,shown=0;}
namespace Menu{bool active=false,redraw=false;}
void clean_quit(int){}
namespace Input{unordered_map<string,array<int,4>> mouse_mappings;}

// ─── Colors ────────────────────────────────────────────────
static wxColour CBG(22,22,22),CBOX(26,26,26),CTITLE(60,180,230),CHI(100,200,240);
static wxColour CMFG(210,210,210),CDIV(65,65,65),CBORDER(55,55,55);
static wxColour CCPU(80,180,220),CDL(60,200,80),CUL(220,140,40);
static wxColour CRAM(80,180,80),CCACHED(200,160,40);
static wxColour CGREEN(120,200,120),CGREY(150,150,150);

// ─── Helpers ───────────────────────────────────────────────
static string hb(uint64_t b){const char*u[]={"B","KB","MB","GB","TB"};double v=b;int i=0;
    while(v>=1000&&i<4){v/=1000;i++;}ostringstream s;s<<fixed<<setprecision(v<10?1:0)<<v<<" "<<u[i];return s.str();}
static string ss(uint64_t b){return hb(b)+"/s";}
static string sf(double s){int d=s/86400,h=((int)s%86400)/3600,m=((int)s%3600)/60;char b[64];
    if(d)snprintf(b,sizeof(b),"%dd%02d:%02d",d,h,m);else snprintf(b,sizeof(b),"%02d:%02d:%02d",h,m,(int)s%60);return b;}

// ─── Dashboard ─────────────────────────────────────────────
wxBEGIN_EVENT_TABLE(Dashboard,wxScrolledWindow)
    EVT_PAINT(Dashboard::OnPaint)
wxEND_EVENT_TABLE()

Dashboard::Dashboard(wxWindow*p):wxScrolledWindow(p,wxID_ANY){
    state.loadavg[0]=state.loadavg[1]=state.loadavg[2]=0;
    SetBackgroundStyle(wxBG_STYLE_PAINT);SetScrollRate(8,8);
    SetVirtualSize(800,1600);SetBackgroundColour(CBG);
    timer=new wxTimer(this);
    timer->Bind(wxEVT_TIMER,[this](wxTimerEvent&){
        Cpu::collect(false);Mem::collect(false);Net::collect(false);Proc::collect(false);
        RefreshState();Refresh(false);
    });timer->Start(1500);RefreshState();
}

void Dashboard::RefreshState(){
    auto&cpu=Cpu::current_cpu;
    if(cpu.cpu_percent.contains("total"s))state.cpu_total=cpu.cpu_percent.at("total"s);
    state.cpu_name=Cpu::cpuName;state.cpu_freq=Cpu::cpuHz;state.cpu_cores=cpu.core_percent;
    if(!cpu.load_avg.empty()){state.loadavg[0]=cpu.load_avg[0];
        state.loadavg[1]=cpu.load_avg.size()>1?cpu.load_avg[1]:0;
        state.loadavg[2]=cpu.load_avg.size()>2?cpu.load_avg[2]:0;}
    if(Cpu::got_sensors&&!cpu.temp.empty()&&!cpu.temp[0].empty())state.cpu_temp=cpu.temp[0].back();
    if(Cpu::has_battery){auto[pct,w,s,st]=Cpu::current_bat;state.battery_pct=pct;}
    auto&mem=Mem::current_mem;
    state.mem_total=Mem::get_totalMem();state.mem_used=mem.stats["used"];state.mem_avail=mem.stats["available"];
    state.mem_cache=mem.stats["cached"];state.mem_free=mem.stats["free"];
    state.swap_total=mem.stats["swap_total"];state.swap_used=mem.stats["swap_used"];state.swap_free=mem.stats["swap_free"];
    state.disks.clear();for(auto&m:mem.disks_order)if(mem.disks.contains(m))state.disks.push_back({m,mem.disks.at(m)});
    if(Net::current_net.contains(Net::selected_iface)){auto&n=Net::current_net.at(Net::selected_iface);
        if(n.bandwidth.contains("download"s)){state.net_dl=n.bandwidth.at("download"s);
            state.net_dl_speed=n.stat.at("download"s).speed;state.net_dl_total=n.stat.at("download"s).total;
            state.net_dl_avg=n.stat.at("download"s).avg_speed;}
        if(n.bandwidth.contains("upload"s)){state.net_ul=n.bandwidth.at("upload"s);
            state.net_ul_speed=n.stat.at("upload"s).speed;state.net_ul_total=n.stat.at("upload"s).total;
            state.net_ul_avg=n.stat.at("upload"s).avg_speed;}
        state.net_ip=n.ipv4.empty()?n.ipv6:n.ipv4;state.net_connected=n.connected;state.net_iface=Net::selected_iface;}
    state.procs=Proc::current_procs;
}

// ─── Drawing primitives ────────────────────────────────────
void Dashboard::OnPaint(wxPaintEvent&){wxAutoBufferedPaintDC dc(this);DoPaint(dc);}
void Dashboard::Box(wxDC&dc,int x,int y,int w,int h){
    dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(CBOX));dc.DrawRectangle(x,y,w,h);
    dc.SetPen(wxPen(CBORDER));dc.SetBrush(*wxTRANSPARENT_BRUSH);dc.DrawRectangle(x,y,w,h);}
void Dashboard::TitleC(wxDC&dc,int x,int y,int w,const wxString&s){
    dc.SetTextForeground(CTITLE);wxFont f(8,wxFONTFAMILY_TELETYPE,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_BOLD);dc.SetFont(f);
    int tw,th;dc.GetTextExtent(s,&tw,&th);dc.DrawText(s,x+(w-tw)/2,y+2);}
void Dashboard::Text(wxDC&dc,int x,int y,const wxString&s,const wxColour&c,int sz,bool b){
    dc.SetTextForeground(c);dc.SetFont(wxFont(sz,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,b?wxFONTWEIGHT_BOLD:wxFONTWEIGHT_NORMAL));
    dc.DrawText(s,x,y);}
void Dashboard::Bar(wxDC&dc,int x,int y,int w,int h,double p,const wxColour&c){
    p=max(0.0,min(1.0,p));int fw=(int)(w*p);
    if(fw>0){dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(c));dc.DrawRectangle(x,y,fw,h);}
    dc.SetPen(wxPen(CDIV));dc.SetBrush(*wxTRANSPARENT_BRUSH);dc.DrawRectangle(x,y,w,h);}
void Dashboard::Graph(wxDC&dc,int x,int y,int w,int h,const deque<long long>&d,long long mx,const wxColour&c){
    dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(wxColour(18,18,18)));dc.DrawRectangle(x,y,w,h);
    if(d.size()<2)return;long long m=mx?(mx):max(1ll,*max_element(d.begin(),d.end()));m=max(m,1ll);
    int n=d.size();dc.SetPen(wxPen(c,1));double xs=(double)w/max(1,n-1),ys=(double)h/(double)m;
    int lx=-1,ly=-1;
    for(int i=0;i<n;i++){int px=x+(int)(i*xs),py=y+h-(int)(d[i]*ys);py=max(y,min(y+h,py));
        if(lx>=0)dc.DrawLine(lx,ly,px,py);lx=px;ly=py;}
    dc.SetPen(wxPen(CDIV));dc.SetBrush(*wxTRANSPARENT_BRUSH);dc.DrawRectangle(x,y,w,h);}

// ─── Layout: CPU (left) | Network (right) ──────────────────
void Dashboard::DoPaint(wxDC&dc){
    wxSize sz=GetClientSize();
    int W=max(680,sz.x),pad=3,gap=2;
    (void)sz.y;
    dc.SetBackground(wxBrush(CBG));dc.Clear();

    int x=2,y=2,fh=13;
    // CPU takes ~55%, Network takes the rest
    int cpu_w=(W-6)*55/100,net_w=W-6-cpu_w-gap;
    int cpu_x=x,net_x=cpu_x+cpu_w+gap;

    // Row 1: CPU | Network (same height)
    int cpu_h=CalcCpuH(cpu_w,fh);
    int net_h=CalcNetH(net_w,fh);
    int row1_h=max(cpu_h,net_h);

    if(show_cpu)DrawCPU(dc,cpu_x,y,cpu_w,row1_h,fh);
    if(show_net)DrawNet(dc,net_x,y,net_w,row1_h,fh);
    y+=row1_h+gap;

    // Row 2: Memory (full width)
    if(show_mem){int mem_w=W-4;int mh=CalcMemH(mem_w,fh);
        DrawMem(dc,x,y,mem_w,mh,fh);y+=mh+gap;}

    // Row 3: Processes (full width)
    if(show_proc){int pw=W-4;int ph=CalcProcH(pw,fh);
        DrawProc(dc,x,y,pw,ph,fh);y+=ph+gap;}

    int nh=y+8;if(abs(nh-paint_h)>20){paint_h=nh;SetVirtualSize(W,paint_h);}
}

int Dashboard::CalcCpuH(int w,int){int ncores=max(1,(int)state.cpu_cores.size());int cols=max(1,(w-16)/160);int rows=(ncores+cols-1)/cols;return 16+50+3+14+3+rows*9+8;}
int Dashboard::CalcNetH(int w,int){return 16+3+50+14+3+50+14+8;}
int Dashboard::CalcMemH(int w,int){int dcount=min(8,(int)state.disks.size());return 16+4+18+2+14+2+14+9+2+14+2+14+dcount*14+8;}
int Dashboard::CalcProcH(int w,int){int nshow=min(30,(int)state.procs.size());return 16+14+nshow*13+8;}

// ─── CPU Box ──────────────────────────────────────────────
void Dashboard::DrawCPU(wxDC&dc,int x,int y,int w,int h,int fh){
    Box(dc,x,y,w,h);
    int pad=3,gap=2;
    int title_h=16,graph_h=50,stats_h=14,core_h=9;
    int ncores=max(1,(int)state.cpu_cores.size());int cols=max(1,(w-16)/160),rows=(ncores+cols-1)/cols;

    wxString nm=wxString::FromUTF8(state.cpu_name);
#ifdef __linux__
    if(Shared::physical_cores>0){nm+=wxString::Format(" [%ldC]",Shared::physical_cores);
        if(Shared::smt_enabled)nm+=wxString::Format("/%ldT",Shared::coreCount);}
#endif
    TitleC(dc,x,y,w,nm);
    Text(dc,x+4,y+2,"m",CHI,8,1);Text(dc,x+12,y+2,"enu",CTITLE,8);
    Text(dc,x+44,y+2,"p",CHI,8,1);Text(dc,x+52,y+2,"reset",CTITLE,8);
    if(!state.cpu_freq.empty()){Text(dc,x+w-70,y+2,wxString::FromUTF8(state.cpu_freq),CGREEN,8);}
    y+=title_h;

    Graph(dc,x+pad,y,w-2*pad,graph_h,state.cpu_total,100,CCPU);
    long long pct=state.cpu_total.empty()?0:state.cpu_total.back();
    wxString ps=wxString::Format("%lld%%",pct);int tw,th;dc.SetFont(wxFont(18,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_BOLD));
    dc.GetTextExtent(ps,&tw,&th);dc.SetTextForeground(wxColour(230,230,230));
    dc.DrawText(ps,x+(w-tw)/2,y+(graph_h-th)/2);
    y+=graph_h+gap;

    ostringstream si;si<<"Load avg: "<<fixed<<setprecision(2)<<state.loadavg[0]<<" "<<state.loadavg[1]<<" "<<state.loadavg[2];
    si<<"   Up: "<<sf(Tools::system_uptime());
    if(state.cpu_temp>0)si<<"   Temp: "<<state.cpu_temp<<"C";
    if(state.battery_pct>=0)si<<"   BAT: "<<state.battery_pct<<"%";
    Text(dc,x+pad,y,wxString::FromUTF8(si.str()),CMFG,8);y+=stats_h+gap;

    for(int r=0;r<rows;r++){int ry=y+r*9;
        for(int c=0;c<cols;c++){int idx=r*cols+c;if(idx>=ncores)break;
            int cx=x+pad+c*(w/cols),cw2=w/cols-4;
            Text(dc,cx,ry,wxString::Format("C%d",idx),CGREY,7);
            if(idx<(int)state.cpu_cores.size())Graph(dc,cx+22,ry,cw2-60,core_h,state.cpu_cores[idx],100,wxColour(70,150,210));
            double cp=0;if(idx<(int)state.cpu_cores.size()&&!state.cpu_cores[idx].empty())cp=state.cpu_cores[idx].back();
            wxColour cbar=cp>90?wxColour(220,50,50):cp>75?wxColour(240,150,30):wxColour(60,180,75);
            Bar(dc,cx+cw2-42,ry+1,30,core_h-2,cp/100.0,cbar);
            Text(dc,cx+cw2-9,ry+1,wxString::Format("%.0f%%",cp),CMFG,7);}}
}

// ─── Network Box ───────────────────────────────────────────
void Dashboard::DrawNet(wxDC&dc,int x,int y,int w,int h,int fh){
    Box(dc,x,y,w,h);
    int pad=3,gap=2;
    int title_h=16,graph_h=50,stat_h=14;

    string iface=state.net_iface,ip=state.net_ip,conn=state.net_connected?"●":"○";
    wxString nt=wxString::Format("Net  %s  %s  %s",iface,conn,ip);
    TitleC(dc,x,y,w,nt);y+=title_h;

    // Download
    Text(dc,x+pad,y,"▼ DL",CDL,8,1);y+=fh;
    Graph(dc,x+pad,y,w-2*pad,graph_h,state.net_dl,0,CDL);y+=graph_h;
    Text(dc,x+pad,y,wxString::Format("Spd:%s Tot:%s Avg:%s",ss(state.net_dl_speed),hb(state.net_dl_total),ss(state.net_dl_avg)),CMFG,8);
    y+=stat_h+gap;

    // Upload  
    Text(dc,x+pad,y,"▲ UL",CUL,8,1);y+=fh;
    Graph(dc,x+pad,y,w-2*pad,graph_h,state.net_ul,0,CUL);y+=graph_h;
    Text(dc,x+pad,y,wxString::Format("Spd:%s Tot:%s Avg:%s",ss(state.net_ul_speed),hb(state.net_ul_total),ss(state.net_ul_avg)),CMFG,8);
}

// ─── Memory Box ────────────────────────────────────────────
void Dashboard::DrawMem(wxDC&dc,int x,int y,int w,int h,int fh){
    Box(dc,x,y,w,h);
    int pad=3,ram_h=18;
    TitleC(dc,x,y,w,"Memory & Disks");y+=16+4;

    double rp=state.mem_total>0?(double)state.mem_used/state.mem_total:0;
    Text(dc,x+pad,y,wxString::Format("RAM  %s / %s",hb(state.mem_used),hb(state.mem_total)),CMFG,8);
    Text(dc,x+w-44,y,wxString::Format("%.0f%%",rp*100),CMFG,8);y+=14;
    Bar(dc,x+pad,y,w-2*pad,ram_h,rp,CRAM);y+=ram_h+2;
    Text(dc,x+pad,y,wxString::Format("avail:%s  cache:%s  free:%s",hb(state.mem_avail),hb(state.mem_cache),hb(state.mem_free)),CGREY,8);
    y+=14+2;

    if(state.swap_total>0){double sp=(double)state.swap_used/state.swap_total;
        Text(dc,x+pad,y,wxString::Format("Swap %s / %s",hb(state.swap_used),hb(state.swap_total)),CMFG,8);
        Text(dc,x+w-44,y,wxString::Format("%.0f%%",sp*100),CMFG,8);y+=14;
        Bar(dc,x+pad,y,w-2*pad,9,sp,CCACHED);y+=11;
        Text(dc,x+pad,y,wxString::Format("free:%s",hb(state.swap_free)),CGREY,8);y+=14+2;}

    Text(dc,x+pad,y,"Disks",wxColour(140,180,220),8,1);y+=16;
    int dcount=min(8,(int)state.disks.size());
    for(int i=0;i<dcount;i++){auto&[mnt,d]=state.disks[i];(void)mnt;
        Text(dc,x+pad,y,wxString::FromUTF8(d.name),CMFG,8);
        double dp=d.total>0?(double)d.used/d.total:0;
        wxColour dc2=dp>0.9?wxColour(220,50,50):dp>0.75?wxColour(240,150,30):CRAM;
        Bar(dc,x+90,y+2,w-200,10,dp,dc2);
        ostringstream di;di<<hb(d.used)<<"/"<<hb(d.total)<<" ("<<d.used_percent<<"%)";
        Text(dc,x+w-150,y,wxString::FromUTF8(di.str()),CGREY,8);y+=14;}
}

// ─── Proc Box ──────────────────────────────────────────────
void Dashboard::DrawProc(wxDC&dc,int x,int y,int w,int h,int fh){
    Box(dc,x,y,w,h);
    int pad=3,nshow=min(30,(int)state.procs.size()),row_h=13;
    TitleC(dc,x,y,w,wxString::Format("Processes (%zu)",state.procs.size()));y+=16;

    Text(dc,x+pad,y,"PID",CGREY,8,1);Text(dc,x+74,y,"Name",CGREY,8,1);
    Text(dc,x+w-160,y,"CPU%",CGREY,8,1);Text(dc,x+w-100,y,"MEM",CGREY,8,1);
    Text(dc,x+w-260,y,"User",CGREY,8,1);y+=14;

    for(int i=0;i<nshow;i++){auto&p=state.procs[i];
        wxColour tc=p.cpu_p>50?wxColour(255,140,100):p.cpu_p>20?wxColour(240,220,140):CMFG;
        Text(dc,x+pad,y,wxString::Format("%zu",p.pid),tc,8);
        string nm=p.name;if(nm.size()>35)nm=nm.substr(0,32)+"...";Text(dc,x+74,y,wxString::FromUTF8(nm),tc,8);
        Text(dc,x+w-160,y,wxString::Format("%.1f",p.cpu_p),tc,8);
        Text(dc,x+w-100,y,wxString::FromUTF8(hb(p.mem)),tc,8);
        string us=p.user;if(us.size()>12)us=us.substr(0,11);Text(dc,x+w-260,y,wxString::FromUTF8(us),tc,8);
        y+=row_h;}
}

// ─── MainFrame ────────────────────────────────────────────
wxBEGIN_EVENT_TABLE(MainFrame,wxFrame)EVT_CLOSE(MainFrame::OnClose)EVT_KEY_DOWN(MainFrame::OnKeyDown)wxEND_EVENT_TABLE()
MainFrame::MainFrame():wxFrame(nullptr,wxID_ANY,"btop++ GUI",wxDefaultPosition,wxSize(960,800)){
    SetBackgroundColour(CBG);dash=new Dashboard(this);Maximize();}
void MainFrame::TakeScreenshot(const wxString&p){
    wxSize sz=GetClientSize();wxBitmap bmp(sz.x,sz.y);wxMemoryDC mdc(bmp);
    mdc.SetBackground(wxBrush(CBG));mdc.Clear();dash->DoPaint(mdc);mdc.SelectObject(wxNullBitmap);bmp.SaveFile(p,wxBITMAP_TYPE_PNG);}
void MainFrame::OnClose(wxCloseEvent&){Destroy();}
void MainFrame::OnKeyDown(wxKeyEvent&e){int k=e.GetKeyCode();
    if(k=='1'){dash->show_cpu=!dash->show_cpu;dash->Refresh();}
    else if(k=='2'){dash->show_mem=!dash->show_mem;dash->Refresh();}
    else if(k=='3'){dash->show_net=!dash->show_net;dash->Refresh();}
    else if(k=='4'){dash->show_proc=!dash->show_proc;dash->Refresh();}
    else if(k=='S'&&e.ControlDown())TakeScreenshot("/tmp/btop-gui-screenshot.png");
    e.Skip();}

// ─── App ──────────────────────────────────────────────────
bool BtopApp::OnInit(){bool shot=false;for(int i=1;i<argc;i++)if(string(argv[i])=="--screenshot")shot=true;
    try{Shared::init();}catch(const exception&e){wxMessageBox("Init failed: "+string(e.what()),"Error");return false;}
    Cpu::collect(false);Mem::collect(false);Net::collect(false);Proc::collect(false);
    MainFrame*f=new MainFrame();f->Show(true);
    if(shot){wxTimer*st=new wxTimer(f);st->Bind(wxEVT_TIMER,[f,st](wxTimerEvent&){
        f->dash->RefreshState();f->dash->Refresh();f->Update();
        wxTimer*s2=new wxTimer(f);s2->Bind(wxEVT_TIMER,[f,s2](wxTimerEvent&){
            f->TakeScreenshot("/tmp/btop-gui-screenshot.png");f->Close();});s2->Start(500);st->Stop();});st->Start(2000);}
    return true;}
int BtopApp::OnExit(){return 0;}wxIMPLEMENT_APP(BtopApp);
