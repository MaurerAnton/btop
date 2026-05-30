// btop-gui v4 — close visual match to terminal btop
// Single dashboard, painted boxes with ANSI-style borders, keyboard toggles

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

// ─── Color scheme (exact btop default theme) ────────────────
static wxColour C_BG(22,22,22);           // terminal bg
static wxColour C_BOX_BG(26,26,26);       // inside boxes
static wxColour C_TITLE(60,180,230);      // title_fg / cyan-ish
static wxColour C_HI_FG(100,200,240);     // hi_fg / bright cyan
static wxColour C_MAIN_FG(210,210,210);   // main_fg
static wxColour C_DIV(65,65,65);          // div_line
static wxColour C_BORDER(55,55,55);       // box border
static wxColour C_GRAPH_TEXT(120,120,120);
static wxColour C_CPU(80,180,220);        // cpu graph color
static wxColour C_DL(60,200,80);          // download
static wxColour C_UL(220,140,40);         // upload
static wxColour C_RAM(80,180,80);         // used/mem
static wxColour C_AVAIL(60,150,200);      // available
static wxColour C_CACHED(200,160,40);     // cached
static wxColour C_FREE(100,100,100);      // free
static wxColour C_TEMP(220,60,60);

// ─── Helpers ───────────────────────────────────────────────
static string hb(uint64_t b){const char*u[]={"B","KB","MB","GB","TB"};double v=b;int i=0;
    while(v>=1000&&i<4){v/=1000;i++;}ostringstream s;s<<fixed<<setprecision(v<10?1:0)<<v<<" "<<u[i];return s.str();}
static string ss(uint64_t b){return hb(b)+"/s";}
static string sf(double s){int d=s/86400,h=((int)s%86400)/3600,m=((int)s%3600)/60;char b[64];
    if(d)snprintf(b,sizeof(b),"%dd%02d:%02d",d,h,m);else snprintf(b,sizeof(b),"%02d:%02d:%02d",h,m,(int)s%60);return b;}

// ─── Dashboard ─────────────────────────────────────────────
wxBEGIN_EVENT_TABLE(Dashboard, wxScrolledWindow)
    EVT_PAINT(Dashboard::OnPaint)
wxEND_EVENT_TABLE()

Dashboard::Dashboard(wxWindow* p) : wxScrolledWindow(p, wxID_ANY){
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetScrollRate(8,8);
    SetVirtualSize(800,1600);
    SetBackgroundColour(C_BG);
    timer=new wxTimer(this);
    timer->Bind(wxEVT_TIMER,[this](wxTimerEvent&){
        Cpu::collect(false);Mem::collect(false);Net::collect(false);Proc::collect(false);
        RefreshState();
        Refresh(false);
    });
    timer->Start(1500);
    RefreshState();
}

void Dashboard::RefreshState(){
    auto&cpu=Cpu::current_cpu;
    if(cpu.cpu_percent.contains("total"s))state.cpu_total=cpu.cpu_percent.at("total"s);
    state.cpu_name=Cpu::cpuName;state.cpu_freq=Cpu::cpuHz;
    state.cpu_cores=cpu.core_percent;
    if(!cpu.load_avg.empty()){state.loadavg[0]=cpu.load_avg[0];
        state.loadavg[1]=cpu.load_avg.size()>1?cpu.load_avg[1]:0;
        state.loadavg[2]=cpu.load_avg.size()>2?cpu.load_avg[2]:0;}
    if(Cpu::got_sensors&&!cpu.temp.empty()&&!cpu.temp[0].empty())
        state.cpu_temp=cpu.temp[0].back();
    if(Cpu::has_battery){auto[pct,w,secs,st]=Cpu::current_bat;state.battery_pct=pct;state.battery_status=st;}
    auto&mem=Mem::current_mem;
    state.mem_total=Mem::get_totalMem();state.mem_used=mem.stats["used"];state.mem_avail=mem.stats["available"];
    state.mem_cache=mem.stats["cached"];state.mem_free=mem.stats["free"];
    state.swap_total=mem.stats["swap_total"];state.swap_used=mem.stats["swap_used"];state.swap_free=mem.stats["swap_free"];
    state.disks.clear();
    for(auto&m:mem.disks_order)if(mem.disks.contains(m))state.disks.push_back({m,mem.disks.at(m)});
    if(Net::current_net.contains(Net::selected_iface)){
        auto&n=Net::current_net.at(Net::selected_iface);
        if(n.bandwidth.contains("download"s)){state.net_dl=n.bandwidth.at("download"s);
            state.net_dl_speed=n.stat.at("download"s).speed;state.net_dl_total=n.stat.at("download"s).total;
            state.net_dl_avg=n.stat.at("download"s).avg_speed;}
        if(n.bandwidth.contains("upload"s)){state.net_ul=n.bandwidth.at("upload"s);
            state.net_ul_speed=n.stat.at("upload"s).speed;state.net_ul_total=n.stat.at("upload"s).total;
            state.net_ul_avg=n.stat.at("upload"s).avg_speed;}
        state.net_ip=n.ipv4.empty()?n.ipv6:n.ipv4;state.net_connected=n.connected;state.net_iface=Net::selected_iface;}
    state.procs=Proc::current_procs;
}

// ─── Drawing ───────────────────────────────────────────────
void Dashboard::OnPaint(wxPaintEvent&){wxAutoBufferedPaintDC dc(this);DoPaint(dc);}

void Dashboard::DoPaint(wxDC&dc){
    wxSize sz=GetClientSize();
    int W=max(600,sz.x);
    (void)sz.y; // unused height — scrolled window handles it
    dc.SetBackground(wxBrush(C_BG));dc.Clear();

    int y=2,x=2,w=W-4;
    int fh=13; // font height approx

    if(show_cpu){int hy=DrawCPU(dc,y,x,w,fh);y=hy;}
    if(show_mem){int hy=DrawMem(dc,y,x,w,fh);y=hy;}
    if(show_net){int hy=DrawNet(dc,y,x,w,fh);y=hy;}
    if(show_proc){int hy=DrawProc(dc,y,x,w,fh);y=hy;}

    int nh=y+8;
    if(abs(nh-paint_h)>20){paint_h=nh;SetVirtualSize(W,paint_h);}
}

// ─── Box border ───────────────────────────────────────────
void Dashboard::BoxBorder(wxDC&dc,int x,int y,int w,int h,const wxColour&c){
    dc.SetPen(wxPen(c));dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(x,y,w,h);
}

void Dashboard::BoxTitle(wxDC&dc,int x,int y,int w,const wxString&s,const wxColour&c){
    dc.SetTextForeground(c);
    wxFont f(8,wxFONTFAMILY_TELETYPE,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_BOLD);
    dc.SetFont(f);
    int tw,th;dc.GetTextExtent(s,&tw,&th);
    dc.DrawText(s,x+(w-tw)/2,y+2);
}

// Draw a horizontal color bar (for mem/swap gauges)
void Dashboard::HBar(wxDC&dc,int x,int y,int w,int h,double pct,const wxColour&c){
    pct=max(0.0,min(1.0,pct));
    int fw=(int)(w*pct);if(fw>0){dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(c));dc.DrawRectangle(x,y,fw,h);}
    dc.SetPen(wxPen(C_DIV));dc.SetBrush(*wxTRANSPARENT_BRUSH);dc.DrawRectangle(x,y,w,h);
}

// Mini line graph
void Dashboard::MiniGraph(wxDC&dc,int x,int y,int w,int h,const deque<long long>&d,long long mx,const wxColour&c,int thick){
    if(d.size()<2)return;
    long long m=max(1ll,mx);
    if(mx==0){m=max(1ll,*max_element(d.begin(),d.end()));}
    int n=d.size();dc.SetPen(wxPen(c,thick));
    double xs=(double)w/max(1,n-1),ys=(double)h/(double)m;
    int lx=-1,ly=-1;
    for(int i=0;i<n;i++){
        int px=x+(int)(i*xs),py=y+h-(int)(d[i]*ys);
        py=max(y,min(y+h,py));if(lx>=0)dc.DrawLine(lx,ly,px,py);lx=px;ly=py;
    }
    dc.SetPen(wxPen(C_DIV,1));dc.SetBrush(*wxTRANSPARENT_BRUSH);dc.DrawRectangle(x,y,w,h);
}

// ─── CPU Box ──────────────────────────────────────────────
int Dashboard::DrawCPU(wxDC&dc,int y,int x,int w,int fh){
    int pad=3,gap=2;
    int title_h=fh+4;
    int graph_h=50,stats_h=fh+2,core_row_h=10;
    int ncores=max(1,(int)state.cpu_cores.size());
    int cols=max(1,(w-16)/160),rows=(ncores+cols-1)/cols;
    int core_area=rows*(core_row_h+gap)+pad;
    int h=title_h+graph_h+gap+stats_h+gap+core_area+pad;

    // Background
    dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(C_BOX_BG));
    dc.DrawRectangle(x,y,w,h);
    BoxBorder(dc,x,y,w,h,C_BORDER);

    // Title bar
    wxString nm=wxString::FromUTF8(state.cpu_name);
#ifdef __linux__
    if(Shared::physical_cores>0){nm+=wxString::Format(" [%ldC]",Shared::physical_cores);
        if(Shared::smt_enabled)nm+=wxString::Format("/%ldT",Shared::coreCount);}
#endif
    BoxTitle(dc,x,y,w,nm,C_TITLE);

    dc.SetFont(wxFont(8,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_BOLD));
    // [m]enu [p]reset
    dc.SetTextForeground(C_HI_FG);dc.DrawText("m",x+4,y+3);
    dc.SetTextForeground(C_TITLE);dc.DrawText("enu",x+12,y+3);
    dc.SetTextForeground(C_HI_FG);dc.DrawText("p",x+44,y+3);
    dc.SetTextForeground(C_TITLE);dc.DrawText("reset",x+52,y+3);

    // Freq on right
    if(!state.cpu_freq.empty()){dc.SetTextForeground(wxColour(120,200,120));
        int tw,th;dc.GetTextExtent(wxString::FromUTF8(state.cpu_freq),&tw,&th);
        dc.DrawText(wxString::FromUTF8(state.cpu_freq),x+w-tw-4,y+3);}

    y+=title_h;

    // CPU total graph
    int gx=x+pad,gw=w-2*pad;
    dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(wxColour(18,18,18)));
    dc.DrawRectangle(gx,y,gw,graph_h);
    MiniGraph(dc,gx,y,gw,graph_h,state.cpu_total,100,C_CPU,2);

    // Big CPU% in center
    long long pct=state.cpu_total.empty()?0:state.cpu_total.back();
    dc.SetTextForeground(wxColour(230,230,230));
    wxFont bf(18,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_BOLD);dc.SetFont(bf);
    wxString ps=wxString::Format("%lld%%",pct);int tw,th;dc.GetTextExtent(ps,&tw,&th);
    dc.DrawText(ps,gx+(gw-tw)/2,y+(graph_h-th)/2);

    y+=graph_h+gap;

    // Stats line: load + uptime + temp + battery
    dc.SetFont(wxFont(8,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_NORMAL));
    ostringstream si;si<<"Load avg: "<<fixed<<setprecision(2)<<state.loadavg[0]<<" "<<state.loadavg[1]<<" "<<state.loadavg[2];
    si<<"   Up: "<<sf(Tools::system_uptime());
    if(state.cpu_temp>0)si<<"   Temp: "<<state.cpu_temp<<"°C";
    if(state.battery_pct>=0)si<<"   BAT: "<<state.battery_pct<<"%";
    dc.SetTextForeground(C_MAIN_FG);dc.DrawText(wxString::FromUTF8(si.str()),x+pad,y);
    y+=stats_h+gap;

    // Per-core rows
    for(int r=0;r<rows;r++){
        int ry=y+r*(core_row_h+gap);
        for(int c=0;c<cols;c++){
            int idx=r*cols+c;if(idx>=ncores)break;
            int cx=x+pad+c*(w/cols),cw2=w/cols-4;
            dc.SetFont(wxFont(7,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_NORMAL));
            dc.SetTextForeground(wxColour(150,150,150));
            dc.DrawText(wxString::Format("C%d",idx),cx,ry);
            // Mini graph
            int mw=cw2-60;
            if(idx<(int)state.cpu_cores.size())
                MiniGraph(dc,cx+22,ry,mw,core_row_h,state.cpu_cores[idx],100,wxColour(70,150,210),1);
            // Bar + %
            double cp=0;if(idx<(int)state.cpu_cores.size()&&!state.cpu_cores[idx].empty())cp=state.cpu_cores[idx].back();
            wxColour cbar=cp>90?wxColour(220,50,50):cp>75?wxColour(240,150,30):wxColour(60,180,75);
            HBar(dc,cx+cw2-42,ry+2,30,core_row_h-4,cp/100.0,cbar);
            dc.SetTextForeground(C_MAIN_FG);
            dc.SetFont(wxFont(7,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_NORMAL));
            dc.DrawText(wxString::Format("%.0f%%",cp),cx+cw2-8,ry+1);
        }
    }
    return y+core_area+pad;
}

// ─── Memory Box ───────────────────────────────────────────
int Dashboard::DrawMem(wxDC&dc,int y,int x,int w,int fh){
    int pad=3,gap=3;
    int title_h=fh+4,ram_h=18,detail_h=fh+2,disk_row_h=14;
    int dcount=min(8,(int)state.disks.size());
    int h=title_h+gap+ram_h+gap+detail_h+gap+fh+12+ram_h/2+detail_h+gap+fh+gap+dcount*disk_row_h+pad;

    dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(C_BOX_BG));
    dc.DrawRectangle(x,y,w,h);
    BoxBorder(dc,x,y,w,h,C_BORDER);
    BoxTitle(dc,x,y,w,"Memory & Disks",C_TITLE);
    y+=title_h+gap;

    dc.SetFont(wxFont(8,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_NORMAL));
    // RAM
    double rp=state.mem_total>0?(double)state.mem_used/state.mem_total:0;
    dc.SetTextForeground(C_MAIN_FG);
    dc.DrawText(wxString::Format("RAM  %s / %s",hb(state.mem_used),hb(state.mem_total)),x+pad,y);
    dc.DrawText(wxString::Format("%.0f%%",rp*100),x+w-40,y);
    y+=fh;
    HBar(dc,x+pad,y,w-2*pad,ram_h,rp,C_RAM);
    y+=ram_h+2;
    dc.SetTextForeground(wxColour(150,150,150));
    dc.DrawText(wxString::Format("avail: %s  cache: %s  free: %s",hb(state.mem_avail),hb(state.mem_cache),hb(state.mem_free)),x+pad,y);
    y+=detail_h+gap;

    // Swap
    if(state.swap_total>0){
        double sp=(double)state.swap_used/state.swap_total;
        dc.SetTextForeground(C_MAIN_FG);
        dc.DrawText(wxString::Format("Swap %s / %s",hb(state.swap_used),hb(state.swap_total)),x+pad,y);
        dc.DrawText(wxString::Format("%.0f%%",sp*100),x+w-40,y);
        y+=fh;
        HBar(dc,x+pad,y,w-2*pad,ram_h/2,sp,C_CACHED);
        y+=ram_h/2+2;
        dc.SetTextForeground(wxColour(150,150,150));
        dc.DrawText(wxString::Format("free: %s",hb(state.swap_free)),x+pad,y);
        y+=detail_h+gap;
    }

    // Disks header
    dc.SetFont(wxFont(8,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_BOLD));
    dc.SetTextForeground(wxColour(140,180,220));
    dc.DrawText("Disks",x+pad,y);
    dc.SetFont(wxFont(8,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_NORMAL));
    y+=fh+gap;

    for(int i=0;i<dcount;i++){
        auto&[mnt,d]=state.disks[i];(void)mnt;
        dc.SetTextForeground(C_MAIN_FG);
        dc.DrawText(wxString::FromUTF8(d.name),x+pad,y);
        double dp=d.total>0?(double)d.used/d.total:0;
        wxColour dc2=dp>0.9?wxColour(220,50,50):dp>0.75?wxColour(240,150,30):C_RAM;
        HBar(dc,x+90,y+2,w-200,disk_row_h-4,dp,dc2);
        ostringstream di;di<<hb(d.used)<<"/"<<hb(d.total)<<" ("<<d.used_percent<<"%)";
        dc.SetTextForeground(wxColour(150,150,150));
        dc.DrawText(wxString::FromUTF8(di.str()),x+w-150,y);
        y+=disk_row_h;
    }
    return y+pad;
}

// ─── Network Box ──────────────────────────────────────────
int Dashboard::DrawNet(wxDC&dc,int y,int x,int w,int fh){
    int pad=3,gap=3;
    int title_h=fh+4,graph_h=50,stat_h=fh+2;
    int h=title_h+gap+graph_h+stat_h+gap+graph_h+stat_h+pad;

    dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(C_BOX_BG));
    dc.DrawRectangle(x,y,w,h);
    BoxBorder(dc,x,y,w,h,C_BORDER);

    wxString nt=wxString::Format("Network  %s  %s  %s",
        wxString::FromUTF8(state.net_iface),
        state.net_connected?"●":"○",
        wxString::FromUTF8(state.net_ip));
    BoxTitle(dc,x,y,w,nt,C_TITLE);
    y+=title_h+gap;

    dc.SetFont(wxFont(8,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_NORMAL));

    // Download
    dc.SetTextForeground(C_DL);dc.DrawText("▼ Download",x+pad,y);y+=fh;
    dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(wxColour(18,18,18)));
    dc.DrawRectangle(x+pad,y,w-2*pad,graph_h);
    MiniGraph(dc,x+pad,y,w-2*pad,graph_h,state.net_dl,0,C_DL,1);
    y+=graph_h;
    dc.SetTextForeground(C_MAIN_FG);
    dc.DrawText(wxString::Format("Speed: %s    Total: %s    5m avg: %s",
        ss(state.net_dl_speed),hb(state.net_dl_total),ss(state.net_dl_avg)),x+pad,y);
    y+=stat_h+gap;

    // Upload
    dc.SetTextForeground(C_UL);dc.DrawText("▲ Upload",x+pad,y);y+=fh;
    dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(wxColour(18,18,18)));
    dc.DrawRectangle(x+pad,y,w-2*pad,graph_h);
    MiniGraph(dc,x+pad,y,w-2*pad,graph_h,state.net_ul,0,C_UL,1);
    y+=graph_h;
    dc.SetTextForeground(C_MAIN_FG);
    dc.DrawText(wxString::Format("Speed: %s    Total: %s    5m avg: %s",
        ss(state.net_ul_speed),hb(state.net_ul_total),ss(state.net_ul_avg)),x+pad,y);
    y+=stat_h;
    return y+pad;
}

// ─── Proc Box ─────────────────────────────────────────────
int Dashboard::DrawProc(wxDC&dc,int y,int x,int w,int fh){
    int pad=3,row_h=fh+2;
    int nshow=min(30,(int)state.procs.size());
    int title_h=fh+4,header_h=fh+2;
    int h=title_h+header_h+nshow*row_h+pad;

    dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(C_BOX_BG));
    dc.DrawRectangle(x,y,w,h);
    BoxBorder(dc,x,y,w,h,C_BORDER);
    BoxTitle(dc,x,y,w,wxString::Format("Processes (%zu)",state.procs.size()),C_TITLE);
    y+=title_h;

    // Headers
    dc.SetFont(wxFont(8,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_BOLD));
    dc.SetTextForeground(wxColour(150,150,150));
    int cx=x+pad,cpu_x=x+w-160,mem_x=x+w-100,usr_x=x+w-260;
    dc.DrawText("PID",cx,y);dc.DrawText("Name",cx+70,y);dc.DrawText("CPU%",cpu_x,y);
    dc.DrawText("MEM",mem_x,y);dc.DrawText("User",usr_x-20,y);
    y+=header_h;

    dc.SetFont(wxFont(8,wxFONTFAMILY_DEFAULT,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_NORMAL));
    for(int i=0;i<nshow;i++){
        auto&p=state.procs[i];
        wxColour tc=p.cpu_p>50?wxColour(255,140,100):p.cpu_p>20?wxColour(240,220,140):C_MAIN_FG;
        dc.SetTextForeground(tc);
        dc.DrawText(wxString::Format("%zu",p.pid),cx,y);
        string nm=p.name;if(nm.size()>35)nm=nm.substr(0,32)+"...";
        dc.DrawText(wxString::FromUTF8(nm),cx+70,y);
        dc.DrawText(wxString::Format("%.1f",p.cpu_p),cpu_x,y);
        dc.DrawText(wxString::FromUTF8(hb(p.mem)),mem_x,y);
        string us=p.user;if(us.size()>12)us=us.substr(0,11);
        dc.DrawText(wxString::FromUTF8(us),usr_x-20,y);
        y+=row_h;
    }
    return y+pad;
}

// ─── MainFrame ────────────────────────────────────────────
wxBEGIN_EVENT_TABLE(MainFrame,wxFrame)
    EVT_CLOSE(MainFrame::OnClose)
    EVT_KEY_DOWN(MainFrame::OnKeyDown)
wxEND_EVENT_TABLE()

MainFrame::MainFrame():wxFrame(nullptr,wxID_ANY,"btop++ GUI",wxDefaultPosition,wxSize(960,800)){
    SetBackgroundColour(C_BG);
    dash=new Dashboard(this);
    Maximize();
}

void MainFrame::TakeScreenshot(const wxString&path){
    wxSize sz=GetClientSize();
    wxBitmap bmp(sz.x,sz.y);
    wxMemoryDC mdc(bmp);
    mdc.SetBackground(wxBrush(C_BG));mdc.Clear();
    dash->DoPaint(mdc);
    mdc.SelectObject(wxNullBitmap);
    bmp.SaveFile(path,wxBITMAP_TYPE_PNG);
}

void MainFrame::OnClose(wxCloseEvent&){Destroy();}
void MainFrame::OnKeyDown(wxKeyEvent&evt){
    int k=evt.GetKeyCode();
    if(k=='1'){dash->show_cpu=!dash->show_cpu;dash->Refresh();}
    else if(k=='2'){dash->show_mem=!dash->show_mem;dash->Refresh();}
    else if(k=='3'){dash->show_net=!dash->show_net;dash->Refresh();}
    else if(k=='4'){dash->show_proc=!dash->show_proc;dash->Refresh();}
    else if(k=='S'&&evt.ControlDown())TakeScreenshot("/tmp/btop-gui-screenshot.png");
    evt.Skip();
}

// ─── App ──────────────────────────────────────────────────
bool BtopApp::OnInit(){
    bool shot=false;for(int i=1;i<argc;i++)if(string(argv[i])=="--screenshot")shot=true;
    try{Shared::init();}catch(const exception&e){wxMessageBox("Init failed: "+string(e.what()),"Error");return false;}
    Cpu::collect(false);Mem::collect(false);Net::collect(false);Proc::collect(false);
    MainFrame*f=new MainFrame();f->Show(true);
    if(shot){wxTimer*st=new wxTimer(f);st->Bind(wxEVT_TIMER,[f,st](wxTimerEvent&){
        f->dash->RefreshState();f->dash->Refresh();f->Update();
        wxTimer*s2=new wxTimer(f);s2->Bind(wxEVT_TIMER,[f,s2](wxTimerEvent&){
            f->TakeScreenshot("/tmp/btop-gui-screenshot.png");f->Close();
        });s2->Start(500);st->Stop();
    });st->Start(2000);}
    return true;
}
int BtopApp::OnExit(){return 0;}
wxIMPLEMENT_APP(BtopApp);
