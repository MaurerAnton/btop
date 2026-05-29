// btop-gui: wxWidgets GUI for btop++ — implementation v2
#include "btop_gui.h"
#include <sstream>
#include <iomanip>
#include <atomic>

using namespace std;

// ─── Stubs for btop.o symbols ────────────────────────────────
namespace Runner {
    atomic<bool> stopping{false};
    atomic<bool> coreNum_reset{false};
    atomic<bool> active{false};
    atomic<bool> redraw{false};
    bool pause_output = false;
}
namespace Global {
    const string Version = "1.4.7-gui";
    string overlay, exit_error_msg, clock;
    atomic<bool> resized{false};
    atomic<bool> init_conf{true};
    atomic<bool> quitting{false};
    uid_t real_uid = 0, set_uid = 0;
    const vector<array<string, 2>> Banner_src;
}
namespace Cpu { int width = 100, min_width = 60, min_height = 8; }
namespace Mem { int width = 300, min_width = 36, min_height = 6; bool redraw = true; }
namespace Net { int width = 300, min_width = 36, min_height = 6; bool redraw = true; }
namespace Proc { int width = 300, min_width = 44, min_height = 16; bool redraw = true, shown = true;
    int select_max = 1, selected_pid = 0, start = 0, selected = 0, selected_depth = 0;
    string selected_name; }
namespace Gpu { int width = 100, min_width = 36, count = 0, shown = 0; }
namespace Menu { bool active = false, redraw = false; }
void clean_quit(int) {}
namespace Input { unordered_map<string, array<int, 4>> mouse_mappings; }

// ─── Helpers ──────────────────────────────────────────────────

static wxColour cpu_color(double pct) {
    if (pct > 90) return wxColour(220, 50, 50);
    if (pct > 75) return wxColour(240, 150, 30);
    if (pct > 50) return wxColour(220, 200, 40);
    return wxColour(60, 180, 75);
}

static string human_bytes(uint64_t b) {
    const char* u[] = {"B","KB","MB","GB","TB"};
    double v = b; int i = 0;
    while (v >= 1000 && i < 4) { v/=1000; i++; }
    ostringstream ss;
    ss << fixed << setprecision(v<10?1:0) << v << " " << u[i];
    return ss.str();
}

static string speed_str(uint64_t bps) {
    return human_bytes(bps) + "/s";
}

static string sec_fmt(double s) {
    int d=s/86400, h=((int)s%86400)/3600, m=((int)s%3600)/60;
    char buf[64];
    if (d) snprintf(buf,sizeof(buf),"%dd %02d:%02d",d,h,m);
    else snprintf(buf,sizeof(buf),"%02d:%02d:%02d",h,m,(int)s%60);
    return buf;
}

// ─── GraphPanel ────────────────────────────────────────────────

wxBEGIN_EVENT_TABLE(GraphPanel, wxPanel)
    EVT_PAINT(GraphPanel::OnPaint)
    EVT_SIZE(GraphPanel::OnSize)
wxEND_EVENT_TABLE()

GraphPanel::GraphPanel(wxWindow* p, wxWindowID id, const wxPoint& pos, const wxSize& sz)
    : wxPanel(p, id, pos, sz, wxBORDER_SIMPLE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(80, 30));
}

void GraphPanel::SetData(const deque<long long>& d) { graph_data = d; Refresh(false); }

void GraphPanel::OnSize(wxSizeEvent&) { backbuf = wxBitmap(); Refresh(); }

void GraphPanel::OnPaint(wxPaintEvent&) {
    wxSize sz = GetClientSize();
    if (sz.x<4 || sz.y<4) return;
    if (!backbuf.IsOk()) backbuf.Create(sz.x, sz.y);
    wxBufferedPaintDC dc(this, backbuf);
    dc.SetBackground(wxBrush(wxColour(35,35,35)));
    dc.Clear();

    dc.SetTextForeground(wxColour(160,160,160));
    dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    dc.DrawText(title, 3, 2);

    int gx=2, gy=14, gw=sz.x-4, gh=sz.y-18;
    if (gh<4) return;
    dc.SetPen(wxPen(wxColour(60,60,60)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(gx, gy, gw, gh);

    if (graph_data.size() < 2) return;
    long long gmax = graph_max;
    if (autoscale) {
        gmax = max(1ll, *max_element(graph_data.begin(), graph_data.end()));
        gmax = max(gmax, graph_max);
    }
    int n = graph_data.size();
    double xs = (double)gw / max(1, n-1);
    double ys = (double)gh / max(1ll, gmax);
    dc.SetPen(wxPen(color, 1));
    int lx=-1, ly=-1;
    for (int i=0; i<n; i++) {
        int x = gx + (int)(i*xs);
        int y = gy+gh-1 - (int)(graph_data[i]*ys);
        y = max(gy, min(gy+gh-1, y));
        if (lx>=0) dc.DrawLine(lx, ly, x, y);
        lx=x; ly=y;
    }
    if (gmax>0) {
        dc.SetTextForeground(wxColour(100,100,100));
        dc.DrawText(speed_str(gmax), gx+2, gy+2);
    }
}

// ─── CPU Panel ────────────────────────────────────────────────

CpuPanel::CpuPanel(wxWindow* p) : wxPanel(p) {
    wxBoxSizer* ms = new wxBoxSizer(wxVERTICAL);

    // Info row
    wxBoxSizer* ir = new wxBoxSizer(wxHORIZONTAL);
    cpu_name_label = new wxStaticText(this, wxID_ANY, "CPU");
    cpu_name_label->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    cpu_name_label->SetForegroundColour(wxColour(220,220,220));
    ir->Add(cpu_name_label, 0, wxALL, 4);
    smt_label = new wxStaticText(this, wxID_ANY, "");
    smt_label->SetForegroundColour(wxColour(160,160,160));
    ir->Add(smt_label, 0, wxALL|wxALIGN_CENTER_VERTICAL, 4);
    ir->AddStretchSpacer();
    freq_label = new wxStaticText(this, wxID_ANY, "");
    freq_label->SetForegroundColour(wxColour(120,200,120));
    ir->Add(freq_label, 0, wxALL|wxALIGN_CENTER_VERTICAL, 4);
    ms->Add(ir, 0, wxEXPAND);

    // Total graph
    total_graph = new GraphPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 130));
    total_graph->SetTitle("CPU Usage %");
    total_graph->SetMax(100);
    total_graph->SetColor(wxColour(80,180,220));
    ms->Add(total_graph, 1, wxEXPAND|wxALL, 4);

    // Stats row
    wxBoxSizer* sr = new wxBoxSizer(wxHORIZONTAL);
    load_label = new wxStaticText(this, wxID_ANY, "Load: --");
    load_label->SetForegroundColour(wxColour(200,180,100));
    sr->Add(load_label, 0, wxALL, 4);
    uptime_label = new wxStaticText(this, wxID_ANY, "Up: --");
    uptime_label->SetForegroundColour(wxColour(140,180,200));
    sr->Add(uptime_label, 0, wxALL, 4);
    temp_label = new wxStaticText(this, wxID_ANY, "Temp: --");
    temp_label->SetForegroundColour(wxColour(220,140,60));
    sr->Add(temp_label, 0, wxALL, 4);
    battery_label = new wxStaticText(this, wxID_ANY, "");
    battery_label->SetForegroundColour(wxColour(180,200,100));
    sr->Add(battery_label, 0, wxALL, 4);
    ms->Add(sr, 0, wxEXPAND);

    // Per-core header
    wxStaticText* ch = new wxStaticText(this, wxID_ANY, "Per-Core Usage");
    ch->SetForegroundColour(wxColour(160,160,160));
    ch->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    ms->Add(ch, 0, wxALL, 4);

    // Core grid
    wxFlexGridSizer* cg = new wxFlexGridSizer(0, 3, 2, 4);
    for (long i=0; i<Shared::coreCount; i++) {
        wxStaticText* lb = new wxStaticText(this, wxID_ANY,
            wxString::Format("C%ld", i), wxDefaultPosition, wxSize(32,16));
        lb->SetForegroundColour(wxColour(180,180,180));
        cg->Add(lb);

        wxGauge* g = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(80,14));
        core_gauges.push_back(g);
        cg->Add(g);

        GraphPanel* gp = new GraphPanel(this, wxID_ANY, wxDefaultPosition, wxSize(100,28));
        gp->SetMax(100);
        gp->SetColor(wxColour(80,160,220));
        core_graphs.push_back(gp);
        cg->Add(gp);
    }
    ms->Add(cg, 0, wxEXPAND|wxALL, 4);

    SetSizerAndFit(ms);

    timer = new wxTimer(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&){ RefreshData(); }, timer->GetId());
    timer->Start(1500);
}

void CpuPanel::RefreshData() {
    Cpu::collect(false);
    UpdateStats();
}

void CpuPanel::UpdateStats() {
    auto& cpu = Cpu::current_cpu;
    if (cpu.cpu_percent.contains("total"s))
        total_graph->SetData(cpu.cpu_percent.at("total"s));

    string name = Cpu::cpuName;
#ifdef __linux__
    if (Shared::physical_cores>0) {
        if (Shared::smt_enabled)
            name += " [" + to_string(Shared::physical_cores) + "C/" + to_string(Shared::coreCount) + "T]";
        else
            name += " [" + to_string(Shared::physical_cores) + "C]";
    }
    smt_label->SetLabel(Shared::smt_enabled ? "SMT ON" : "");
#endif
    cpu_name_label->SetLabel(wxString::FromUTF8(name));

    if (!Cpu::cpuHz.empty())
        freq_label->SetLabel(wxString::FromUTF8(Cpu::cpuHz));

    for (size_t i=0; i<core_graphs.size() && i<cpu.core_percent.size(); i++) {
        core_graphs[i]->SetData(cpu.core_percent[i]);
        if (!cpu.core_percent[i].empty()) {
            int pct = (int)cpu.core_percent[i].back();
            core_gauges[i]->SetValue(pct);
            core_gauges[i]->SetForegroundColour(cpu_color(pct));
        }
    }

    if (!cpu.load_avg.empty()) {
        ostringstream ss;
        ss << "Load: ";
        for (size_t i=0; i<min((size_t)3,cpu.load_avg.size()); i++) {
            if (i) ss<<" ";
            ss<<fixed<<setprecision(2)<<cpu.load_avg[i];
        }
        load_label->SetLabel(wxString::FromUTF8(ss.str()));
    }

    double up = Tools::system_uptime();
    uptime_label->SetLabel(wxString::FromUTF8("Up: "+sec_fmt(up)));

    if (Cpu::has_battery) {
        auto [pct, watts, secs, status] = Cpu::current_bat;
        ostringstream bs;
        bs << "BAT: " << pct << "%";
        if (secs>0) bs << " (" << sec_fmt(secs) << ")";
        if (watts>=0) bs << " " << fixed << setprecision(1) << watts << "W";
        battery_label->SetLabel(wxString::FromUTF8(bs.str()));
    }

    if (Cpu::got_sensors && !cpu.temp.empty() && !cpu.temp[0].empty()) {
        long long t = cpu.temp[0].back();
        ostringstream ts;
        ts << "Temp: " << t << "\u00B0C";
        temp_label->SetLabel(wxString::FromUTF8(ts.str()));
    }

    GetSizer()->Layout();
}

// ─── Memory Panel ──────────────────────────────────────────────

MemPanel::MemPanel(wxWindow* p) : wxPanel(p) {
    wxBoxSizer* ms = new wxBoxSizer(wxVERTICAL);

    total_label = new wxStaticText(this, wxID_ANY, "Total: --");
    total_label->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    total_label->SetForegroundColour(wxColour(200,200,200));
    ms->Add(total_label, 0, wxALL, 4);

    wxStaticText* rt = new wxStaticText(this, wxID_ANY, "RAM");
    rt->SetForegroundColour(wxColour(140,200,140));
    rt->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    ms->Add(rt, 0, wxALL, 4);

    ram_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 24));
    ms->Add(ram_gauge, 0, wxEXPAND|wxALL, 4);

    ram_label = new wxStaticText(this, wxID_ANY, "Used: -- / Avail: -- / Cache: -- / Free: --");
    ram_label->SetForegroundColour(wxColour(180,180,180));
    ms->Add(ram_label, 0, wxALL, 4);

    wxStaticText* st = new wxStaticText(this, wxID_ANY, "Swap");
    st->SetForegroundColour(wxColour(200,160,60));
    st->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    ms->Add(st, 0, wxALL, 4);

    swap_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 24));
    ms->Add(swap_gauge, 0, wxEXPAND|wxALL, 4);

    swap_label = new wxStaticText(this, wxID_ANY, "Used: -- / Free: --");
    swap_label->SetForegroundColour(wxColour(180,180,180));
    ms->Add(swap_label, 0, wxALL, 4);

    wxStaticText* dt = new wxStaticText(this, wxID_ANY, "Disks");
    dt->SetForegroundColour(wxColour(140,180,220));
    dt->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    ms->Add(dt, 0, wxALL, 4);

    disk_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 180),
                               wxLC_REPORT|wxLC_SINGLE_SEL);
    disk_list->AppendColumn("Mount", wxLIST_FORMAT_LEFT, 100);
    disk_list->AppendColumn("Name", wxLIST_FORMAT_LEFT, 80);
    disk_list->AppendColumn("Total", wxLIST_FORMAT_RIGHT, 80);
    disk_list->AppendColumn("Used", wxLIST_FORMAT_RIGHT, 80);
    disk_list->AppendColumn("Free", wxLIST_FORMAT_RIGHT, 80);
    disk_list->AppendColumn("Use%", wxLIST_FORMAT_RIGHT, 60);
    disk_list->AppendColumn("IO Read", wxLIST_FORMAT_RIGHT, 90);
    disk_list->AppendColumn("IO Write", wxLIST_FORMAT_RIGHT, 90);
    ms->Add(disk_list, 1, wxEXPAND|wxALL, 4);

    SetSizerAndFit(ms);

    timer = new wxTimer(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&){ RefreshData(); }, timer->GetId());
    timer->Start(2000);
}

void MemPanel::RefreshData() {
    Mem::collect(false);
    UpdateStats();
}

void MemPanel::UpdateStats() {
    auto& mem = Mem::current_mem;
    uint64_t total = Mem::get_totalMem();
    total_label->SetLabel(wxString::FromUTF8("Total: "+human_bytes(total)));

    uint64_t used = mem.stats["used"], available = mem.stats["available"];
    uint64_t cached = mem.stats["cached"], free = mem.stats["free"];
    int ram_pct = total>0 ? (int)(used*100/total) : 0;
    ram_gauge->SetValue(ram_pct);
    if (ram_pct>90) ram_gauge->SetForegroundColour(wxColour(220,50,50));
    else if (ram_pct>70) ram_gauge->SetForegroundColour(wxColour(240,150,30));
    else ram_gauge->SetForegroundColour(wxColour(60,180,75));

    ostringstream rs;
    rs << "Used: "<<human_bytes(used)<<" / Avail: "<<human_bytes(available)
       << " / Cache: "<<human_bytes(cached)<<" / Free: "<<human_bytes(free)
       << "  ("<<ram_pct<<"%)";
    ram_label->SetLabel(wxString::FromUTF8(rs.str()));

    if (Mem::has_swap) {
        uint64_t su = mem.stats["swap_used"], sf = mem.stats["swap_free"];
        uint64_t st = mem.stats["swap_total"];
        int sp = st>0 ? (int)(su*100/st) : 0;
        swap_gauge->SetValue(sp);
        ostringstream ss;
        ss << "Used: "<<human_bytes(su)<<" / Free: "<<human_bytes(sf)<<" / Total: "<<human_bytes(st);
        swap_label->SetLabel(wxString::FromUTF8(ss.str()));
    }

    disk_list->DeleteAllItems();
    int idx=0;
    for (auto& mnt : mem.disks_order) {
        if (!mem.disks.contains(mnt)) continue;
        auto& d = mem.disks.at(mnt);
        disk_list->InsertItem(idx, wxString::FromUTF8(mnt));
        disk_list->SetItem(idx, 1, wxString::FromUTF8(d.name));
        disk_list->SetItem(idx, 2, wxString::FromUTF8(human_bytes(d.total)));
        disk_list->SetItem(idx, 3, wxString::FromUTF8(human_bytes(d.used)));
        disk_list->SetItem(idx, 4, wxString::FromUTF8(human_bytes(d.free)));
        disk_list->SetItem(idx, 5, wxString::Format("%d%%", d.used_percent));
        disk_list->SetItem(idx, 6, d.io_read.empty() ? "---" : speed_str(d.io_read.back()));
        disk_list->SetItem(idx, 7, d.io_write.empty() ? "---" : speed_str(d.io_write.back()));
        idx++;
    }

    GetSizer()->Layout();
}

// ─── Network Panel ─────────────────────────────────────────────

NetPanel::NetPanel(wxWindow* p) : wxPanel(p) {
    wxBoxSizer* ms = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* ir2 = new wxBoxSizer(wxHORIZONTAL);
    ir2->Add(new wxStaticText(this, wxID_ANY, "Interface:"), 0, wxALL|wxALIGN_CENTER_VERTICAL, 4);
    iface_choice = new wxComboBox(this, wxID_ANY, "", wxDefaultPosition, wxSize(150,-1));
    iface_choice->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&){
        Net::selected_iface = iface_choice->GetValue().ToStdString();
    });
    ir2->Add(iface_choice, 0, wxALL, 4);
    ip_label = new wxStaticText(this, wxID_ANY, "");
    ip_label->SetForegroundColour(wxColour(140,180,220));
    ir2->Add(ip_label, 0, wxALL|wxALIGN_CENTER_VERTICAL, 4);
    iface_label = new wxStaticText(this, wxID_ANY, "");
    ir2->Add(iface_label, 0, wxALL|wxALIGN_CENTER_VERTICAL, 4);
    ms->Add(ir2, 0, wxEXPAND);

    // Download
    wxStaticText* dlt = new wxStaticText(this, wxID_ANY, "\u25BC Download");
    dlt->SetForegroundColour(wxColour(120,200,120));
    dlt->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    ms->Add(dlt, 0, wxALL, 4);

    dl_graph = new GraphPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 100));
    dl_graph->SetTitle("Download");
    dl_graph->SetColor(wxColour(60,200,80));
    ms->Add(dl_graph, 0, wxEXPAND|wxALL, 4);

    dl_speed_label = new wxStaticText(this, wxID_ANY, "Speed: --");
    dl_speed_label->SetForegroundColour(wxColour(200,200,200));
    ms->Add(dl_speed_label, 0, wxALL, 4);

    dl_total_label = new wxStaticText(this, wxID_ANY, "Total: --");
    dl_total_label->SetForegroundColour(wxColour(160,160,160));
    ms->Add(dl_total_label, 0, wxALL, 4);

    dl_avg_label = new wxStaticText(this, wxID_ANY, "5m Avg: --");
    dl_avg_label->SetForegroundColour(wxColour(140,140,200));
    ms->Add(dl_avg_label, 0, wxALL, 4);

    // Upload
    wxStaticText* ult = new wxStaticText(this, wxID_ANY, "\u25B2 Upload");
    ult->SetForegroundColour(wxColour(200,140,60));
    ult->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    ms->Add(ult, 0, wxALL, 4);

    ul_graph = new GraphPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 100));
    ul_graph->SetTitle("Upload");
    ul_graph->SetColor(wxColour(220,140,40));
    ms->Add(ul_graph, 0, wxEXPAND|wxALL, 4);

    ul_speed_label = new wxStaticText(this, wxID_ANY, "Speed: --");
    ul_speed_label->SetForegroundColour(wxColour(200,200,200));
    ms->Add(ul_speed_label, 0, wxALL, 4);

    ul_total_label = new wxStaticText(this, wxID_ANY, "Total: --");
    ul_total_label->SetForegroundColour(wxColour(160,160,160));
    ms->Add(ul_total_label, 0, wxALL, 4);

    ul_avg_label = new wxStaticText(this, wxID_ANY, "5m Avg: --");
    ul_avg_label->SetForegroundColour(wxColour(140,140,200));
    ms->Add(ul_avg_label, 0, wxALL, 4);

    SetSizerAndFit(ms);

    timer = new wxTimer(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&){ RefreshData(); }, timer->GetId());
    timer->Start(1500);
}

void NetPanel::RefreshData() {
    Net::collect(false);
    UpdateStats();
}

void NetPanel::UpdateStats() {
    iface_choice->Clear();
    for (auto& iface : Net::interfaces)
        iface_choice->Append(wxString::FromUTF8(iface));
    if (!Net::selected_iface.empty())
        iface_choice->SetStringSelection(wxString::FromUTF8(Net::selected_iface));
    if (!Net::current_net.contains(Net::selected_iface)) return;

    auto& net = Net::current_net.at(Net::selected_iface);
    string ip = net.ipv4.empty() ? net.ipv6 : net.ipv4;
    ip_label->SetLabel(wxString::FromUTF8(ip));
    iface_label->SetLabel(net.connected ? "\u25CF Connected" : "\u25CB Disconnected");
    iface_label->SetForegroundColour(net.connected ? wxColour(120,220,120) : wxColour(180,100,100));

    if (net.bandwidth.contains("download"s)) {
        dl_graph->SetData(net.bandwidth.at("download"s));
        auto& s = net.stat.at("download"s);
        dl_speed_label->SetLabel(wxString::FromUTF8("Speed: "+speed_str(s.speed)));
        dl_total_label->SetLabel(wxString::FromUTF8("Total: "+human_bytes(s.total)));
        if (s.avg_speed>0) dl_avg_label->SetLabel(wxString::FromUTF8("5m Avg: "+speed_str(s.avg_speed)));
    }
    if (net.bandwidth.contains("upload"s)) {
        ul_graph->SetData(net.bandwidth.at("upload"s));
        auto& s = net.stat.at("upload"s);
        ul_speed_label->SetLabel(wxString::FromUTF8("Speed: "+speed_str(s.speed)));
        ul_total_label->SetLabel(wxString::FromUTF8("Total: "+human_bytes(s.total)));
        if (s.avg_speed>0) ul_avg_label->SetLabel(wxString::FromUTF8("5m Avg: "+speed_str(s.avg_speed)));
    }

    GetSizer()->Layout();
}

// ─── Process Panel ─────────────────────────────────────────────

wxBEGIN_EVENT_TABLE(ProcPanel, wxPanel)
    EVT_LIST_COL_CLICK(wxID_ANY, ProcPanel::OnColClick)
wxEND_EVENT_TABLE()

ProcPanel::ProcPanel(wxWindow* p) : wxPanel(p) {
    wxBoxSizer* ms = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* tr = new wxBoxSizer(wxHORIZONTAL);
    count_label = new wxStaticText(this, wxID_ANY, "Processes: --");
    count_label->SetForegroundColour(wxColour(180,180,180));
    tr->Add(count_label, 0, wxALL|wxALIGN_CENTER_VERTICAL, 4);
    tr->AddStretchSpacer();
    tr->Add(new wxStaticText(this, wxID_ANY, "Filter:"), 0, wxALL|wxALIGN_CENTER_VERTICAL, 4);
    filter_text = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(160,-1));
    filter_text->Bind(wxEVT_TEXT, [this](wxCommandEvent&){ UpdateList(); });
    tr->Add(filter_text, 0, wxALL, 4);
    ms->Add(tr, 0, wxEXPAND);

    proc_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 300),
                               wxLC_REPORT|wxLC_SINGLE_SEL);
    proc_list->AppendColumn("PID", wxLIST_FORMAT_RIGHT, 60);
    proc_list->AppendColumn("Name", wxLIST_FORMAT_LEFT, 160);
    proc_list->AppendColumn("CPU%", wxLIST_FORMAT_RIGHT, 60);
    proc_list->AppendColumn("MEM", wxLIST_FORMAT_RIGHT, 80);
    proc_list->AppendColumn("User", wxLIST_FORMAT_LEFT, 100);
    proc_list->AppendColumn("Threads", wxLIST_FORMAT_RIGHT, 60);
    proc_list->AppendColumn("State", wxLIST_FORMAT_LEFT, 80);
    ms->Add(proc_list, 1, wxEXPAND|wxALL, 4);

    SetSizerAndFit(ms);

    timer = new wxTimer(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&){ RefreshData(); }, timer->GetId());
    timer->Start(2000);
}

void ProcPanel::RefreshData() {
    Proc::collect(false);
    UpdateList();
}

void ProcPanel::UpdateList() {
    auto& procs = Proc::current_procs;
    string filter = filter_text->GetValue().Lower().ToStdString();

    vector<Proc::proc_info*> filt;
    for (auto& p : procs) {
        if (!filter.empty()) {
            string nl = p.name;
            transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
            if (nl.find(filter) == string::npos) continue;
        }
        filt.push_back(&p);
    }

    sort(filt.begin(), filt.end(), [this](auto* a, auto* b){
        auto c = [&](auto va, auto vb){ return sort_asc ? va<vb : va>vb; };
        switch(sort_col) {
            case 0: return c(a->pid, b->pid);
            case 1: return c(a->name, b->name);
            case 2: return c(a->cpu_p, b->cpu_p);
            case 3: return c(a->mem, b->mem);
            case 4: return c(a->user, b->user);
            case 5: return c(a->threads, b->threads);
            default: return c(a->cpu_p, b->cpu_p);
        }
    });

    count_label->SetLabel(wxString::Format("Processes: %zu", filt.size()));
    proc_list->DeleteAllItems();
    for (size_t i=0; i<filt.size(); i++) {
        auto& p = *filt[i];
        long idx = proc_list->InsertItem(i, wxString::Format("%zu", p.pid));
        proc_list->SetItem(idx, 1, wxString::FromUTF8(p.name));
        proc_list->SetItem(idx, 2, wxString::Format("%.1f", p.cpu_p));
        proc_list->SetItem(idx, 3, wxString::FromUTF8(human_bytes(p.mem)));
        proc_list->SetItem(idx, 4, wxString::FromUTF8(p.user));
        proc_list->SetItem(idx, 5, wxString::Format("%zu", p.threads));
        proc_list->SetItem(idx, 6, wxString::FromUTF8(string(1, p.state)));
        if (p.cpu_p > 50) proc_list->SetItemTextColour(idx, wxColour(255,150,100));
        else if (p.cpu_p > 20) proc_list->SetItemTextColour(idx, wxColour(255,220,140));
    }
}

void ProcPanel::OnColClick(wxListEvent& evt) {
    int col = evt.GetColumn();
    if (col == sort_col) sort_asc = !sort_asc;
    else { sort_col = col; sort_asc = false; }
    UpdateList();
}

// ─── MainFrame ─────────────────────────────────────────────────

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "btop++ GUI", wxDefaultPosition, wxSize(960, 680)) {
    SetBackgroundColour(wxColour(40,40,40));
    notebook = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxAUI_NB_TOP|wxAUI_NB_TAB_SPLIT|wxAUI_NB_TAB_MOVE);

    cpu_panel = new CpuPanel(notebook);
    notebook->AddPage(cpu_panel, "CPU", true);
    mem_panel = new MemPanel(notebook);
    notebook->AddPage(mem_panel, "Memory");
    net_panel = new NetPanel(notebook);
    notebook->AddPage(net_panel, "Network");
    proc_panel = new ProcPanel(notebook);
    notebook->AddPage(proc_panel, "Processes");

    CreateStatusBar();
    SetStatusText("Ready");
    Maximize();

    // Force initial data refresh after UI is visible
    CallAfter([this](){
        if (cpu_panel) cpu_panel->RefreshData();
        if (mem_panel) mem_panel->RefreshData();
        if (net_panel) net_panel->RefreshData();
        if (proc_panel) proc_panel->RefreshData();
    });
}

void MainFrame::OnClose(wxCloseEvent&) { Destroy(); }

// ─── App ───────────────────────────────────────────────────────

bool BtopApp::OnInit() {
    try {
        Shared::init();
    } catch (const exception& e) {
        wxMessageBox("Backend init failed: " + string(e.what()), "Error", wxOK|wxICON_ERROR);
        return false;
    }

    // Initial data collection
    Cpu::collect(false);
    Mem::collect(false);
    Net::collect(false);
    Proc::collect(false);

    MainFrame* f = new MainFrame();
    f->Show(true);
    return true;
}

int BtopApp::OnExit() { return 0; }

wxIMPLEMENT_APP(BtopApp);
