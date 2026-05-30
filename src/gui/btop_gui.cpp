// btop-gui v3 — single dashboard layout matching terminal btop
#include "btop_gui.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <atomic>
#include <cmath>

using namespace std;

// ─── Stubs ─────────────────────────────────────────────────
namespace Runner {
    atomic<bool> stopping{false}, coreNum_reset{false}, active{false}, redraw{false};
    bool pause_output = false;
}
namespace Global {
    const string Version = "1.4.7-gui";
    string overlay, exit_error_msg, clock;
    atomic<bool> resized{false}, init_conf{true}, quitting{false};
    uid_t real_uid=0, set_uid=0;
    const vector<array<string,2>> Banner_src;
}
namespace Cpu { int width=100, min_width=60, min_height=8; }
namespace Mem { int width=300, min_width=36, min_height=6; bool redraw=true; }
namespace Net { int width=300, min_width=36, min_height=6; bool redraw=true; }
namespace Proc { int width=300, min_width=44, min_height=16; bool redraw=true, shown=true;
    int select_max=1, selected_pid=0, start=0, selected=0, selected_depth=0;
    string selected_name; }
namespace Gpu { int width=100, min_width=36, count=0, shown=0; }
namespace Menu { bool active=false, redraw=false; }
void clean_quit(int) {}
namespace Input { unordered_map<string, array<int,4>> mouse_mappings; }

// ─── Color scheme (matches btop's default theme) ────────────
static wxColour BG(30,30,30);
static wxColour BOX_BG(35,35,35);
static wxColour BORDER(60,60,60);
static wxColour TITLE_FG(100,200,240);
static wxColour MAIN_FG(200,200,200);
static wxColour DIV(70,70,70);
static wxColour GRAPH_CPU(80,180,220);
static wxColour GRAPH_DL(60,200,80);
static wxColour GRAPH_UL(220,140,40);
static wxColour GAUGE_RAM(60,180,75);
static wxColour GAUGE_SWAP(200,160,60);
static wxColour GAUGE_USED(180,50,50);
static wxColour GAUGE_CACHED(220,180,40);

static string human_bytes(uint64_t b) {
    const char* u[]={"B","KB","MB","GB","TB"};
    double v=b; int i=0;
    while(v>=1000&&i<4){v/=1000;i++;}
    ostringstream ss;
    ss<<fixed<<setprecision(v<10?1:0)<<v<<" "<<u[i];
    return ss.str();
}
static string speed_str(uint64_t bps) { return human_bytes(bps)+"/s"; }
static string sec_fmt(double s) {
    int d=s/86400, h=((int)s%86400)/3600, m=((int)s%3600)/60;
    char b[64];
    if(d) snprintf(b,sizeof(b),"%dd %02d:%02d",d,h,m);
    else snprintf(b,sizeof(b),"%02d:%02d:%02d",h,m,(int)s%60);
    return b;
}

// ─── GraphStrip ────────────────────────────────────────────
wxBEGIN_EVENT_TABLE(GraphStrip, wxWindow)
    EVT_PAINT(GraphStrip::OnPaint)
wxEND_EVENT_TABLE()

GraphStrip::GraphStrip(wxWindow* p, const wxSize& sz) : wxWindow(p, wxID_ANY, wxDefaultPosition, sz) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(20,8));
}

void GraphStrip::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    wxSize sz = GetClientSize();
    dc.SetBackground(wxBrush(BOX_BG));
    dc.Clear();
    if (data.size()<2 || sz.x<4 || sz.y<2) return;
    long long mx = gmax;
    if (autoscale) {
        mx = max(1ll, *max_element(data.begin(), data.end()));
        mx = max(mx, gmax);
    }
    int n=data.size(), gw=sz.x, gh=sz.y;
    double xs=(double)gw/max(1,n-1), ys=(double)gh/max(1ll,mx);
    dc.SetPen(wxPen(color,1));
    int lx=-1, ly=-1;
    for(int i=0;i<n;i++) {
        int x=(int)(i*xs), y=gh-1-(int)(data[i]*ys);
        y=max(0,min(gh-1,y));
        if(lx>=0) dc.DrawLine(lx,ly,x,y);
        lx=x; ly=y;
    }
}

// ─── Dashboard ─────────────────────────────────────────────
wxBEGIN_EVENT_TABLE(Dashboard, wxScrolledWindow)
    EVT_PAINT(Dashboard::OnPaint)
wxEND_EVENT_TABLE()

Dashboard::Dashboard(wxWindow* p) : wxScrolledWindow(p, wxID_ANY) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetScrollRate(8,8);
    SetVirtualSize(800, 1600);
    SetBackgroundColour(BG);

    timer = new wxTimer(this);
    timer->Bind(wxEVT_TIMER, [this](wxTimerEvent&){
        // Collect all data inline
        Cpu::collect(false);
        Mem::collect(false);
        Net::collect(false);
        Proc::collect(false);
        // Store CPU
        auto& cpu = Cpu::current_cpu;
        if (cpu.cpu_percent.contains("total"s)) state.cpu_total = cpu.cpu_percent.at("total"s);
        state.cpu_name = Cpu::cpuName;
        state.cpu_freq = Cpu::cpuHz;
        for (size_t i=0; i<cpu.core_percent.size(); i++)
            if (i>=state.cpu_cores.size()) state.cpu_cores.push_back(cpu.core_percent[i]);
            else state.cpu_cores[i] = cpu.core_percent[i];
        if (!cpu.load_avg.empty()) {
            state.loadavg[0]=cpu.load_avg[0];
            state.loadavg[1]=cpu.load_avg.size()>1?cpu.load_avg[1]:0;
            state.loadavg[2]=cpu.load_avg.size()>2?cpu.load_avg[2]:0;
        }
        if (Cpu::got_sensors && !cpu.temp.empty() && !cpu.temp[0].empty()) {
            state.cpu_temp = cpu.temp[0].back();
            state.cpu_tmax = cpu.temp_max>0 ? cpu.temp_max : 90;
        }
        if (Cpu::has_battery) {
            auto [pct, w, secs, status] = Cpu::current_bat;
            state.battery_pct = pct;
        }
        // Store Mem
        auto& mem = Mem::current_mem;
        state.mem_total = Mem::get_totalMem();
        state.mem_used = mem.stats["used"];
        state.mem_avail = mem.stats["available"];
        state.mem_cache = mem.stats["cached"];
        state.mem_free = mem.stats["free"];
        state.swap_total = mem.stats["swap_total"];
        state.swap_used = mem.stats["swap_used"];
        state.swap_free = mem.stats["swap_free"];
        // Store Net
        if (Net::current_net.contains(Net::selected_iface)) {
            auto& net = Net::current_net.at(Net::selected_iface);
            if (net.bandwidth.contains("download"s)) {
                state.net_dl = net.bandwidth.at("download"s);
                state.net_dl_speed = net.stat.at("download"s).speed;
                state.net_dl_total = net.stat.at("download"s).total;
                state.net_dl_avg = net.stat.at("download"s).avg_speed;
            }
            if (net.bandwidth.contains("upload"s)) {
                state.net_ul = net.bandwidth.at("upload"s);
                state.net_ul_speed = net.stat.at("upload"s).speed;
                state.net_ul_total = net.stat.at("upload"s).total;
                state.net_ul_avg = net.stat.at("upload"s).avg_speed;
            }
            state.net_ip = net.ipv4.empty() ? net.ipv6 : net.ipv4;
            state.net_connected = net.connected;
            state.net_iface = Net::selected_iface;
        }
        // Store Proc
        state.procs = Proc::current_procs;
        Refresh(false);
    });
    timer->Start(1500);
}

void Dashboard::OnTimer(wxTimerEvent& evt) {
    (void)evt;
}

void Dashboard::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    DoPaint(dc);
}

void Dashboard::DoPaint(wxDC& dc) {
    wxSize sz = GetClientSize();
    int w = max(400, sz.x);
    int total_h = 1600; // will be adjusted

    // If virtual size needs updating
    if (w != paint_w) {
        paint_w = w;
    }

    dc.SetBackground(wxBrush(BG));
    dc.Clear();

    int y = 4, x = 2;
    int cw = w - 4;

    DrawCPU(dc, y, x, cw);
    DrawMem(dc, y, x, cw);
    DrawNet(dc, y, x, cw);
    DrawProc(dc, y, x, cw);

    // Update virtual height
    int new_h = y + 8;
    if (abs(new_h - paint_h) > 20) {
        paint_h = new_h;
        SetVirtualSize(w, paint_h);
    }
}

// ─── Box drawing ──────────────────────────────────────────
void Dashboard::DrawBox(wxDC& dc, int x, int y, int w, int h,
                         const wxString& title, const wxColour& border) {
    dc.SetPen(wxPen(border));
    dc.SetBrush(wxBrush(BOX_BG));
    dc.DrawRectangle(x, y, w, h);

    if (!title.empty()) {
        dc.SetTextForeground(TITLE_FG);
        wxFont f(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
        dc.SetFont(f);
        int tw, th;
        dc.GetTextExtent(title, &tw, &th);
        int tx = x + (w - tw) / 2;
        dc.DrawText(title, tx, y + 1);
    }
}

// ─── CPU section ──────────────────────────────────────────
void Dashboard::DrawCPU(wxDC& dc, int& y, int x, int w) {
    int bw = w;
    int title_h = 18, core_h = 12, bar_h = 10, gap = 3;

    // Box
    int box_h = title_h + 76 + gap + 14 + gap + 14 + gap;
    // Per-core lines
    int ncores = max(1, (int)state.cpu_cores.size());
    int cols = max(1, (bw - 20) / 160);
    int rows = (ncores + cols - 1) / cols;
    box_h += rows * (core_h + gap) + 4;

    DrawBox(dc, x, y, bw, box_h, "", BORDER);

    // Title
    dc.SetTextForeground(TITLE_FG);
    wxFont tf(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    dc.SetFont(tf);
    wxString ttl = wxString::FromUTF8(state.cpu_name);
#ifdef __linux__
    if (Shared::physical_cores>0) {
        ttl += wxString::Format(" [%ldC]", Shared::physical_cores);
        if (Shared::smt_enabled) ttl += wxString::Format("/%ldT", Shared::coreCount);
    }
#endif
    dc.DrawText(ttl, x+4, y+2);
    if (!state.cpu_freq.empty()) {
        dc.SetTextForeground(wxColour(120,200,120));
        int tw, th;
        dc.GetTextExtent(wxString::FromUTF8(state.cpu_freq), &tw, &th);
        dc.DrawText(wxString::FromUTF8(state.cpu_freq), x+bw-tw-6, y+2);
    }
    dc.SetTextForeground(TITLE_FG);
    y += title_h;

    // CPU total graph
    int gy = y, gh = 64, gw = bw - 8;
    dc.SetPen(wxPen(wxColour(50,50,50)));
    dc.SetBrush(wxBrush(wxColour(25,25,25)));
    dc.DrawRectangle(x+4, gy, gw, gh);
    DrawLineGraph(dc, x+4, gy, gw, gh, state.cpu_total, 100, GRAPH_CPU, false);

    // CPU% label
    long long cpu_pct = state.cpu_total.empty() ? 0 : state.cpu_total.back();
    dc.SetTextForeground(wxColour(220,220,220));
    wxFont bigf(20, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    dc.SetFont(bigf);
    dc.DrawText(wxString::Format("%lld%%", cpu_pct), x+gw/2-25, gy+gh/2-12);

    y += gh + gap;

    // Load avg + uptime + temp + battery
    dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    ostringstream info;
    info << "Load: " << fixed << setprecision(2) << state.loadavg[0]
         << " " << state.loadavg[1] << " " << state.loadavg[2]
         << "  |  Up: " << sec_fmt(Tools::system_uptime());
    if (state.cpu_temp > 0)
        info << "  |  Temp: " << state.cpu_temp << "C";
    if (state.battery_pct >= 0)
        info << "  |  BAT: " << state.battery_pct << "%";
    dc.SetTextForeground(wxColour(160,160,160));
    dc.DrawText(wxString::FromUTF8(info.str()), x+6, y);
    y += 16;

    // Per-core bars
    for (int r=0; r<rows; r++) {
        int cy = y;
        for (int c=0; c<cols; c++) {
            int idx = r*cols + c;
            if (idx >= ncores) break;
            int cx = x + 4 + c * (bw/cols);
            int cw2 = bw/cols - 4;

            // Core label
            dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            dc.SetTextForeground(wxColour(140,140,140));
            dc.DrawText(wxString::Format("C%d", idx), cx, cy);

            // Mini graph
            int mgx = cx + 24, mgw = cw2 - 70, mgh = core_h;
            dc.SetPen(wxPen(wxColour(40,40,40)));
            dc.SetBrush(wxBrush(wxColour(25,25,25)));
            dc.DrawRectangle(mgx, cy, mgw, mgh);
            if (idx < (int)state.cpu_cores.size())
                DrawLineGraph(dc, mgx, cy, mgw, mgh, state.cpu_cores[idx], 100,
                              wxColour(80,160,220), false);

            // Percent bar + number
            int bx = cx + cw2 - 44, bw3 = 40;
            double pct = (idx < (int)state.cpu_cores.size() && !state.cpu_cores[idx].empty())
                         ? state.cpu_cores[idx].back() : 0;
            DrawGauge(dc, bx, cy+2, bw3, mgh-4, pct/100.0,
                      pct>90?wxColour(220,50,50):pct>75?wxColour(240,150,30):wxColour(60,180,75));
            dc.SetTextForeground(wxColour(200,200,200));
            dc.DrawText(wxString::Format("%.0f%%", pct), bx+bw3+2, cy+1);
        }
        y += core_h + gap;
    }
    y += 6;
}

// ─── Memory/Disks section ────────────────────────────────
void Dashboard::DrawMem(wxDC& dc, int& y, int x, int w) {
    int bw = w, gap = 3;
    int lines = 10; // total, ram bar, ram details, swap bar, swap details, disk header + 4-5 disks
    auto& mem = Mem::current_mem;
    int disks = min(6, (int)mem.disks_order.size());
    int box_h = 22 + 40 + gap + 20 + 40 + gap + 20 + gap + 16 + disks*16;

    DrawBox(dc, x, y, bw, box_h, "", BORDER);

    // Title
    dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    dc.SetTextForeground(TITLE_FG);
    dc.DrawText("Memory & Disks", x+4, y+2);
    y += 22;

    // RAM
    dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    dc.SetTextForeground(MAIN_FG);
    double ram_pct = state.mem_total>0 ? (double)state.mem_used/state.mem_total : 0;
    dc.DrawText(wxString::Format("RAM  %s / %s", human_bytes(state.mem_used), human_bytes(state.mem_total)), x+6, y);
    dc.DrawText(wxString::Format("%.0f%%", ram_pct*100), x+bw-40, y);
    y += 14;
    DrawGauge(dc, x+6, y, bw-12, 18, ram_pct, GAUGE_RAM);
    y += 22;

    dc.SetTextForeground(wxColour(140,140,140));
    dc.DrawText(wxString::Format("avail: %s  cache: %s  free: %s",
        human_bytes(state.mem_avail), human_bytes(state.mem_cache), human_bytes(state.mem_free)), x+6, y);
    y += 18;

    // Swap
    if (state.swap_total > 0) {
        double sw_pct = (double)state.swap_used/state.swap_total;
        dc.SetTextForeground(MAIN_FG);
        dc.DrawText(wxString::Format("Swap %s / %s", human_bytes(state.swap_used), human_bytes(state.swap_total)), x+6, y);
        dc.DrawText(wxString::Format("%.0f%%", sw_pct*100), x+bw-40, y);
        y += 14;
        DrawGauge(dc, x+6, y, bw-12, 14, sw_pct, GAUGE_SWAP);
        y += 18;
    }

    // Disks
    dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    dc.SetTextForeground(wxColour(140,180,220));
    dc.DrawText("Disks", x+6, y);
    dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    y += 16;

    int dcount = 0;
    for (auto& mnt : mem.disks_order) {
        if (!mem.disks.contains(mnt) || dcount>=8) continue;
        auto& d = mem.disks.at(mnt);
        dc.SetTextForeground(MAIN_FG);
        dc.DrawText(wxString::FromUTF8(mnt), x+6, y);

        double dp = d.total>0 ? (double)d.used/d.total : 0;
        DrawGauge(dc, x+80, y+2, bw-160, 10, dp,
            dp>0.9?wxColour(220,50,50):dp>0.75?wxColour(240,150,30):wxColour(60,180,75));

        ostringstream di;
        di << human_bytes(d.used) << "/" << human_bytes(d.total) << " (" << d.used_percent << "%)";
        dc.SetTextForeground(wxColour(150,150,150));
        dc.DrawText(wxString::FromUTF8(di.str()), x+bw-78, y);
        y += 14;
        dcount++;
    }
    y += 6;
}

// ─── Network section ──────────────────────────────────────
void Dashboard::DrawNet(wxDC& dc, int& y, int x, int w) {
    int bw = w, gh = 60, gap = 4;
    int box_h = 22 + 14 + gh + 16 + 16 + gap + 16 + gh + 16 + 16 + 12;

    DrawBox(dc, x, y, bw, box_h, "", BORDER);

    // Title
    dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    dc.SetTextForeground(TITLE_FG);
    wxString ntitle = wxString::Format("Network  %s  %s  %s",
        wxString::FromUTF8(state.net_iface),
        state.net_connected ? "\u25CF" : "\u25CB",
        wxString::FromUTF8(state.net_ip));
    dc.DrawText(ntitle, x+4, y+2);
    y += 22;

    dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

    // Download
    dc.SetTextForeground(wxColour(120,200,120));
    dc.DrawText("\u25BC Download", x+6, y);
    y += 14;

    int gx = x+6, gw2 = bw-12;
    dc.SetPen(wxPen(wxColour(50,50,50)));
    dc.SetBrush(wxBrush(wxColour(25,25,25)));
    dc.DrawRectangle(gx, y, gw2, gh);
    DrawLineGraph(dc, gx, y, gw2, gh, state.net_dl, 0, GRAPH_DL, true);
    y += gh;

    dc.SetTextForeground(MAIN_FG);
    dc.DrawText(wxString::Format("Speed: %s    Total: %s    5m avg: %s",
        speed_str(state.net_dl_speed), human_bytes(state.net_dl_total), speed_str(state.net_dl_avg)), x+6, y);
    y += 18;

    // Upload
    dc.SetTextForeground(wxColour(200,140,60));
    dc.DrawText("\u25B2 Upload", x+6, y);
    y += 14;

    dc.SetPen(wxPen(wxColour(50,50,50)));
    dc.SetBrush(wxBrush(wxColour(25,25,25)));
    dc.DrawRectangle(gx, y, gw2, gh);
    DrawLineGraph(dc, gx, y, gw2, gh, state.net_ul, 0, GRAPH_UL, true);
    y += gh;

    dc.SetTextForeground(MAIN_FG);
    dc.DrawText(wxString::Format("Speed: %s    Total: %s    5m avg: %s",
        speed_str(state.net_ul_speed), human_bytes(state.net_ul_total), speed_str(state.net_ul_avg)), x+6, y);
    y += 18;
}

// ─── Process section ──────────────────────────────────────
void Dashboard::DrawProc(wxDC& dc, int& y, int x, int w) {
    int bw = w, row_h = 14;
    int max_rows = 20;
    int procs = min(max_rows, (int)state.procs.size());
    int box_h = 22 + 14 + procs * row_h + 8;

    DrawBox(dc, x, y, bw, box_h, "", BORDER);

    // Title + count
    dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    dc.SetTextForeground(TITLE_FG);
    dc.DrawText(wxString::Format("Processes (%zu)", state.procs.size()), x+4, y+2);
    y += 22;

    // Column headers
    dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    dc.SetTextForeground(wxColour(140,140,140));
    int col_pid = x+6;
    int col_name = x+70;
    int col_cpu = x+bw-160;
    int col_mem = x+bw-100;
    int col_user = x+bw-260;
    dc.DrawText("PID", col_pid, y);
    dc.DrawText("Name", col_name, y);
    dc.DrawText("CPU%", col_cpu, y);
    dc.DrawText("MEM", col_mem, y);
    dc.DrawText("User", col_user-20, y);
    y += 16;

    dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

    for (int i=0; i<procs; i++) {
        auto& p = state.procs[i];
        dc.SetTextForeground(p.cpu_p>50?wxColour(255,140,100):p.cpu_p>20?wxColour(255,220,140):MAIN_FG);

        dc.DrawText(wxString::Format("%zu", p.pid), col_pid, y);

        string name = p.name;
        if ((int)name.size() > 30) name = name.substr(0, 27) + "...";
        dc.DrawText(wxString::FromUTF8(name), col_name, y);

        dc.DrawText(wxString::Format("%.1f", p.cpu_p), col_cpu, y);
        dc.DrawText(wxString::FromUTF8(human_bytes(p.mem)), col_mem, y);

        string user = p.user;
        if (user.size() > 12) user = user.substr(0,11);
        dc.DrawText(wxString::FromUTF8(user), col_user-20, y);

        y += row_h;
    }
    y += 6;
}

// ─── Gauge drawing ────────────────────────────────────────
void Dashboard::DrawGauge(wxDC& dc, int x, int y, int w, int h, double pct, const wxColour& c) {
    pct = max(0.0, min(1.0, pct));
    // Background
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(wxColour(50,50,50)));
    dc.DrawRectangle(x, y, w, h);
    // Filled
    int fw = (int)(w * pct);
    if (fw > 0) {
        dc.SetBrush(wxBrush(c));
        dc.DrawRectangle(x, y, fw, h);
    }
    // Border
    dc.SetPen(wxPen(wxColour(80,80,80)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(x, y, w, h);
}

// ─── Line graph ───────────────────────────────────────────
void Dashboard::DrawLineGraph(wxDC& dc, int x, int y, int w, int h,
                               const deque<long long>& d, long long maxv,
                               const wxColour& c, bool autoscale) {
    if (d.size()<2) return;
    long long mx = maxv;
    if (autoscale) {
        mx = max(1ll, *max_element(d.begin(), d.end()));
        mx = max(mx, maxv);
    }
    int n = d.size();
    dc.SetPen(wxPen(c, 1));
    int lx=-1, ly=-1;
    for (int i=0; i<n; i++) {
        int px = x + (int)((double)i*w/max(1,n-1));
        int py = y+h-1 - (int)((double)d[i]*h/max(1ll,mx));
        py = max(y, min(y+h-1, py));
        if (lx>=0) dc.DrawLine(lx, ly, px, py);
        lx=px; ly=py;
    }
}

// ─── MainFrame ────────────────────────────────────────────
wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_CLOSE(MainFrame::OnClose)
    EVT_KEY_DOWN(MainFrame::OnKeyDown)
wxEND_EVENT_TABLE()

MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "btop++ GUI",
                                  wxDefaultPosition, wxSize(960, 800)) {
    SetBackgroundColour(BG);
    dash = new Dashboard(this);
    Maximize();
}

void MainFrame::TakeScreenshot(const wxString& path) {
    wxSize sz = GetClientSize();
    wxBitmap bmp(sz.x, sz.y);
    wxMemoryDC mdc(bmp);
    mdc.SetBackground(wxBrush(BG));
    mdc.Clear();
    dash->DoPaint(mdc);
    mdc.SelectObject(wxNullBitmap);
    bmp.SaveFile(path, wxBITMAP_TYPE_PNG);
}

void MainFrame::OnClose(wxCloseEvent&) { Destroy(); }

void MainFrame::OnKeyDown(wxKeyEvent& evt) {
    if (evt.GetKeyCode() == 'S' && evt.ControlDown()) {
        TakeScreenshot("/tmp/btop-gui-screenshot.png");
    }
    evt.Skip();
}

// ─── App ──────────────────────────────────────────────────

bool BtopApp::OnInit() {
    // Check for screenshot mode
    bool shot = false;
    for (int i=1; i<argc; i++)
        if (string(argv[i]) == "--screenshot") shot = true;

    try { Shared::init(); }
    catch (const exception& e) {
        wxMessageBox("Backend init failed: "+string(e.what()), "Error");
        return false;
    }

    Cpu::collect(false);
    Mem::collect(false);
    Net::collect(false);
    Proc::collect(false);

    MainFrame* f = new MainFrame();
    f->Show(true);

    if (shot) {
        // Wait for first data refresh, then screenshot
        wxTimer* st = new wxTimer(f);
        st->Bind(wxEVT_TIMER, [f, st](wxTimerEvent&){
            f->TakeScreenshot("/tmp/btop-gui-screenshot.png");
            f->Close();
        });
        st->Start(3000);
    }

    return true;
}

int BtopApp::OnExit() { return 0; }
wxIMPLEMENT_APP(BtopApp);
