// btop-gui v6 — terminal-accurate: CPU top, Mem+Net left, Proc right
#ifndef BTOP_GUI_H
#define BTOP_GUI_H
#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include <wx/scrolwin.h>
#include "btop_shared.hpp"
#include "btop_config.hpp"
namespace Cpu{extern cpu_info current_cpu;}namespace Mem{extern mem_info current_mem;}namespace Proc{extern std::vector<proc_info> current_procs;}

class Dashboard:public wxScrolledWindow{public:
    Dashboard(wxWindow*p);bool show_cpu=true,show_mem=true,show_net=true,show_proc=true;void DoPaint(wxDC&dc);void RefreshState();
private:
    void OnPaint(wxPaintEvent&);int CalcCpuH(int w),CalcNetH(int w),CalcMemH(int w),CalcProcH(int w);
    void DrawCPU(wxDC&,int x,int y,int w,int h);void DrawNet(wxDC&,int x,int y,int w,int h);
    void DrawMem(wxDC&,int x,int y,int w,int h);void DrawProc(wxDC&,int x,int y,int w,int h);
    void Box(wxDC&,int x,int y,int w,int h);void TitleC(wxDC&,int x,int y,int w,const wxString&s);
    void Text(wxDC&,int x,int y,const wxString&s,const wxColour&c,int sz=8,bool b=false);
    void Bar(wxDC&,int x,int y,int w,int h,double p,const wxColour&c);
    void Graph(wxDC&,int x,int y,int w,int h,const std::deque<long long>&d,long long mx,const wxColour&c);
    struct{std::deque<long long>cpu_total;std::string cpu_name,cpu_freq;std::vector<std::deque<long long>>cpu_cores;
        double loadavg[3];long long cpu_temp=0;int battery_pct=-1;
        uint64_t mem_total=0,mem_used=0,mem_avail=0,mem_cache=0,mem_free=0,swap_total=0,swap_used=0,swap_free=0;
        struct DiskEntry{std::string mount;Mem::disk_info info;};std::vector<DiskEntry>disks;
        std::deque<long long>net_dl,net_ul;uint64_t net_dl_speed=0,net_ul_speed=0,net_dl_total=0,net_ul_total=0,net_dl_avg=0,net_ul_avg=0;
        std::string net_iface,net_ip;bool net_connected=false;std::vector<Proc::proc_info>procs;}state;
    wxTimer*timer;int paint_h=1600;wxDECLARE_EVENT_TABLE();};
class MainFrame:public wxFrame{public:MainFrame();void TakeScreenshot(const wxString&p);Dashboard*dash=nullptr;
private:void OnClose(wxCloseEvent&);void OnKeyDown(wxKeyEvent&);wxDECLARE_EVENT_TABLE();};
class BtopApp:public wxApp{public:bool OnInit()override;int OnExit()override;};
#endif
