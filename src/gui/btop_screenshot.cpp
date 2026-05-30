// btop-screenshot v2 — terminal-accurate grid layout (CPU|Net, Mem full, Proc full)
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

namespace Runner{atomic<bool> stopping{false},coreNum_reset{false},active{false},redraw{false};bool pause_output=false;}
namespace Global{extern const string Version="1.4.7";string overlay,exit_error_msg,clock;
    atomic<bool> resized{false},init_conf{true},quitting{false};uid_t real_uid=0,set_uid=0;const vector<array<string,2>> Banner_src;}
namespace Cpu{int width=100,min_width=60,min_height=8;}
namespace Mem{int width=300,min_width=36,min_height=6;bool redraw=true;}
namespace Net{int width=300,min_width=36,min_height=6;bool redraw=true;}
namespace Proc{int width=300,min_width=44,min_height=16;bool redraw=true,shown=true;int select_max=1,selected_pid=0,start=0,selected=0,selected_depth=0;string selected_name;}
namespace Gpu{int width=100,min_width=36,count=0,shown=0;}namespace Menu{bool active=false,redraw=false;}
void clean_quit(int){}namespace Input{unordered_map<string,array<int,4>> mouse_mappings;}

#include "btop_shared.hpp"
#include "btop_config.hpp"
namespace Cpu{extern cpu_info current_cpu;}namespace Mem{extern mem_info current_mem;}namespace Proc{extern vector<proc_info> current_procs;}

static string hb(uint64_t b){const char*u[]={"B","KB","MB","GB","TB"};double v=b;int i=0;
    while(v>=1000&&i<4){v/=1000;i++;}ostringstream s;s<<fixed<<setprecision(v<10?1:0)<<v<<" "<<u[i];return s.str();}
static string ss(uint64_t b){return hb(b)+"/s";}
static string sf(double s){int d=s/86400,h=((int)s%86400)/3600,m=((int)s%3600)/60;char b[64];
    if(d)snprintf(b,sizeof(b),"%dd%02d:%02d",d,h,m);else snprintf(b,sizeof(b),"%02d:%02d:%02d",h,m,(int)s%60);return b;}

struct C{double r,g,b;};
static C BG{0.086,0.086,0.086},BOX_BG{0.102,0.102,0.102},TITLE{0.235,0.706,0.902},MAIN{0.824,0.824,0.824};
static C HI{0.392,0.784,0.941},BORDER{0.216,0.216,0.216},CPU_C{0.314,0.706,0.863};
static C DL_C{0.235,0.784,0.314},UL_C{0.863,0.549,0.157},RAM_C{0.314,0.706,0.314};
static C CACHED_C{0.784,0.627,0.157},DIV_C{0.255,0.255,0.255},GBG{0.071,0.071,0.071};
static C GREY{0.588,0.588,0.588},GREEN{0.471,0.784,0.471},BLUE{0.549,0.706,0.863};

static void sc(cairo_t*cr,C c){cairo_set_source_rgb(cr,c.r,c.g,c.b);}
static void bx(cairo_t*cr,double x,double y,double w,double h,C f){sc(cr,f);cairo_rectangle(cr,x,y,w,h);cairo_fill(cr);}
static void bd(cairo_t*cr,double x,double y,double w,double h){sc(cr,BORDER);cairo_set_line_width(cr,1);cairo_rectangle(cr,x,y,w,h);cairo_stroke(cr);}
static void tx(cairo_t*cr,double x,double y,const string&s,C c,int sz=9,bool b=false){
    sc(cr,c);cairo_select_font_face(cr,"monospace",CAIRO_FONT_SLANT_NORMAL,b?CAIRO_FONT_WEIGHT_BOLD:CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr,sz);cairo_move_to(cr,x,y+sz*0.75);cairo_show_text(cr,s.c_str());}
static void txc(cairo_t*cr,double x,double y,double w,const string&s,C c,int sz=9,bool b=false){
    cairo_text_extents_t te;cairo_select_font_face(cr,"monospace",CAIRO_FONT_SLANT_NORMAL,b?CAIRO_FONT_WEIGHT_BOLD:CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr,sz);cairo_text_extents(cr,s.c_str(),&te);tx(cr,x+(w-te.width)/2,y,s,c,sz,b);}
static void bar(cairo_t*cr,double x,double y,double w,double h,double p,C c){
    p=min(1.0,max(0.0,p));bx(cr,x,y,w*p,h,c);bd(cr,x,y,w,h);}
static void gr(cairo_t*cr,double x,double y,double w,double h,const deque<long long>&d,long long mx,C c){
    bx(cr,x,y,w,h,GBG);if(d.size()<2)return;long long m=mx?(mx):max(1ll,*max_element(d.begin(),d.end()));m=max(m,1ll);
    int n=d.size();sc(cr,c);cairo_set_line_width(cr,1.5);cairo_move_to(cr,x,y+h-(d[0]*h/m));
    for(int i=1;i<n;i++){double px=x+(double)i*w/max(1,n-1),py=y+h-(d[i]*h/m);py=max(y,min(y+h,py));cairo_line_to(cr,px,py);}
    cairo_stroke(cr);bd(cr,x,y,w,h);}

int main(){
    Config::unlock();Config::lock();Shared::init();
    Cpu::collect(false);Mem::collect(false);Net::collect(false);Proc::collect(false);
    Cpu::collect(false);Mem::collect(false);Net::collect(false);Proc::collect(false);

    int W=960,H=1300;cairo_surface_t*s=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,W,H);
    cairo_t*cr=cairo_create(s);bx(cr,0,0,W,H,BG);

    auto&cpu=Cpu::current_cpu;auto&mem=Mem::current_mem;auto&procs=Proc::current_procs;
    double pad=3,gap=2,x=2,y=2;
    int cpu_w=(W-6)*55/100,net_w=W-6-cpu_w-gap;
    double cpu_x=x,net_x=cpu_x+cpu_w+gap;

    // ─── Row 1: CPU | Network ─────────────────────────────
    int ncores=max(1,(int)cpu.core_percent.size()),cols=max(1,(cpu_w-16)/160),rows=(ncores+cols-1)/cols;
    int cpu_h=16+50+3+14+3+rows*9+6;
    int net_h=16+3+50+14+3+50+14+6;
    int row1_h=max(cpu_h,net_h);

    // CPU Box
    {bx(cr,cpu_x,y,cpu_w,row1_h,BOX_BG);bd(cr,cpu_x,y,cpu_w,row1_h);
        string nm=Cpu::cpuName;
#ifdef __linux__
        if(Shared::physical_cores>0){nm+=" ["+to_string(Shared::physical_cores)+"C]";if(Shared::smt_enabled)nm+="/"+to_string(Shared::coreCount)+"T";}
#endif
        txc(cr,cpu_x,y,cpu_w,nm,TITLE,9,1);
        tx(cr,cpu_x+4,y+2,"m",HI,8,1);tx(cr,cpu_x+12,y+2,"enu",TITLE,8);
        tx(cr,cpu_x+44,y+2,"p",HI,8,1);tx(cr,cpu_x+52,y+2,"reset",TITLE,8);
        if(!Cpu::cpuHz.empty())tx(cr,cpu_x+cpu_w-70,y+2,Cpu::cpuHz,GREEN,8);
        double cy=cpu_x,ry=y+16;
        if(cpu.cpu_percent.contains("total"s))gr(cr,cy+pad,ry,cpu_w-2*pad,50,cpu.cpu_percent.at("total"s),100,CPU_C);
        long long pct=cpu.cpu_percent.contains("total"s)&&!cpu.cpu_percent.at("total"s).empty()?cpu.cpu_percent.at("total"s).back():0;
        txc(cr,cy,ry+50/2,cpu_w,to_string(pct)+"%",MAIN,18,1);
        ry+=53;
        ostringstream si;si<<"Load avg: "<<fixed<<setprecision(2);
        if(!cpu.load_avg.empty())si<<cpu.load_avg[0]<<" "<<(cpu.load_avg.size()>1?to_string(cpu.load_avg[1]):"")<<" "<<(cpu.load_avg.size()>2?to_string(cpu.load_avg[2]):"");
        si<<"   Up: "<<sf(Tools::system_uptime());tx(cr,cy+pad,ry,si.str(),MAIN,8);ry+=16;
        for(int r=0;r<rows;r++){double rry=ry+r*9;
            for(int c=0;c<cols;c++){int idx=r*cols+c;if(idx>=ncores)break;
                double cx2=cy+pad+c*(cpu_w/cols),cw2=cpu_w/cols-4;
                tx(cr,cx2,rry,"C"+to_string(idx),GREY,7);
                if(idx<(int)cpu.core_percent.size())gr(cr,cx2+22,rry,cw2-60,9,cpu.core_percent[idx],100,BLUE);
                double cp=0;if(idx<(int)cpu.core_percent.size()&&!cpu.core_percent[idx].empty())cp=cpu.core_percent[idx].back();
                C cc=cp>90?C{0.86,0.2,0.2}:cp>75?C{0.94,0.59,0.12}:RAM_C;
                bar(cr,cx2+cw2-42,rry+1,30,7,cp/100.0,cc);tx(cr,cx2+cw2-9,rry+1,to_string((int)cp)+"%",MAIN,7);}}}

    // Network Box
    {bx(cr,net_x,y,net_w,row1_h,BOX_BG);bd(cr,net_x,y,net_w,row1_h);
        string iface=Net::selected_iface,ip,conn="O";
        if(Net::current_net.contains(iface)){auto&n=Net::current_net.at(iface);ip=n.ipv4.empty()?n.ipv6:n.ipv4;conn=n.connected?"●":"○";}
        txc(cr,net_x,y,net_w,"Net  "+iface+"  "+conn+"  "+ip,TITLE,8,1);
        double ny=y+16;
        if(Net::current_net.contains(iface)){auto&n=Net::current_net.at(iface);
            if(n.bandwidth.contains("download"s)){tx(cr,net_x+pad,ny,"▼ DL",DL_C,8,1);ny+=10;
                gr(cr,net_x+pad,ny,net_w-2*pad,50,n.bandwidth.at("download"s),0,DL_C);ny+=50;
                auto&st=n.stat.at("download"s);tx(cr,net_x+pad,ny,"Spd:"+ss(st.speed)+" Tot:"+hb(st.total)+" Avg:"+ss(st.avg_speed?st.avg_speed:0),MAIN,8);ny+=16;}
            if(n.bandwidth.contains("upload"s)){tx(cr,net_x+pad,ny,"▲ UL",UL_C,8,1);ny+=10;
                gr(cr,net_x+pad,ny,net_w-2*pad,50,n.bandwidth.at("upload"s),0,UL_C);ny+=50;
                auto&st=n.stat.at("upload"s);tx(cr,net_x+pad,ny,"Spd:"+ss(st.speed)+" Tot:"+hb(st.total)+" Avg:"+ss(st.avg_speed?st.avg_speed:0),MAIN,8);}}}

    y+=row1_h+gap;

    // ─── Row 2: Memory ────────────────────────────────────
    int mem_w=W-4,dcount=min(8,(int)mem.disks_order.size()),mem_h=16+4+18+2+14+2+14+9+2+14+2+14+dcount*14+8;
    {bx(cr,x,y,mem_w,mem_h,BOX_BG);bd(cr,x,y,mem_w,mem_h);
        txc(cr,x,y,mem_w,"Memory & Disks",TITLE,9,1);y+=20;
        uint64_t tot=Mem::get_totalMem(),used=mem.stats["used"],avail=mem.stats["available"],cache=mem.stats["cached"],free=mem.stats["free"];
        double rp=tot>0?(double)used/tot:0;
        tx(cr,x+pad,y,"RAM  "+hb(used)+" / "+hb(tot),MAIN,8);tx(cr,x+mem_w-44,y,to_string((int)(rp*100))+"%",MAIN,8);y+=14;
        bar(cr,x+pad,y,mem_w-2*pad,18,rp,RAM_C);y+=20;
        tx(cr,x+pad,y,"avail:"+hb(avail)+"  cache:"+hb(cache)+"  free:"+hb(free),GREY,8);y+=16;
        uint64_t su=mem.stats["swap_used"],stot=mem.stats["swap_total"],sfree=mem.stats["swap_free"];
        if(stot>0){double sp=(double)su/stot;
            tx(cr,x+pad,y,"Swap "+hb(su)+" / "+hb(stot),MAIN,8);tx(cr,x+mem_w-44,y,to_string((int)(sp*100))+"%",MAIN,8);y+=14;
            bar(cr,x+pad,y,mem_w-2*pad,9,sp,CACHED_C);y+=11;
            tx(cr,x+pad,y,"free:"+hb(sfree),GREY,8);y+=16;}
        tx(cr,x+pad,y,"Disks",BLUE,8,1);y+=16;
        for(int i=0;i<dcount;i++){auto&mnt=mem.disks_order[i];if(!mem.disks.contains(mnt))continue;auto&d=mem.disks.at(mnt);
            tx(cr,x+pad,y,d.name,MAIN,8);double dp=d.total>0?(double)d.used/d.total:0;
            C dc=dp>0.9?C{0.86,0.2,0.2}:dp>0.75?C{0.94,0.59,0.12}:RAM_C;
            bar(cr,x+90,y+2,mem_w-200,10,dp,dc);
            ostringstream di;di<<hb(d.used)<<"/"<<hb(d.total)<<" ("<<d.used_percent<<"%)";tx(cr,x+mem_w-150,y,di.str(),GREY,8);y+=14;}}

    y+=gap;

    // ─── Row 3: Processes ─────────────────────────────────
    int nshow=min(30,(int)procs.size()),proc_h=16+14+nshow*13+8;
    {bx(cr,x,y,mem_w,proc_h,BOX_BG);bd(cr,x,y,mem_w,proc_h);
        txc(cr,x,y,mem_w,"Processes ("+to_string(procs.size())+")",TITLE,9,1);y+=16;
        tx(cr,x+pad,y,"PID",GREY,8,1);tx(cr,x+74,y,"Name",GREY,8,1);
        tx(cr,x+mem_w-160,y,"CPU%",GREY,8,1);tx(cr,x+mem_w-100,y,"MEM",GREY,8,1);tx(cr,x+mem_w-260,y,"User",GREY,8,1);y+=14;
        for(int i=0;i<nshow;i++){auto&p=procs[i];
            C tc=p.cpu_p>50?C{1,0.55,0.39}:p.cpu_p>20?C{0.94,0.86,0.55}:MAIN;
            tx(cr,x+pad,y,to_string(p.pid),tc,8);
            string nm=p.name;if(nm.size()>35)nm=nm.substr(0,32)+"...";tx(cr,x+74,y,nm,tc,8);
            ostringstream cp;cp<<fixed<<setprecision(1)<<p.cpu_p;tx(cr,x+mem_w-160,y,cp.str(),tc,8);
            tx(cr,x+mem_w-100,y,hb(p.mem),tc,8);string us=p.user;if(us.size()>12)us=us.substr(0,11);tx(cr,x+mem_w-260,y,us,tc,8);y+=13;}}

    cairo_surface_write_to_png(s,"/tmp/btop-screenshot.png");
    cairo_destroy(cr);cairo_surface_destroy(s);
    return 0;
}
