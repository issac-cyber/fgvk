#include <gtkmm.h>

#include "process.hpp"

#include "shared/protocol.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace proto = screenfg::protocol;

namespace {

bool fileExists(const std::string& p) {
    struct stat st;
    return ::access(p.c_str(), F_OK) == 0 || stat(p.c_str(), &st) == 0;
}

// 找 screen-fg binary：env → 相對候補 → PATH
std::string findBinary() {
    if (const char* e = std::getenv("SCREENFG_BIN"))
        if (fileExists(e))
            return e;
    std::vector<std::string> cands = {
        "build/screen-fg",
        "../screen-fg/build/screen-fg",
        "/usr/local/bin/screen-fg",
    };
    try {
        auto ex = fs::canonical(fs::absolute("/proc/self/exe"));
        cands.push_back((ex.parent_path() / ".." / "screen-fg" / "build" / "screen-fg").generic_string());
    } catch (...) {
    }
    for (auto& c : cands)
        if (fileExists(c))
            return fs::absolute(c).string();
    if (const char* path = std::getenv("PATH")) {
        std::string cur;
        for (const char* p = path; *p; ++p) {
            if (*p == ':') {
                if (!cur.empty() && fileExists(cur + "/screen-fg"))
                    return cur + "/screen-fg";
                cur.clear();
            } else {
                cur += *p;
            }
        }
        if (!cur.empty() && fileExists(cur + "/screen-fg"))
            return cur + "/screen-fg";
    }
    return {};
}

// 簡單「標題 + 後綴」列（取代 Adw::ActionRow，無 C++ binding）；suffix 為 managed 指標
Gtk::Box* makeRow(const std::string& title, Gtk::Widget* suffix) {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    auto lbl = Gtk::make_managed<Gtk::Label>(title);
    lbl->set_halign(Gtk::Align::START);
    box->append(*lbl);
    if (suffix) {
        suffix->set_hexpand(true);
        box->append(*suffix);
    }
    return box;
}

} // namespace

class MainWindow : public Gtk::Window {
public:
    MainWindow() {
        set_title("screen-fg");
        set_default_size(560, 640);

        box_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 16);
        box_->set_margin_top(16);
        box_->set_margin_bottom(16);
        box_->set_margin_start(16);
        box_->set_margin_end(16);
        set_child(*box_);

        // binary 路徑
        binEntry_ = Gtk::make_managed<Gtk::Entry>();
        std::string bin = findBinary();
        binEntry_->set_text(bin.empty() ? std::string("（找不到，請手動填路徑）") : bin);
        box_->append(*makeRow("screen-fg binary", binEntry_));

        // 啟動 / 停止
        startBtn_ = Gtk::make_managed<Gtk::Button>("啟動");
        startBtn_->set_hexpand(true);
        startBtn_->signal_clicked().connect([this] { onStartStop(); });
        box_->append(*startBtn_);

        // 狀態
        auto status = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
        status->set_halign(Gtk::Align::CENTER);
        fpsLbl_ = Gtk::make_managed<Gtk::Label>("FPS 0");
        fpsLbl_->add_css_class("title-2");
        multLbl_ = Gtk::make_managed<Gtk::Label>("x0");
        multLbl_->add_css_class("title-2");
        layerLbl_ = Gtk::make_managed<Gtk::Label>("layer off");
        stateLbl_ = Gtk::make_managed<Gtk::Label>("idle");
        stateLbl_->add_css_class("dim-label");
        status->append(*fpsLbl_);
        status->append(*multLbl_);
        status->append(*layerLbl_);
        status->append(*stateLbl_);
        box_->append(*status);

        // 暫停 / 恢復
        pauseBtn_ = Gtk::make_managed<Gtk::Button>("暫停");
        pauseBtn_->set_tooltip_text("暫停 = passthrough（無插幀）。注意：真實捕捉模式下暫停會重新啟動捕捉（重跑選窗）");
        pauseBtn_->signal_clicked().connect([this] { onPause(); });
        box_->append(*makeRow("暫停 / 恢復", pauseBtn_));

        // HUD
        hudSwitch_ = Gtk::make_managed<Gtk::Switch>();
        hudSwitch_->set_active(true);
        hudSwitch_->signal_state_set().connect([this](bool) { onHud(); return false; }, false);
        box_->append(*makeRow("HUD", hudSwitch_));

        // log
        logView_ = Gtk::make_managed<Gtk::TextView>();
        logView_->set_editable(false);
        logView_->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
        logView_->set_vexpand(true);
        auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        scroll->set_child(*logView_);
        scroll->set_vexpand(true);
        box_->append(*scroll);

        proc_.setStatusCb([this](const ScreenFgStatus& s) { onStatus(s); });
        proc_.setExitCb([this](int code) { onExit(code); });
        proc_.setLogCb([this](const std::string& line) { appendLog(line); });
    }

private:
    void onStartStop() {
        if (!proc_.running()) {
            std::string bin = binEntry_->get_text();
            if (bin.empty() || bin.find("（") != std::string::npos) {
                appendLog("錯誤：請先填 screen-fg binary 路徑");
                return;
            }
            if (proc_.start(bin, {})) {
                started_ = true;
                startBtn_->set_label("停止");
                stateLbl_->set_text("starting…");
                appendLog("已啟動 " + bin);
            } else {
                appendLog("錯誤：啟動失敗（fork/exec）");
            }
        } else {
            proc_.quit();
            appendLog("停止中…");
        }
    }

    void onPause() {
        bool toPause = lastState_ != proto::State::Paused;
        proc_.sendCommand(toPause ? proto::Cmd::Pause : proto::Cmd::Resume);
        if (toPause)
            appendLog("注意：暫停會重新啟動捕捉（真實捕捉模式下會重跑選窗）");
    }

    void onHud() {
        proc_.sendCommand(std::string(proto::Cmd::Hud) + " " + (hudSwitch_->get_active() ? "1" : "0"));
    }

    void onStatus(const ScreenFgStatus& s) {
        lastState_ = s.state;
        fpsLbl_->set_text("FPS " + std::to_string(s.fps));
        multLbl_->set_text("x" + std::to_string(s.mult));
        layerLbl_->set_text(s.layer ? "layer on" : "layer off");
        stateLbl_->set_text(s.state);
        pauseBtn_->set_label(s.state == proto::State::Paused ? "恢復" : "暫停");
    }

    void onExit(int code) {
        started_ = false;
        startBtn_->set_label("啟動");
        lastState_ = "";
        stateLbl_->set_text(code == 0 ? "已停止" : "錯誤結束 (" + std::to_string(code) + ")");
        fpsLbl_->set_text("FPS 0");
        appendLog("screen-fg 結束（code " + std::to_string(code) + "）");
    }

    void appendLog(const std::string& line) {
        auto buf = logView_->get_buffer();
        Gtk::TextIter begin, end;
        buf->get_bounds(begin, end);
        auto newEnd = buf->insert(end, line + "\n"); // 回傳插入文本末端的 iterator
        logView_->scroll_to(newEnd, 0.0);
    }

    Gtk::Box* box_ = nullptr;
    Gtk::Entry* binEntry_ = nullptr;
    Gtk::Button* startBtn_ = nullptr;
    Gtk::Label* fpsLbl_ = nullptr, * multLbl_ = nullptr, * layerLbl_ = nullptr, * stateLbl_ = nullptr;
    Gtk::Button* pauseBtn_ = nullptr;
    Gtk::Switch* hudSwitch_ = nullptr;
    Gtk::TextView* logView_ = nullptr;
    ScreenFgProcess proc_;
    bool started_ = false;
    std::string lastState_;
};

class GuiApp : public Gtk::Application {
    MainWindow* win_ = nullptr;
public:
    GuiApp() : Gtk::Application("com.issac.screenfg-gui") {}
    ~GuiApp() override {
        delete win_;
        win_ = nullptr;
    }
    void on_activate() override {
        if (!win_) {
            win_ = new MainWindow();
            add_window(*win_);
        }
        win_->present();
    }
};

int main(int argc, char** argv) {
    GuiApp app;
    return app.run(argc, argv);
}
