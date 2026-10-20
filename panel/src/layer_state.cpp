#include "layer_state.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

std::string homeDir() {
    const char* home = std::getenv("HOME");
    return home && *home ? home : "/tmp";
}

bool exists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

/** /proc から vrcompositor のプロセス ID を探す（無ければ -1）。 */
long findVrcompositor() {
    DIR* dir = opendir("/proc");
    if (!dir) return -1;
    long found = -1;
    while (const dirent* ent = readdir(dir)) {
        const char* name = ent->d_name;
        if (name[0] < '0' || name[0] > '9') continue;
        char path[320];
        std::snprintf(path, sizeof(path), "/proc/%s/comm", name);
        FILE* f = std::fopen(path, "r");
        if (!f) continue;
        char comm[64] = {0};
        const bool ok = std::fgets(comm, sizeof(comm), f) != nullptr;
        std::fclose(f);
        // comm は名前の 15 字まで。"vrcompositor" の後ろは改行か終わり
        if (ok && std::strncmp(comm, "vrcompositor", 12) == 0 && (comm[12] == '\n' || comm[12] == '\0')) {
            found = std::atol(name);
            break;
        }
    }
    closedir(dir);
    return found;
}

}  // namespace

LayerState readLayerState() {
    const std::string home = homeDir();
    LayerState s;
    // betterframectl enable が作るドロップイン（guard で止めたときは無効扱い）。status と同じ判定
    s.enabled = exists(home + "/.config/systemd/user/steamvr.service.d/91-betterframe-xp.conf") &&
                !exists(home + "/.config/betterframe-xp/guard-disabled");
    const long pid = findVrcompositor();
    if (pid < 0) return s;

    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%ld/maps", pid);
    FILE* maps = std::fopen(path, "r");
    if (!maps) return s;  // 読めないときは判断しない（vrcompositorFound は false のまま）
    s.vrcompositorFound = true;
    char line[4096];
    while (std::fgets(line, sizeof(line), maps)) {
        if (!std::strstr(line, "libbetterframexp")) continue;
        s.loaded = true;
        // 入れ替えは mv で行うので、読み込み中の古いファイルは "(deleted)" と出る
        if (std::strstr(line, "(deleted)")) s.stale = true;
    }
    std::fclose(maps);
    return s;
}
