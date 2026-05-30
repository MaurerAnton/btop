// btop-gui v3: single dashboard layout matching terminal btop
// wxWidgets frontend with all sections visible at once, no tabs
// screenshot support for headless comparison

#ifndef BTOP_GUI_H
#define BTOP_GUI_H

#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/combobox.h>
#include <wx/textctrl.h>
#include <wx/scrolwin.h>
#include <wx/image.h>

#include "btop_shared.hpp"
#include "btop_config.hpp"

namespace Cpu { extern cpu_info current_cpu; }
namespace Mem { extern mem_info current_mem; }
namespace Proc { extern std::vector<proc_info> current_procs; }

// ─── Graph canvas ──────────────────────────────────────────

class GraphStrip : public wxWindow {
public:
    GraphStrip(wxWindow* p, const wxSize& sz = wxSize(400, 24));

    void SetData(const std::deque<long long>& d) { data = d; Refresh(false); }
    void SetMax(long long m) { gmax = m; }
    void SetColor(const wxColour& c) { color = c; }
    void SetAuto(bool a) { autoscale = a; }

private:
    void OnPaint(wxPaintEvent&);
    std::deque<long long> data;
    wxColour color{60,180,75};
    long long gmax = 100;
    bool autoscale = true;
    wxDECLARE_EVENT_TABLE();
};

// ─── Dashboard (single scrollable canvas) ──────────────────

class Dashboard : public wxScrolledWindow {
public:
    Dashboard(wxWindow* parent);

private:
    void OnTimer(wxTimerEvent&);
    void OnPaint(wxPaintEvent&);
    void DrawCPU(wxDC& dc, int& y, int x, int w);
    void DrawMem(wxDC& dc, int& y, int x, int w);
    void DrawNet(wxDC& dc, int& y, int x, int w);
    void DrawProc(wxDC& dc, int& y, int x, int w);

    // Box drawing helpers
    void DrawBox(wxDC& dc, int x, int y, int w, int h, const wxString& title, const wxColour& border);
    void DrawGauge(wxDC& dc, int x, int y, int w, int h, double pct, const wxColour& c);
    void DrawLineGraph(wxDC& dc, int x, int y, int w, int h, const std::deque<long long>& d,
                       long long max, const wxColour& c, bool autoscale);

    // Data stores (populated from collectors)
    struct {
        // CPU
        std::deque<long long> cpu_total;
        std::string cpu_name, cpu_freq;
        std::vector<std::deque<long long>> cpu_cores;
        double loadavg[3] = {0,0,0};
        long long cpu_temp = 0, cpu_tmax = 90;
        int battery_pct = -1;
        std::string battery_status;

        // Memory
        uint64_t mem_total = 0, mem_used = 0, mem_avail = 0, mem_cache = 0, mem_free = 0;
        uint64_t swap_total = 0, swap_used = 0, swap_free = 0;

        // Network
        std::deque<long long> net_dl, net_ul;
        uint64_t net_dl_speed = 0, net_ul_speed = 0;
        uint64_t net_dl_total = 0, net_ul_total = 0;
        uint64_t net_dl_avg = 0, net_ul_avg = 0;
        std::string net_iface, net_ip;
        bool net_connected = false;

        // Processes
        std::vector<Proc::proc_info> procs;
    } state;

    // Process scroll offset
    int proc_scroll = 0;
    int proc_max_visible = 0;

    wxTimer* timer;
    wxBitmap backbuf;
    int paint_w = 800, paint_h = 1200;

public:
    void DoPaint(wxDC& dc);

private:

    wxDECLARE_EVENT_TABLE();
};

// ─── Main frame ────────────────────────────────────────────

class MainFrame : public wxFrame {
public:
    MainFrame();
    void TakeScreenshot(const wxString& path);

private:
    void OnClose(wxCloseEvent&);
    void OnKeyDown(wxKeyEvent&);
    Dashboard* dash = nullptr;
    wxDECLARE_EVENT_TABLE();
};

// ─── App ───────────────────────────────────────────────────

class BtopApp : public wxApp {
public:
    bool OnInit() override;
    int OnExit() override;
};

#endif
