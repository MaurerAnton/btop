// btop-screenshot: headless PNG renderer using Cairo
// No display needed — renders directly to PNG image surface
#include <cairo.h>
#include <string>
#include <vector>
#include <deque>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <unordered_map>

using namespace std;

// Minimal stubs
namespace Runner{atomic<bool> stopping{false},coreNum_reset{false},active{false},redraw{false};bool pause_output=false;}
namespace Global{extern const string Version="1.4.7";string overlay,exit_error_msg,clock;
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

#include "btop_shared.hpp"
#include "btop_config.hpp"
namespace Cpu{extern cpu_info current_cpu;}
namespace Mem{extern mem_info current_mem;}
namespace Proc{extern vector<proc_info> current_procs;}

// Helpers
static string hb(uint64_t b){const char*u[]={"B","KB","MB","GB","TB"};double v=b;int i=0;
    while(v>=1000&&i<4){v/=1000;i++;}ostringstream s;s<<fixed<<setprecision(v<10?1:0)<<v<<" "<<u[i];return s.str();}
static string ss(uint64_t b){return hb(b)+"/s";}
static string sf(double s){int d=s/86400,h=((int)s%86400)/3600,m=((int)s%3600)/60;char b[64];
    if(d)snprintf(b,sizeof(b),"%dd%02d:%02d",d,h,m);else snprintf(b,sizeof(b),"%02d:%02d:%02d",h,m,(int)s%60);return b;}

// Colors
struct Color { double r,g,b; };
static Color BG{0.086,0.086,0.086};
static Color BOX_BG{0.102,0.102,0.102};
static Color TITLE{0.235,0.706,0.902};
static Color MAIN_FG{0.824,0.824,0.824};
static Color HI_FG{0.392,0.784,0.941};
static Color BORDER{0.216,0.216,0.216};
static Color CPU_C{0.314,0.706,0.863};
static Color DL_C{0.235,0.784,0.314};
static Color UL_C{0.863,0.549,0.157};
static Color RAM_C{0.314,0.706,0.314};
static Color CACHED_C{0.784,0.627,0.157};
static Color DIV_C{0.255,0.255,0.255};
static Color GRAPH_BG{0.071,0.071,0.071};

static void set_color(cairo_t*cr,Color c){cairo_set_source_rgb(cr,c.r,c.g,c.b);}
static void box(cairo_t*cr,double x,double y,double w,double h,Color fill){
    set_color(cr,fill);cairo_rectangle(cr,x,y,w,h);cairo_fill(cr);}
static void border(cairo_t*cr,double x,double y,double w,double h,Color c){
    set_color(cr,c);cairo_set_line_width(cr,1);cairo_rectangle(cr,x,y,w,h);cairo_stroke(cr);}
static void text(cairo_t*cr,double x,double y,const string&s,Color c,int sz=10,bool bold=false){
    set_color(cr,c);cairo_select_font_face(cr,"monospace",CAIRO_FONT_SLANT_NORMAL,bold?CAIRO_FONT_WEIGHT_BOLD:CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr,sz);cairo_move_to(cr,x,y+sz*0.8);cairo_show_text(cr,s.c_str());}
static void text_centered(cairo_t*cr,double x,double y,double w,const string&s,Color c,int sz=10,bool bold=false){
    cairo_text_extents_t te;
    cairo_select_font_face(cr,"monospace",CAIRO_FONT_SLANT_NORMAL,bold?CAIRO_FONT_WEIGHT_BOLD:CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr,sz);cairo_text_extents(cr,s.c_str(),&te);
    text(cr,x+(w-te.width)/2,y,s,c,sz,bold);}
static void hbar(cairo_t*cr,double x,double y,double w,double h,double pct,Color c){
    pct=max(0.0,min(1.0,pct));
    box(cr,x,y,w*pct,h,c);
    border(cr,x,y,w,h,DIV_C);}
static void graph(cairo_t*cr,double x,double y,double w,double h,const deque<long long>&d,long long mx,Color c){
    box(cr,x,y,w,h,GRAPH_BG);
    if(d.size()<2)return;
    long long m=mx;if(mx==0)m=max(1ll,*max_element(d.begin(),d.end()));
    m=max(m,1ll);
    int n=d.size();
    set_color(cr,c);cairo_set_line_width(cr,1.5);
    cairo_move_to(cr,x,y+h-(d[0]*h/m));
    for(int i=1;i<n;i++){
        double px=x+(double)i*w/max(1,n-1);
        double py=y+h-(d[i]*h/m);
        py=max(y,min(y+h,py));
        cairo_line_to(cr,px,py);
    }
    cairo_stroke(cr);
    border(cr,x,y,w,h,DIV_C);
}

int main(){
    // Init backend
    Config::unlock();Config::lock();
    Shared::init();
    Cpu::collect(false);Mem::collect(false);Net::collect(false);Proc::collect(false);

    // Second collect for speed deltas
    Cpu::collect(false);Mem::collect(false);Net::collect(false);Proc::collect(false);

    int W=960,H=1400;
    cairo_surface_t*surf=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,W,H);
    cairo_t*cr=cairo_create(surf);

    // Background
    box(cr,0,0,W,H,BG);

    auto&cpu=Cpu::current_cpu;
    auto&mem=Mem::current_mem;
    auto&procs=Proc::current_procs;
    double y=4,x=4,w=W-8,fh=13;

    // ─── CPU ───
    {
        int ncores=max(1,(int)cpu.core_percent.size());
        int cols=max(1,(int)(w-16)/160),rows=(ncores+cols-1)/cols;
        int title_h=16,graph_h=50,stats_h=14,core_h=10,gap=3;
        int h=title_h+graph_h+gap+stats_h+gap+rows*(core_h+gap)+8;
        box(cr,x,y,w,h,BOX_BG);border(cr,x,y,w,h,BORDER);

        string name=Cpu::cpuName;
#ifdef __linux__
        if(Shared::physical_cores>0){name+=" ["+to_string(Shared::physical_cores)+"C]";
            if(Shared::smt_enabled)name+="/"+to_string(Shared::coreCount)+"T";}
#endif
        text_centered(cr,x,y,w,name,TITLE,10,true);
        text(cr,x+4,y+2,"m",HI_FG,8,true);text(cr,x+12,y+2,"enu",TITLE,8,false);
        text(cr,x+44,y+2,"p",HI_FG,8,true);text(cr,x+52,y+2,"reset",TITLE,8,false);
        if(!Cpu::cpuHz.empty()){text(cr,x+w-70,y+2,Cpu::cpuHz,Color{0.47,0.78,0.47},8);}
        y+=title_h;

        // Graph
        if(cpu.cpu_percent.contains("total"s))
            graph(cr,x+4,y,w-8,graph_h,cpu.cpu_percent.at("total"s),100,CPU_C);
        long long pct=cpu.cpu_percent.contains("total"s)&&!cpu.cpu_percent.at("total"s).empty()?cpu.cpu_percent.at("total"s).back():0;
        text_centered(cr,x,y+graph_h/2,w,to_string(pct)+"%",MAIN_FG,18,true);
        y+=graph_h+gap;

        // Stats
        ostringstream si;si<<"Load avg: "<<fixed<<setprecision(2);
        if(!cpu.load_avg.empty())si<<cpu.load_avg[0]<<" "<<(cpu.load_avg.size()>1?to_string(cpu.load_avg[1]):"")<<" "<<(cpu.load_avg.size()>2?to_string(cpu.load_avg[2]):"");
        si<<"   Up: "<<sf(Tools::system_uptime());
        text(cr,x+4,y,si.str(),MAIN_FG,8);y+=stats_h+gap;

        // Per-core
        for(int r=0;r<rows;r++){
            double ry=y+r*(core_h+gap);
            for(int c=0;c<cols;c++){
                int idx=r*cols+c;if(idx>=ncores)break;
                double cx2=x+4+c*(w/cols),cw2=w/cols-4;
                text(cr,cx2,ry,"C"+to_string(idx),Color{0.59,0.59,0.59},7);
                if(idx<(int)cpu.core_percent.size())
                    graph(cr,cx2+22,ry,cw2-60,core_h,cpu.core_percent[idx],100,Color{0.275,0.588,0.824});
                double cp=0;if(idx<(int)cpu.core_percent.size()&&!cpu.core_percent[idx].empty())cp=cpu.core_percent[idx].back();
                Color cc=cp>90?Color{0.86,0.2,0.2}:cp>75?Color{0.94,0.59,0.12}:Color{0.235,0.706,0.294};
                hbar(cr,cx2+cw2-42,ry+2,30,core_h-2,cp/100.0,cc);
                text(cr,cx2+cw2-8,ry+1,to_string((int)cp)+"%",MAIN_FG,7);
            }
        }
        y+=rows*(core_h+gap)+6;
    }

    // ─── Memory ───
    {
        int title_h=16,ram_h=18,detail_h=14,disk_h=14;
        int dcount=min(8,(int)mem.disks_order.size());
        int h=title_h+4+ram_h+2+detail_h+2+14+ram_h/2+detail_h+2+16+dcount*disk_h+8;
        box(cr,x,y,w,h,BOX_BG);border(cr,x,y,w,h,BORDER);
        text_centered(cr,x,y,w,"Memory & Disks",TITLE,10,true);
        y+=title_h+4;

        uint64_t tot=Mem::get_totalMem();
        uint64_t used=mem.stats["used"],avail=mem.stats["available"],cache=mem.stats["cached"],free=mem.stats["free"];
        double rp=tot>0?(double)used/tot:0;
        text(cr,x+4,y,"RAM  "+hb(used)+" / "+hb(tot),MAIN_FG,8);
        text(cr,x+w-44,y,to_string((int)(rp*100))+"%",MAIN_FG,8);
        y+=14;
        hbar(cr,x+4,y,w-8,ram_h,rp,RAM_C);
        y+=ram_h+2;
        text(cr,x+4,y,"avail: "+hb(avail)+"  cache: "+hb(cache)+"  free: "+hb(free),Color{0.59,0.59,0.59},8);
        y+=detail_h+2;

        uint64_t su=mem.stats["swap_used"],stot=mem.stats["swap_total"],sfree=mem.stats["swap_free"];
        if(stot>0){
            double sp=(double)su/stot;
            text(cr,x+4,y,"Swap "+hb(su)+" / "+hb(stot),MAIN_FG,8);
            text(cr,x+w-44,y,to_string((int)(sp*100))+"%",MAIN_FG,8);
            y+=14;hbar(cr,x+4,y,w-8,ram_h/2,sp,CACHED_C);
            y+=ram_h/2+2;
            text(cr,x+4,y,"free: "+hb(sfree),Color{0.59,0.59,0.59},8);
            y+=detail_h+2;
        }

        text(cr,x+4,y,"Disks",Color{0.55,0.71,0.86},8,true);y+=16;
        for(int i=0;i<dcount;i++){
            auto&mnt=mem.disks_order[i];
            if(!mem.disks.contains(mnt))continue;
            auto&d=mem.disks.at(mnt);
            double dp=d.total>0?(double)d.used/d.total:0;
            Color dc=dp>0.9?Color{0.86,0.2,0.2}:dp>0.75?Color{0.94,0.59,0.12}:RAM_C;
            text(cr,x+4,y,d.name,MAIN_FG,8);
            hbar(cr,x+90,y+2,w-200,disk_h-2,dp,dc);
            ostringstream di;di<<hb(d.used)<<"/"<<hb(d.total)<<" ("<<d.used_percent<<"%)";
            text(cr,x+w-150,y,di.str(),Color{0.59,0.59,0.59},8);
            y+=disk_h;
        }
        y+=8;
    }

    // ─── Network ───
    {
        int title_h=16,graph_h=50,stat_h=14;
        int h=title_h+4+graph_h+stat_h+4+graph_h+stat_h+8;
        box(cr,x,y,w,h,BOX_BG);border(cr,x,y,w,h,BORDER);

        string iface=Net::selected_iface;
        string ip,conn="O";
        if(Net::current_net.contains(iface)){
            auto&n=Net::current_net.at(iface);
            ip=n.ipv4.empty()?n.ipv6:n.ipv4;
            conn=n.connected?"@":"O";
        }
        text_centered(cr,x,y,w,"Network  "+iface+"  "+conn+"  "+ip,TITLE,9,true);
        y+=title_h+4;

        if(Net::current_net.contains(iface)){
            auto&n=Net::current_net.at(iface);
            if(n.bandwidth.contains("download"s)){
                text(cr,x+4,y,"v Download",DL_C,8);y+=12;
                graph(cr,x+4,y,w-8,graph_h,n.bandwidth.at("download"s),0,DL_C);
                y+=graph_h;
                auto&s=n.stat.at("download"s);
                text(cr,x+4,y,"Speed: "+ss(s.speed)+"    Total: "+hb(s.total)+"    5m avg: "+ss(s.avg_speed?s.avg_speed:0),MAIN_FG,8);
                y+=stat_h+4;
            }
            if(n.bandwidth.contains("upload"s)){
                text(cr,x+4,y,"^ Upload",UL_C,8);y+=12;
                graph(cr,x+4,y,w-8,graph_h,n.bandwidth.at("upload"s),0,UL_C);
                y+=graph_h;
                auto&s=n.stat.at("upload"s);
                text(cr,x+4,y,"Speed: "+ss(s.speed)+"    Total: "+hb(s.total)+"    5m avg: "+ss(s.avg_speed?s.avg_speed:0),MAIN_FG,8);
                y+=stat_h;
            }
        }
        y+=8;
    }

    // ─── Processes ───
    {
        int nshow=min(30,(int)procs.size());
        int title_h=16,header_h=14,row_h=13;
        int h=title_h+header_h+nshow*row_h+8;
        box(cr,x,y,w,h,BOX_BG);border(cr,x,y,w,h,BORDER);
        text_centered(cr,x,y,w,"Processes ("+to_string(procs.size())+")",TITLE,10,true);
        y+=title_h;

        text(cr,x+4,y,"PID",Color{0.59,0.59,0.59},8,true);
        text(cr,x+74,y,"Name",Color{0.59,0.59,0.59},8,true);
        text(cr,x+w-160,y,"CPU%",Color{0.59,0.59,0.59},8,true);
        text(cr,x+w-100,y,"MEM",Color{0.59,0.59,0.59},8,true);
        text(cr,x+w-260,y,"User",Color{0.59,0.59,0.59},8,true);
        y+=header_h;

        for(int i=0;i<nshow;i++){
            auto&p=procs[i];
            Color tc=p.cpu_p>50?Color{1,0.55,0.39}:p.cpu_p>20?Color{0.94,0.86,0.55}:MAIN_FG;
            text(cr,x+4,y,to_string(p.pid),tc,8);
            string nm=p.name;if(nm.size()>35)nm=nm.substr(0,32)+"...";
            text(cr,x+74,y,nm,tc,8);
            ostringstream cp;cp<<fixed<<setprecision(1)<<p.cpu_p;
            text(cr,x+w-160,y,cp.str(),tc,8);
            text(cr,x+w-100,y,hb(p.mem),tc,8);
            string us=p.user;if(us.size()>12)us=us.substr(0,11);
            text(cr,x+w-260,y,us,tc,8);
            y+=row_h;
        }
    }

    cairo_surface_write_to_png(surf,"/tmp/btop-screenshot.png");
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return 0;
}
