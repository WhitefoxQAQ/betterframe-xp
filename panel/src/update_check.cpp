#include "update_check.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <utility>

namespace {

std::string homeDir() {
    const char* home = std::getenv("HOME");
    return home && *home ? home : "/tmp";
}

/** コマンドを実行して標準出力を全部読む（失敗したら、そこまでの分）。別スレッドで使う。 */
std::string runCommand(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    std::string out;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), pipe)) out += buf;
    pclose(pipe);
    return out;
}

/** betterframe-xp-update を引数つきで、別スレッドで呼ぶ（時間切れは 30 秒）。 */
std::future<std::string> runUpdater(const std::string& args) {
    const std::string cmd = "timeout 30 \"" + homeDir() + "/.local/libexec/betterframe-xp-update\" " + args + " 2>/dev/null";
    return std::async(std::launch::async, runCommand, cmd);
}

/** 結果が出ているか。待たずに調べる（取り出し済みや未開始なら false）。 */
bool ready(const std::future<std::string>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

std::string get(const std::map<std::string, std::string>& m, const char* key) {
    const auto it = m.find(key);
    return it == m.end() ? std::string() : it->second;
}

}  // namespace

std::map<std::string, std::string> parseFlat(const std::string& text) {
    std::map<std::string, std::string> out;
    size_t i = 0;
    while ((i = text.find('"', i)) != std::string::npos) {
        const size_t keyEnd = text.find('"', i + 1);
        if (keyEnd == std::string::npos) break;
        const std::string key = text.substr(i + 1, keyEnd - i - 1);
        const size_t colon = text.find(':', keyEnd);
        if (colon == std::string::npos) break;
        size_t v = colon + 1;
        while (v < text.size() && text[v] == ' ') ++v;
        if (v < text.size() && text[v] == '"') {
            const size_t end = text.find('"', v + 1);
            if (end == std::string::npos) break;
            out[key] = text.substr(v + 1, end - v - 1);
            i = end + 1;
        } else {
            size_t end = text.find_first_of(",}\n", v);
            if (end == std::string::npos) end = text.size();
            out[key] = text.substr(v, end - v);
            i = end;
        }
    }
    return out;
}

std::string installedVersion() {
    std::ifstream in(homeDir() + "/.local/share/betterframe-xp/VERSION");
    std::string v;
    in >> v;
    return v.empty() ? "?" : v;
}

Updater::Updater(std::string current) {
    info_.current = std::move(current);
}

void Updater::startCheck() {
    if (check_.valid()) return;  // 確認中
    info_.checking = true;
    check_ = runUpdater("check");
}

void Updater::startInstall() {
    if (install_.valid() || info_.install == "running" || !info_.available || !info_.installable) return;
    install_ = runUpdater("start");
    info_.install = "running";
    info_.step = "start";
    info_.installError.clear();
}

bool Updater::tick(double now) {
    bool changed = false;
    if (ready(check_)) {
        applyCheck(parseFlat(check_.get()), now);
        changed = true;
    }
    if (ready(install_)) {
        const Fields r = parseFlat(install_.get());
        // busy: another install is already running (the status poll shows it); started: it is running now
        if (get(r, "state") == "started" || get(r, "error") == "busy") {
            info_.install = "running";
        } else {
            info_.install = "failed";
            info_.installError = get(r, "error").empty() ? "other" : get(r, "error");
        }
        changed = true;
    }
    if (ready(status_)) {
        applyStatus(parseFlat(status_.get()));
        changed = true;
    }
    if (autoCheck_ && !check_.valid() && now - lastCheck_ >= 3600) {
        startCheck();
        changed = true;
    }
    // 進みは、インストールの開始の答えが来てから読む（開始前の古い状態を拾わないように）
    const bool needStatus = !statusOnce_ || info_.install == "running";
    if (needStatus && !status_.valid() && !install_.valid() && now - lastStatus_ >= 2) {
        status_ = runUpdater("status");
        lastStatus_ = now;
        statusOnce_ = true;
    }
    return changed;
}

void Updater::applyCheck(const Fields& r, double now) {
    info_.checking = false;
    info_.checked = true;
    lastCheck_ = now;
    if (get(r, "state") == "error") {
        const std::string code = get(r, "error");
        info_.error = code.empty() ? "other" : code;
        info_.latest.clear();
        info_.url.clear();
        info_.available = false;
        info_.installable = false;
        return;
    }
    info_.error.clear();
    info_.latest = get(r, "latest");
    info_.url = get(r, "url");
    info_.available = get(r, "state") == "available";
    info_.installable = get(r, "installable") == "yes";
}

void Updater::applyStatus(const Fields& r) {
    const std::string st = get(r, "state");
    if (st == "running") {
        info_.install = "running";
        info_.step = get(r, "step");
    } else if (st == "done") {
        const std::string v = get(r, "version");
        // 入れた版をもう動かしている（パネルを開き直した後）なら、知らせる必要はない
        info_.install = v == info_.current ? "" : "done";
        info_.installVersion = v;
        info_.step.clear();
    } else if (st == "failed") {
        info_.install = "failed";
        info_.installVersion = get(r, "version");
        const std::string code = get(r, "error");
        info_.installError = code.empty() ? "other" : code;
        info_.step.clear();
    } else {
        info_.install.clear();  // idle（まだ一度も入れていない）
        info_.step.clear();
    }
}
