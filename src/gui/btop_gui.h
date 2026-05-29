// btop-gui: wxWidgets GUI frontend for btop++
// Uses existing btop data collection backend, renders with native widgets

#ifndef BTOP_GUI_H
#define BTOP_GUI_H

#include <wx/wx.h>
#include <wx/aui/auibook.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>
#include <wx/gauge.h>
#include <wx/timer.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/splitter.h>
#include <wx/combobox.h>
#include <wx/textctrl.h>

#include <deque>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <unordered_map>

#include "btop_shared.hpp"
#include "btop_config.hpp"

// Extern data structs from platform collectors (not declared in btop_shared.hpp)
namespace Cpu {
    extern cpu_info current_cpu;
}
namespace Mem {
    extern mem_info current_mem;
}
namespace Proc {
    extern std::vector<proc_info> current_procs;
}

// ─── Graph Widget ───────────────────────────────────────────────

class GraphPanel : public wxPanel {
public:
    GraphPanel(wxWindow* parent, wxWindowID id = wxID_ANY,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize);

    void SetData(const std::deque<long long>& data);
    void SetMax(long long max_val) { graph_max = max_val; }
    void SetColor(const wxColour& c) { color = c; }
    void SetAutoScale(bool a) { autoscale = a; }
    void SetTitle(const wxString& t) { title = t; }

private:
    void OnPaint(wxPaintEvent& evt);
    void OnSize(wxSizeEvent& evt);

    std::deque<long long> graph_data;
    long long graph_max = 100;
    bool autoscale = true;
    wxColour color{60, 180, 75};
    wxString title;
    wxBitmap backbuf;

    wxDECLARE_EVENT_TABLE();
};

// ─── CPU Panel ────────────────────────────────────────────────

class CpuPanel : public wxPanel {
public:
    CpuPanel(wxWindow* parent);

    void RefreshData();

private:
    void BuildUI();
    void UpdateStats();

    GraphPanel* total_graph = nullptr;
    std::vector<GraphPanel*> core_graphs;
    std::vector<wxGauge*> core_gauges;
    wxStaticText* cpu_name_label = nullptr;
    wxStaticText* freq_label = nullptr;
    wxStaticText* load_label = nullptr;
    wxStaticText* uptime_label = nullptr;
    wxStaticText* battery_label = nullptr;
    wxStaticText* temp_label = nullptr;
    wxStaticText* smt_label = nullptr;
    wxTimer* timer = nullptr;
};

// ─── Memory Panel ─────────────────────────────────────────────

class MemPanel : public wxPanel {
public:
    MemPanel(wxWindow* parent);

    void RefreshData();

private:
    void BuildUI();
    void UpdateStats();

    wxGauge* ram_gauge = nullptr;
    wxStaticText* ram_label = nullptr;
    wxGauge* swap_gauge = nullptr;
    wxStaticText* swap_label = nullptr;
    wxStaticText* total_label = nullptr;
    wxListCtrl* disk_list = nullptr;
    wxTimer* timer = nullptr;
};

// ─── Network Panel ────────────────────────────────────────────

class NetPanel : public wxPanel {
public:
    NetPanel(wxWindow* parent);

    void RefreshData();

private:
    void BuildUI();
    void UpdateStats();

    GraphPanel* dl_graph = nullptr;
    GraphPanel* ul_graph = nullptr;
    wxStaticText* dl_speed_label = nullptr;
    wxStaticText* ul_speed_label = nullptr;
    wxStaticText* dl_total_label = nullptr;
    wxStaticText* ul_total_label = nullptr;
    wxStaticText* dl_avg_label = nullptr;
    wxStaticText* ul_avg_label = nullptr;
    wxStaticText* iface_label = nullptr;
    wxStaticText* ip_label = nullptr;
    wxComboBox* iface_choice = nullptr;
    wxTimer* timer = nullptr;
};

// ─── Process Panel ───────────────────────────────────────────

class ProcPanel : public wxPanel {
public:
    ProcPanel(wxWindow* parent);

    void RefreshData();

private:
    void BuildUI();
    void UpdateList();
    void OnColClick(wxListEvent& evt);

    wxListCtrl* proc_list = nullptr;
    wxStaticText* count_label = nullptr;
    wxTextCtrl* filter_text = nullptr;
    wxTimer* timer = nullptr;
    int sort_col = 3;
    bool sort_asc = false;
    wxDECLARE_EVENT_TABLE();
};

// ─── Main Frame ───────────────────────────────────────────────

class MainFrame : public wxFrame {
public:
    MainFrame();

private:
    void OnClose(wxCloseEvent& evt);

    wxAuiNotebook* notebook = nullptr;
    CpuPanel* cpu_panel = nullptr;
    MemPanel* mem_panel = nullptr;
    NetPanel* net_panel = nullptr;
    ProcPanel* proc_panel = nullptr;

    wxDECLARE_EVENT_TABLE();
};

// ─── App ──────────────────────────────────────────────────────

class BtopApp : public wxApp {
public:
    bool OnInit() override;
    int OnExit() override;
};

#endif // BTOP_GUI_H
