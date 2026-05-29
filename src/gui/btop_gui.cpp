// btop-gui: wxWidgets GUI for btop++ — implementation
#include "btop_gui.h"
#include <sstream>
#include <iomanip>
#include <atomic>

using namespace std;

// Stubs for symbols normally in btop.o (which we can't link — has main)
namespace Runner {
    std::atomic<bool> stopping{false};
    std::atomic<bool> coreNum_reset{false};
    std::atomic<bool> active{false};
}
namespace Global {
    const std::string Version = "1.4.7-gui";
    std::string overlay;
    std::string exit_error_msg;
    std::string clock;
    std::atomic<bool> resized{false};
    std::atomic<bool> init_conf{true};
    uid_t real_uid = 0;
    const std::vector<std::array<std::string, 2>> Banner_src;
}

// Stubs for display dimensions/state (normally in btop_draw.o)
namespace Cpu { int width = 100; int min_width = 60; int min_height = 8; }
namespace Mem { int width = 300; int min_width = 36; int min_height = 6; bool redraw = true; }
namespace Net { int width = 300; int min_width = 36; int min_height = 6; bool redraw = true; }
namespace Proc { int width = 300; int min_width = 44; int min_height = 16; bool redraw = true; bool shown = true; int select_max = 1; int selected_pid = 0; int start = 0; int selected = 0; int selected_depth = 0; std::string selected_name; }
namespace Gpu { int width = 100; int min_width = 36; int count = 0; int shown = 0; }
namespace Menu { bool active = false; bool redraw = false; }

// Stub for clean_quit (normally in btop.o)
void clean_quit(int) {}

// ─── Helpers ──────────────────────────────────────────────────

static string human_readable(uint64_t bytes, bool bits = false) {
    const char* units[] = {"", "K", "M", "G", "T", "P"};
    double v = bits ? bytes * 8.0 : (double)bytes;
    int i = 0;
    while (v >= 1000.0 && i < 5) { v /= 1000.0; i++; }
    ostringstream ss;
    ss << fixed << setprecision(v < 10 ? 1 : 0) << v << " " << units[i] << (bits ? "bit" : "B");
    return ss.str();
}

static string human_readable_speed(uint64_t bytes_per_sec) {
    return human_readable(bytes_per_sec, false) + "/s";
}

static wxColour cpu_color(double pct) {
    if (pct > 90) return wxColour(220, 50, 50);
    if (pct > 75) return wxColour(240, 150, 30);
    if (pct > 50) return wxColour(220, 200, 40);
    return wxColour(60, 180, 75);
}

static wxColour temp_color(long long t, long long tmax = 90) {
    double r = clamp((double)t / max(1ll, tmax), 0.0, 1.0);
    return wxColour((int)(255 * r), (int)(180 * (1.0 - r)), (int)(80 * (1.0 - r)));
}

static string sec_to_str(double secs) {
    int d = (int)secs / 86400;
    int h = ((int)secs % 86400) / 3600;
    int m = ((int)secs % 3600) / 60;
    char buf[64];
    if (d > 0) snprintf(buf, sizeof(buf), "%dd %02d:%02d", d, h, m);
    else snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, (int)secs % 60);
    return string(buf);
}

// ─── GraphPanel ────────────────────────────────────────────────

wxBEGIN_EVENT_TABLE(GraphPanel, wxPanel)
    EVT_PAINT(GraphPanel::OnPaint)
    EVT_SIZE(GraphPanel::OnSize)
wxEND_EVENT_TABLE()

GraphPanel::GraphPanel(wxWindow* parent, wxWindowID id,
                       const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, id, pos, size, wxBORDER_SIMPLE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(100, 40));
}

void GraphPanel::SetData(const deque<long long>& data) {
    graph_data = data;
    Refresh(false);
}

void GraphPanel::OnSize(wxSizeEvent& evt) {
    backbuf = wxBitmap();
    Refresh();
    evt.Skip();
}

void GraphPanel::OnPaint(wxPaintEvent&) {
    wxSize sz = GetClientSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    if (!backbuf.IsOk() || backbuf.GetWidth() != sz.x || backbuf.GetHeight() != sz.y) {
        backbuf.Create(sz.x, sz.y);
    }

    wxBufferedPaintDC dc(this, backbuf);
    dc.SetBackground(wxBrush(wxColour(30, 30, 30)));
    dc.Clear();

    // Title
    dc.SetTextForeground(wxColour(180, 180, 180));
    dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    dc.DrawText(title, 4, 2);

    // Graph area
    int gx = 4, gy = 16, gw = sz.x - 8, gh = sz.y - 20;
    if (gh < 4) return;

    // Border
    dc.SetPen(wxPen(wxColour(60, 60, 60)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(gx, gy, gw, gh);

    if (graph_data.empty()) return;

    // Auto-scale
    long long gmax = graph_max;
    if (autoscale && !graph_data.empty()) {
        gmax = max(1ll, *max_element(graph_data.begin(), graph_data.end()));
        gmax = max(gmax, graph_max); // don't go below set max
    }

    int n = (int)graph_data.size();
    double xscale = (double)gw / max(1, n - 1);
    double yscale = (double)gh / max(1ll, gmax);

    dc.SetPen(wxPen(color, 1));
    int last_x = -1, last_y = -1;
    for (int i = 0; i < n; i++) {
        int x = gx + (int)(i * xscale);
        int y = gy + gh - 1 - (int)(graph_data[i] * yscale);
        y = clamp(y, gy, gy + gh - 1);
        if (last_x >= 0) {
            dc.DrawLine(last_x, last_y, x, y);
        }
        last_x = x; last_y = y;
    }

    // Max label
    if (gmax > 0) {
        dc.SetTextForeground(wxColour(120, 120, 120));
        dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.DrawText(human_readable_speed(gmax), gx + 2, gy + 2);
    }
}

// ─── CPU Panel ────────────────────────────────────────────────

CpuPanel::CpuPanel(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY)
{
    SetScrollRate(5, 5);
    BuildUI();

    timer = new wxTimer(this);
    timer->Bind(wxEVT_TIMER, [this](wxTimerEvent&) { RefreshData(); });
    timer->Start(1500);
}

void CpuPanel::BuildUI() {
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    // CPU name + info row
    wxBoxSizer* info_row = new wxBoxSizer(wxHORIZONTAL);
    cpu_name_label = new wxStaticText(this, wxID_ANY, "CPU", wxDefaultPosition, wxSize(400, 20));
    cpu_name_label->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    cpu_name_label->SetForegroundColour(wxColour(220, 220, 220));
    info_row->Add(cpu_name_label, 0, wxALL, 4);

    smt_label = new wxStaticText(this, wxID_ANY, "");
    smt_label->SetForegroundColour(wxColour(160, 160, 160));
    info_row->Add(smt_label, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    info_row->AddStretchSpacer();

    freq_label = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxSize(120, 20));
    freq_label->SetForegroundColour(wxColour(120, 200, 120));
    info_row->Add(freq_label, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    main_sizer->Add(info_row, 0, wxEXPAND);

    // Total CPU graph
    total_graph = new GraphPanel(this, wxID_ANY, wxDefaultPosition, wxSize(600, 150));
    total_graph->SetTitle("CPU Usage %");
    total_graph->SetMax(100);
    total_graph->SetColor(wxColour(80, 180, 220));
    main_sizer->Add(total_graph, 0, wxEXPAND | wxALL, 4);

    // Stats row: load, uptime, temp, battery
    wxBoxSizer* stats_row = new wxBoxSizer(wxHORIZONTAL);

    load_label = new wxStaticText(this, wxID_ANY, "Load: —");
    load_label->SetForegroundColour(wxColour(200, 180, 100));
    stats_row->Add(load_label, 0, wxALL, 4);

    uptime_label = new wxStaticText(this, wxID_ANY, "Up: —");
    uptime_label->SetForegroundColour(wxColour(140, 180, 200));
    stats_row->Add(uptime_label, 0, wxALL, 4);

    temp_label = new wxStaticText(this, wxID_ANY, "Temp: —");
    temp_label->SetForegroundColour(wxColour(220, 140, 60));
    stats_row->Add(temp_label, 0, wxALL, 4);

    battery_label = new wxStaticText(this, wxID_ANY, "");
    battery_label->SetForegroundColour(wxColour(180, 200, 100));
    stats_row->Add(battery_label, 0, wxALL, 4);

    main_sizer->Add(stats_row, 0, wxEXPAND);

    // Per-core section header
    wxStaticText* core_header = new wxStaticText(this, wxID_ANY, "Per-Core Usage");
    core_header->SetForegroundColour(wxColour(160, 160, 160));
    core_header->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    main_sizer->Add(core_header, 0, wxALL, 4);

    // Per-core gauges + mini graphs
    wxFlexGridSizer* core_grid = new wxFlexGridSizer(0, 6, 2, 4);

    for (long i = 0; i < Shared::coreCount; i++) {
        wxStaticText* lbl = new wxStaticText(this, wxID_ANY,
            wxString::Format("C%ld", i), wxDefaultPosition, wxSize(32, 16));
        lbl->SetForegroundColour(wxColour(180, 180, 180));
        core_grid->Add(lbl);

        wxGauge* gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(80, 14));
        core_gauges.push_back(gauge);
        core_grid->Add(gauge);

        GraphPanel* gp = new GraphPanel(this, wxID_ANY, wxDefaultPosition, wxSize(100, 30));
        gp->SetMax(100);
        gp->SetColor(wxColour(80, 160, 220));
        core_graphs.push_back(gp);
        core_grid->Add(gp);
    }

    main_sizer->Add(core_grid, 0, wxEXPAND | wxALL, 4);

    SetSizer(main_sizer);
}

void CpuPanel::RefreshData() {
    Cpu::collect(false);
    UpdateStats();
}

void CpuPanel::UpdateStats() {
    auto& cpu = Cpu::current_cpu;

    // Total graph
    if (cpu.cpu_percent.contains("total"s)) {
        total_graph->SetData(cpu.cpu_percent.at("total"s));
    }

    // CPU name + SMT
    string name = Cpu::cpuName;
#ifdef __linux__
    if (Shared::physical_cores > 0) {
        if (Shared::smt_enabled)
            name += " [" + to_string(Shared::physical_cores) + "C/" + to_string(Shared::coreCount) + "T]";
        else
            name += " [" + to_string(Shared::physical_cores) + "C]";
    }
    smt_label->SetLabel(Shared::smt_enabled ? "SMT ON" : "");
    if (Shared::smt_enabled)
        smt_label->SetForegroundColour(wxColour(120, 220, 120));
#endif
    cpu_name_label->SetLabel(wxString::FromUTF8(name));

    // Frequency
    if (!Cpu::cpuHz.empty())
        freq_label->SetLabel(wxString::FromUTF8(Cpu::cpuHz));

    // Per-core
    for (size_t i = 0; i < core_graphs.size() && i < cpu.core_percent.size(); i++) {
        core_graphs[i]->SetData(cpu.core_percent[i]);
        if (!cpu.core_percent[i].empty()) {
            int pct = (int)cpu.core_percent[i].back();
            core_gauges[i]->SetValue(pct);
            core_gauges[i]->SetForegroundColour(cpu_color(pct));
        }
    }

    // Load average
    if (!cpu.load_avg.empty()) {
        ostringstream ss;
        ss << "Load: ";
        for (size_t i = 0; i < min((size_t)3, cpu.load_avg.size()); i++) {
            if (i > 0) ss << " ";
            ss << fixed << setprecision(2) << cpu.load_avg[i];
        }
        load_label->SetLabel(wxString::FromUTF8(ss.str()));
    }

    // Uptime
    double uptime = Tools::system_uptime();
    uptime_label->SetLabel(wxString::FromUTF8("Up: " + sec_to_str(uptime)));

    // Battery
    if (Cpu::has_battery) {
        auto [pct, watts, secs, status] = Cpu::current_bat;
        ostringstream bs;
        bs << "BAT: " << pct << "%";
        if (secs > 0) {
            bs << " (" << sec_to_str(secs) << ")";
        }
        if (watts >= 0) bs << " " << fixed << setprecision(1) << watts << "W";
        battery_label->SetLabel(wxString::FromUTF8(bs.str()));
    }

    // Temperature
    if (Cpu::got_sensors && !cpu.temp.empty() && !cpu.temp[0].empty()) {
        long long t = cpu.temp[0].back();
        long long tmax = cpu.temp_max > 0 ? cpu.temp_max : 90;
        ostringstream ts;
        ts << "Temp: " << t << "°C";
        temp_label->SetLabel(wxString::FromUTF8(ts.str()));
        temp_label->SetForegroundColour(temp_color(t, tmax));
    }

    Layout();
}

// ─── Memory Panel ──────────────────────────────────────────────

MemPanel::MemPanel(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY)
{
    SetScrollRate(5, 5);
    BuildUI();

    timer = new wxTimer(this);
    timer->Bind(wxEVT_TIMER, [this](wxTimerEvent&) { RefreshData(); });
    timer->Start(2000);
}

void MemPanel::BuildUI() {
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    // Total
    total_label = new wxStaticText(this, wxID_ANY, "Total: —");
    total_label->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    total_label->SetForegroundColour(wxColour(200, 200, 200));
    main_sizer->Add(total_label, 0, wxALL, 4);

    // RAM
    wxStaticText* ram_title = new wxStaticText(this, wxID_ANY, "RAM");
    ram_title->SetForegroundColour(wxColour(140, 200, 140));
    ram_title->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    main_sizer->Add(ram_title, 0, wxALL, 4);

    ram_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(400, 24));
    main_sizer->Add(ram_gauge, 0, wxEXPAND | wxALL, 4);

    ram_label = new wxStaticText(this, wxID_ANY, "Used: — / Free: — / Cache: —");
    ram_label->SetForegroundColour(wxColour(180, 180, 180));
    main_sizer->Add(ram_label, 0, wxALL, 4);

    // Swap
    wxStaticText* swap_title = new wxStaticText(this, wxID_ANY, "Swap");
    swap_title->SetForegroundColour(wxColour(200, 160, 60));
    swap_title->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    main_sizer->Add(swap_title, 0, wxALL, 4);

    swap_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(400, 24));
    main_sizer->Add(swap_gauge, 0, wxEXPAND | wxALL, 4);

    swap_label = new wxStaticText(this, wxID_ANY, "Used: — / Free: —");
    swap_label->SetForegroundColour(wxColour(180, 180, 180));
    main_sizer->Add(swap_label, 0, wxALL, 4);

    // Disks
    wxStaticText* disk_title = new wxStaticText(this, wxID_ANY, "Disks");
    disk_title->SetForegroundColour(wxColour(140, 180, 220));
    disk_title->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    main_sizer->Add(disk_title, 0, wxALL, 4);

    disk_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(500, 200),
                               wxLC_REPORT | wxLC_SINGLE_SEL);
    disk_list->AppendColumn("Mount", wxLIST_FORMAT_LEFT, 100);
    disk_list->AppendColumn("Name", wxLIST_FORMAT_LEFT, 80);
    disk_list->AppendColumn("Total", wxLIST_FORMAT_RIGHT, 80);
    disk_list->AppendColumn("Used", wxLIST_FORMAT_RIGHT, 80);
    disk_list->AppendColumn("Free", wxLIST_FORMAT_RIGHT, 80);
    disk_list->AppendColumn("Use%", wxLIST_FORMAT_RIGHT, 60);
    disk_list->AppendColumn("IO Read", wxLIST_FORMAT_RIGHT, 90);
    disk_list->AppendColumn("IO Write", wxLIST_FORMAT_RIGHT, 90);
    main_sizer->Add(disk_list, 1, wxEXPAND | wxALL, 4);

    SetSizer(main_sizer);
}

void MemPanel::RefreshData() {
    Mem::collect(false);
    UpdateStats();
}

void MemPanel::UpdateStats() {
    auto& mem = Mem::current_mem;

    uint64_t total = Mem::get_totalMem();
    total_label->SetLabel(wxString::FromUTF8("Total: " + human_readable(total)));

    // RAM
    uint64_t used = mem.stats["used"];
    uint64_t available = mem.stats["available"];
    uint64_t cached = mem.stats["cached"];
    uint64_t free = mem.stats["free"];

    int ram_pct = total > 0 ? (int)(used * 100 / total) : 0;
    ram_gauge->SetValue(ram_pct);
    if (ram_pct > 90) ram_gauge->SetForegroundColour(wxColour(220, 50, 50));
    else if (ram_pct > 70) ram_gauge->SetForegroundColour(wxColour(240, 150, 30));
    else ram_gauge->SetForegroundColour(wxColour(60, 180, 75));

    ostringstream rs;
    rs << "Used: " << human_readable(used)
       << " / Avail: " << human_readable(available)
       << " / Cache: " << human_readable(cached)
       << " / Free: " << human_readable(free)
       << "  (" << ram_pct << "%)";
    ram_label->SetLabel(wxString::FromUTF8(rs.str()));

    // Swap
    if (Mem::has_swap) {
        uint64_t swap_total = mem.stats["swap_total"];
        uint64_t swap_used = mem.stats["swap_used"];
        uint64_t swap_free = mem.stats["swap_free"];
        int swap_pct = swap_total > 0 ? (int)(swap_used * 100 / swap_total) : 0;
        swap_gauge->SetValue(swap_pct);

        ostringstream ss;
        ss << "Used: " << human_readable(swap_used)
           << " / Free: " << human_readable(swap_free)
           << " / Total: " << human_readable(swap_total);
        swap_label->SetLabel(wxString::FromUTF8(ss.str()));
    }

    // Disks
    disk_list->DeleteAllItems();
    int idx = 0;
    for (const auto& mount : mem.disks_order) {
        if (!mem.disks.contains(mount)) continue;
        auto& d = mem.disks.at(mount);
        disk_list->InsertItem(idx, wxString::FromUTF8(mount));
        disk_list->SetItem(idx, 1, wxString::FromUTF8(d.name));
        disk_list->SetItem(idx, 2, wxString::FromUTF8(human_readable(d.total)));
        disk_list->SetItem(idx, 3, wxString::FromUTF8(human_readable(d.used)));
        disk_list->SetItem(idx, 4, wxString::FromUTF8(human_readable(d.free)));
        disk_list->SetItem(idx, 5, wxString::Format("%d%%", d.used_percent));

        string io_r = d.io_read.empty() ? "—" : human_readable_speed(d.io_read.back());
        string io_w = d.io_write.empty() ? "—" : human_readable_speed(d.io_write.back());
        disk_list->SetItem(idx, 6, wxString::FromUTF8(io_r));
        disk_list->SetItem(idx, 7, wxString::FromUTF8(io_w));
        idx++;
    }
}

// ─── Network Panel ─────────────────────────────────────────────

NetPanel::NetPanel(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY)
{
    SetScrollRate(5, 5);
    BuildUI();

    timer = new wxTimer(this);
    timer->Bind(wxEVT_TIMER, [this](wxTimerEvent&) { RefreshData(); });
    timer->Start(1500);
}

void NetPanel::BuildUI() {
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    // Interface selector
    wxBoxSizer* iface_row = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* iface_lbl = new wxStaticText(this, wxID_ANY, "Interface:");
    iface_lbl->SetForegroundColour(wxColour(180, 180, 180));
    iface_row->Add(iface_lbl, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    iface_choice = new wxComboBox(this, wxID_ANY, "", wxDefaultPosition, wxSize(150, -1));
    iface_choice->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
        Net::selected_iface = iface_choice->GetValue().ToStdString();
    });
    iface_row->Add(iface_choice, 0, wxALL, 4);

    ip_label = new wxStaticText(this, wxID_ANY, "");
    ip_label->SetForegroundColour(wxColour(140, 180, 220));
    iface_row->Add(ip_label, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    iface_label = new wxStaticText(this, wxID_ANY, "");
    iface_label->SetForegroundColour(wxColour(160, 160, 160));
    iface_row->Add(iface_label, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    main_sizer->Add(iface_row, 0, wxEXPAND);

    // Download section
    wxStaticText* dl_title = new wxStaticText(this, wxID_ANY, "▼ Download");
    dl_title->SetForegroundColour(wxColour(120, 200, 120));
    dl_title->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    main_sizer->Add(dl_title, 0, wxALL, 4);

    dl_graph = new GraphPanel(this, wxID_ANY, wxDefaultPosition, wxSize(500, 120));
    dl_graph->SetTitle("Download");
    dl_graph->SetColor(wxColour(60, 200, 80));
    main_sizer->Add(dl_graph, 0, wxEXPAND | wxALL, 4);

    dl_speed_label = new wxStaticText(this, wxID_ANY, "Speed: —");
    dl_speed_label->SetForegroundColour(wxColour(200, 200, 200));
    main_sizer->Add(dl_speed_label, 0, wxALL, 4);

    dl_total_label = new wxStaticText(this, wxID_ANY, "Total: —");
    dl_total_label->SetForegroundColour(wxColour(160, 160, 160));
    main_sizer->Add(dl_total_label, 0, wxALL, 4);

    dl_avg_label = new wxStaticText(this, wxID_ANY, "5m Avg: —");
    dl_avg_label->SetForegroundColour(wxColour(140, 140, 200));
    main_sizer->Add(dl_avg_label, 0, wxALL, 4);

    // Upload section
    wxStaticText* ul_title = new wxStaticText(this, wxID_ANY, "▲ Upload");
    ul_title->SetForegroundColour(wxColour(200, 140, 60));
    ul_title->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    main_sizer->Add(ul_title, 0, wxALL, 4);

    ul_graph = new GraphPanel(this, wxID_ANY, wxDefaultPosition, wxSize(500, 120));
    ul_graph->SetTitle("Upload");
    ul_graph->SetColor(wxColour(220, 140, 40));
    main_sizer->Add(ul_graph, 0, wxEXPAND | wxALL, 4);

    ul_speed_label = new wxStaticText(this, wxID_ANY, "Speed: —");
    ul_speed_label->SetForegroundColour(wxColour(200, 200, 200));
    main_sizer->Add(ul_speed_label, 0, wxALL, 4);

    ul_total_label = new wxStaticText(this, wxID_ANY, "Total: —");
    ul_total_label->SetForegroundColour(wxColour(160, 160, 160));
    main_sizer->Add(ul_total_label, 0, wxALL, 4);

    ul_avg_label = new wxStaticText(this, wxID_ANY, "5m Avg: —");
    ul_avg_label->SetForegroundColour(wxColour(140, 140, 200));
    main_sizer->Add(ul_avg_label, 0, wxALL, 4);

    SetSizer(main_sizer);
}

void NetPanel::RefreshData() {
    Net::collect(false);
    UpdateStats();
}

void NetPanel::UpdateStats() {
    // Update interface list
    iface_choice->Clear();
    for (auto& iface : Net::interfaces) {
        iface_choice->Append(wxString::FromUTF8(iface));
    }
    if (!Net::selected_iface.empty())
        iface_choice->SetStringSelection(wxString::FromUTF8(Net::selected_iface));

    if (!Net::current_net.contains(Net::selected_iface)) return;
    auto& net = Net::current_net.at(Net::selected_iface);

    // IP address
    string ip = net.ipv4.empty() ? net.ipv6 : net.ipv4;
    ip_label->SetLabel(wxString::FromUTF8(ip));
    iface_label->SetLabel(net.connected ? "● Connected" : "○ Disconnected");
    iface_label->SetForegroundColour(net.connected ? wxColour(120, 220, 120) : wxColour(180, 100, 100));

    // Download
    if (net.bandwidth.contains("download"s)) {
        dl_graph->SetData(net.bandwidth.at("download"s));
        auto& stat = net.stat.at("download"s);
        dl_speed_label->SetLabel(wxString::FromUTF8("Speed: " + human_readable_speed(stat.speed)));
        dl_total_label->SetLabel(wxString::FromUTF8("Total: " + human_readable(stat.total)));
        if (stat.avg_speed > 0)
            dl_avg_label->SetLabel(wxString::FromUTF8("5m Avg: " + human_readable_speed(stat.avg_speed)));
    }

    // Upload
    if (net.bandwidth.contains("upload"s)) {
        ul_graph->SetData(net.bandwidth.at("upload"s));
        auto& stat = net.stat.at("upload"s);
        ul_speed_label->SetLabel(wxString::FromUTF8("Speed: " + human_readable_speed(stat.speed)));
        ul_total_label->SetLabel(wxString::FromUTF8("Total: " + human_readable(stat.total)));
        if (stat.avg_speed > 0)
            ul_avg_label->SetLabel(wxString::FromUTF8("5m Avg: " + human_readable_speed(stat.avg_speed)));
    }

    Layout();
}

// ─── Process Panel ─────────────────────────────────────────────

wxBEGIN_EVENT_TABLE(ProcPanel, wxPanel)
    EVT_LIST_COL_CLICK(wxID_ANY, ProcPanel::OnColClick)
    EVT_LIST_ITEM_ACTIVATED(wxID_ANY, ProcPanel::OnItemActivated)
wxEND_EVENT_TABLE()

ProcPanel::ProcPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
    BuildUI();

    timer = new wxTimer(this);
    timer->Bind(wxEVT_TIMER, [this](wxTimerEvent&) { RefreshData(); });
    timer->Start(2000);
}

void ProcPanel::BuildUI() {
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    // Top bar: count + filter
    wxBoxSizer* top_row = new wxBoxSizer(wxHORIZONTAL);
    count_label = new wxStaticText(this, wxID_ANY, "Processes: —");
    count_label->SetForegroundColour(wxColour(180, 180, 180));
    top_row->Add(count_label, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    top_row->AddStretchSpacer();

    wxStaticText* filter_lbl = new wxStaticText(this, wxID_ANY, "Filter:");
    filter_lbl->SetForegroundColour(wxColour(140, 140, 140));
    top_row->Add(filter_lbl, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    filter_text = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(160, -1));
    filter_text->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { UpdateList(); });
    top_row->Add(filter_text, 0, wxALL, 4);

    main_sizer->Add(top_row, 0, wxEXPAND);

    // Process list
    proc_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(700, 400),
                               wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL);
    proc_list->AppendColumn("PID", wxLIST_FORMAT_RIGHT, 60);
    proc_list->AppendColumn("Name", wxLIST_FORMAT_LEFT, 150);
    proc_list->AppendColumn("CPU%", wxLIST_FORMAT_RIGHT, 60);
    proc_list->AppendColumn("MEM", wxLIST_FORMAT_RIGHT, 80);
    proc_list->AppendColumn("User", wxLIST_FORMAT_LEFT, 100);
    proc_list->AppendColumn("Threads", wxLIST_FORMAT_RIGHT, 60);
    proc_list->AppendColumn("State", wxLIST_FORMAT_LEFT, 80);
    main_sizer->Add(proc_list, 1, wxEXPAND | wxALL, 4);

    SetSizer(main_sizer);
}

void ProcPanel::RefreshData() {
    Proc::collect(false);
    UpdateList();
}

void ProcPanel::UpdateList() {
    auto& procs = Proc::current_procs;
    string filter = filter_text->GetValue().Lower().ToStdString();

    // Filter + sort
    vector<Proc::proc_info*> filtered;
    for (auto& p : procs) {
        if (!filter.empty()) {
            string name_lower = p.name;
            transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
            if (name_lower.find(filter) == string::npos) continue;
        }
        filtered.push_back(&p);
    }

    // Sort
    sort(filtered.begin(), filtered.end(), [this](Proc::proc_info* a, Proc::proc_info* b) {
        auto cmp = [&](auto&& val_a, auto&& val_b) {
            return sort_asc ? val_a < val_b : val_a > val_b;
        };
        switch (sort_col) {
            case 0: return cmp(a->pid, b->pid);
            case 1: return cmp(a->name, b->name);
            case 2: return cmp(a->cpu_p, b->cpu_p);
            case 3: return cmp(a->mem, b->mem);
            case 4: return cmp(a->user, b->user);
            case 5: return cmp(a->threads, b->threads);
            default: return cmp(a->cpu_p, b->cpu_p);
        }
    });

    count_label->SetLabel(wxString::Format("Processes: %zu", filtered.size()));

    proc_list->DeleteAllItems();
    for (size_t i = 0; i < filtered.size(); i++) {
        auto& p = *filtered[i];
        long idx = proc_list->InsertItem(i, wxString::Format("%zu", p.pid));
        proc_list->SetItem(idx, 1, wxString::FromUTF8(p.name));
        proc_list->SetItem(idx, 2, wxString::Format("%.1f", p.cpu_p));
        proc_list->SetItem(idx, 3, wxString::FromUTF8(human_readable(p.mem)));
        proc_list->SetItem(idx, 4, wxString::FromUTF8(p.user));
        proc_list->SetItem(idx, 5, wxString::Format("%zu", p.threads));
        string state_str(1, p.state);
        proc_list->SetItem(idx, 6, wxString::FromUTF8(state_str));

        // Color CPU-heavy processes
        if (p.cpu_p > 50)
            proc_list->SetItemTextColour(idx, wxColour(255, 150, 100));
        else if (p.cpu_p > 20)
            proc_list->SetItemTextColour(idx, wxColour(255, 220, 140));
    }
}

void ProcPanel::OnColClick(wxListEvent& evt) {
    int col = evt.GetColumn();
    if (col == sort_col) sort_asc = !sort_asc;
    else { sort_col = col; sort_asc = false; }
    UpdateList();
}

void ProcPanel::OnItemActivated(wxListEvent& evt) {
    // Could show detailed process info
}

// ─── MainFrame ─────────────────────────────────────────────────

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_TIMER(wxID_ANY, MainFrame::OnTimer)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "btop++ GUI", wxDefaultPosition, wxSize(960, 700))
{
    SetBackgroundColour(wxColour(40, 40, 40));

    notebook = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE);

    cpu_panel = new CpuPanel(notebook);
    notebook->AddPage(cpu_panel, "CPU", true);

    mem_panel = new MemPanel(notebook);
    notebook->AddPage(mem_panel, "Memory");

    net_panel = new NetPanel(notebook);
    notebook->AddPage(net_panel, "Network");

    proc_panel = new ProcPanel(notebook);
    notebook->AddPage(proc_panel, "Processes");

    // Refresh timer for title bar updates
    refresh_timer = new wxTimer(this);
    refresh_timer->Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        // Update window title with CPU usage
        auto& cpu = Cpu::current_cpu;
        if (cpu.cpu_percent.contains("total"s) && !cpu.cpu_percent.at("total"s).empty()) {
            long long cpu_pct = cpu.cpu_percent.at("total"s).back();
            SetTitle(wxString::Format("btop++ GUI — CPU: %lld%%", cpu_pct));
        }
    });
    refresh_timer->Start(2000);

    CreateStatusBar();
    SetStatusText("Ready");

    Maximize();
}

void MainFrame::OnTimer(wxTimerEvent&) {
    // Handled by individual panel timers
}

void MainFrame::OnClose(wxCloseEvent& evt) {
    Destroy();
}

// ─── App ───────────────────────────────────────────────────────

bool BtopApp::OnInit() {
    // Initialize btop backend
    Config::unlock();
    Config::lock();

    try {
        Shared::init();
    } catch (const exception& e) {
        wxMessageBox(wxString::FromUTF8(string("Failed to init btop backend: ") + e.what()),
                     "Error", wxOK | wxICON_ERROR);
        return false;
    }

    // Do initial data collection so panels have data
    Cpu::collect(false);
    Mem::collect(false);
    Net::collect(false);
    Proc::collect(false);

    MainFrame* frame = new MainFrame();
    frame->Show(true);
    return true;
}

int BtopApp::OnExit() {
    return 0;
}

wxIMPLEMENT_APP(BtopApp);
