// btop-gui v4 — close visual match to terminal btop
#ifndef BTOP_GUI_H
#define BTOP_GUI_H

#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include <wx/scrolwin.h>
#include <wx/image.h>

#include "btop_shared.hpp"
#include "btop_config.hpp"

namespace Cpu { extern cpu_info current_cpu; }
namespace Mem { extern mem_info current_mem; }
namespace Proc { extern std::vector<proc_info> current_procs; }

// ─── Dashboard ──────────────────────────────────────────────
class Dashboard : public wxScrolledWindow {
public:
    Dashboard(wxWindow* parent);

    // Toggles for keyboard shortcuts (1/2/3/4)
    bool show_cpu=true, show_mem=true, show_net=true, show_proc=true;

    void DoPaint(wxDC& dc);
    void RefreshState();

private:
    void OnPaint(wxPaintEvent&);
    void DrawBox(wxDC&,int x,int y,int w,int h,const wxColour&border);
    void BoxBorder(wxDC&,int x,int y,int w,int h,const wxColour&c);
    void BoxTitle(wxDC&,int x,int y,int w,const wxString&s,const wxColour&c);
    void HBar(wxDC&,int x,int y,int w,int h,double pct,const wxColour&c);
    void MiniGraph(wxDC&,int x,int y,int w,int h,const std::deque<long long>&d,long long mx,const wxColour&c,int thick);

    int DrawCPU(wxDC&,int y,int x,int w,int fh);
    int DrawMem(wxDC&,int y,int x,int w,int fh);
    int DrawNet(wxDC&,int y,int x,int w,int fh);
    int DrawProc(wxDC&,int y,int x,int w,int fh);

    struct {
        std::deque<long long> cpu_total;
        std::string cpu_name, cpu_freq;
        std::vector<std::deque<long long>> cpu_cores;
        double loadavg[3]={0,0,0};
        long long cpu_temp=0;
        int battery_pct=-1;
        std::string battery_status;

        uint64_t mem_total=0,mem_used=0,mem_avail=0,mem_cache=0,mem_free=0;
        uint64_t swap_total=0,swap_used=0,swap_free=0;

        struct DiskEntry { std::string mount; Mem::disk_info info; };
        std::vector<DiskEntry> disks;

        std::deque<long long> net_dl, net_ul;
        uint64_t net_dl_speed=0,net_ul_speed=0,net_dl_total=0,net_ul_total=0;
        uint64_t net_dl_avg=0,net_ul_avg=0;
        std::string net_iface,net_ip;
        bool net_connected=false;

        std::vector<Proc::proc_info> procs;
    } state;

    wxTimer* timer;
    int paint_h=1600;

    wxDECLARE_EVENT_TABLE();
};

// ─── MainFrame ─────────────────────────────────────────────
class MainFrame : public wxFrame {
public:
    MainFrame();
    void TakeScreenshot(const wxString& path);
    Dashboard* dash=nullptr;

private:
    void OnClose(wxCloseEvent&);
    void OnKeyDown(wxKeyEvent&);
    wxDECLARE_EVENT_TABLE();
};

// ─── App ───────────────────────────────────────────────────
class BtopApp : public wxApp {
public:
    bool OnInit() override;
    int OnExit() override;
};

#endif
