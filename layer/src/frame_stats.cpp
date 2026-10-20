#include "frame_stats.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace vkBasalt
{
    namespace
    {
        constexpr size_t kTraceLimit  = 8u << 20;  // stats.late: ~100 bytes per late frame
        constexpr size_t kFramesLimit = 32u << 20; // stats.frames: 24 bytes per frame, ~2.5 h at 144 fps
    } // namespace

    // Appends to "path", opening it on first use. When the file would pass "limit" it is closed, renamed to
    // "path.1" and started again. Failure to write is ignored: the stats are a diagnostic, not a feature.
    void FrameStats::append(FILE*& file, size_t& bytes, const std::string& path, size_t limit, const void* data, size_t length)
    {
        if (!file)
        {
            file = std::fopen(path.c_str(), "ab");
            if (!file) return;
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            bytes           = size > 0 ? static_cast<size_t>(size) : 0;
        }
        if (bytes + length > limit)
        {
            std::fclose(file);
            std::rename(path.c_str(), (path + ".1").c_str());
            file  = std::fopen(path.c_str(), "ab");
            bytes = 0;
            if (!file) return;
        }
        if (std::fwrite(data, 1, length, file) == length) bytes += length;
        std::fflush(file);
    }

    FrameStats::FrameStats()
    {
        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        std::string dir = runtime && *runtime ? std::string(runtime) : "/tmp/betterframe-xp-" + std::to_string(::getuid());
        if (runtime && *runtime) dir += "/betterframe-xp";
        ::mkdir(dir.c_str(), 0700);
        path        = dir + "/stats";
        windowStart = std::chrono::steady_clock::now();
    }

    void FrameStats::addGpuNs(double ns)
    {
        // Ignore nonsense (counter wrap, a frame that waited on something else)
        if (ns < 0.0 || ns > 100e6) return;
        gpuSum += ns;
        gpuMax = std::max(gpuMax, ns);
        ++gpuCount;
    }

    void FrameStats::addCpuNs(double ns)
    {
        cpuSum += ns;
        cpuMax = std::max(cpuMax, ns);
        ++cpuCount;
    }

    void FrameStats::presentEntered()
    {
        const auto now = std::chrono::steady_clock::now();
        if (enteredAt.time_since_epoch().count() != 0)
            lastEnterGapMs = std::chrono::duration<double, std::milli>(now - enteredAt).count();
        enteredAt = now;
    }

    void FrameStats::addPresentNs(double ns)
    {
        presentLastNs = ns;
        presentSum += ns;
        presentMax = std::max(presentMax, ns);
        ++presentCount;
    }

    void FrameStats::frameDone()
    {
        ++frames;
        const auto now = std::chrono::steady_clock::now();
        if (lastFrame.time_since_epoch().count() != 0)
            gaps.push_back(std::chrono::duration<double, std::milli>(now - lastFrame).count());
        // Trace every late frame: how long between entering the hook, our GPU time, how long present blocked.
        if (!gaps.empty() && gaps.back() > 10.0)
        {
            char line[160];
            const int length = std::snprintf(line, sizeof(line), "t=%.3f gap=%.2f enterGap=%.2f layerGpu=%.2f presentBlock=%.2f\n",
                                             std::chrono::duration<double>(now.time_since_epoch()).count(), gaps.back(), lastEnterGapMs,
                                             lastGpuNs / 1e6, presentLastNs / 1e6);
            if (length > 0 && length < static_cast<int>(sizeof(line)))
                append(trace, traceBytes, path + ".late", kTraceLimit, line, static_cast<size_t>(length));
        }
        if (!gaps.empty())
            records.push_back({static_cast<float>(gaps.back()), static_cast<float>(lastEnterGapMs), static_cast<float>(lastGpuNs / 1e6),
                               static_cast<float>(presentLastNs / 1e6), std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()});
        lastFrame = now;
        const double seconds = std::chrono::duration<double>(now - windowStart).count();
        if (seconds < 1.0) return;
        write(seconds);
        if (!records.empty())
            append(framesFile, framesBytes, path + ".frames", kFramesLimit, records.data(), records.size() * sizeof(FrameRecord));
        records.clear();
        windowStart = now;
        frames = gpuCount = cpuCount = presentCount = 0;
        gpuSum = gpuMax = cpuSum = cpuMax = presentSum = presentMax = 0.0;
        gaps.clear();
    }

    void FrameStats::write(double seconds)
    {
        const std::string tmp = path + ".tmp";
        FILE*             out = std::fopen(tmp.c_str(), "w");
        if (!out) return;
        const double fps = frames / seconds;
        std::fprintf(out, "time = %lld\n", static_cast<long long>(std::time(nullptr)));
        std::fprintf(out, "fps = %.2f\n", fps);
        std::fprintf(out, "gpuSamples = %llu\n", static_cast<unsigned long long>(gpuCount));
        std::fprintf(out, "gpuMsAvg = %.4f\n", gpuCount ? gpuSum / gpuCount / 1e6 : -1.0);
        std::fprintf(out, "gpuMsMax = %.4f\n", gpuCount ? gpuMax / 1e6 : -1.0);
        std::fprintf(out, "cpuUsAvg = %.2f\n", cpuCount ? cpuSum / cpuCount / 1e3 : -1.0);
        std::fprintf(out, "cpuUsMax = %.2f\n", cpuCount ? cpuMax / 1e3 : -1.0);
        std::fprintf(out, "presentMsAvg = %.3f\n", presentCount ? presentSum / presentCount / 1e6 : -1.0);
        std::fprintf(out, "presentMsMax = %.3f\n", presentCount ? presentMax / 1e6 : -1.0);
        // Frame gaps: the average shows the frame rate, the max and the count of long gaps show hitches.
        double gapMax = 0.0, gapSum = 0.0;
        for (double g : gaps) { gapMax = std::max(gapMax, g); gapSum += g; }
        const double gapAvg = gaps.empty() ? 0.0 : gapSum / gaps.size();
        size_t       late   = 0;
        for (double g : gaps) if (gapAvg > 0.0 && g > gapAvg * 1.25) ++late;
        std::fprintf(out, "gapMsAvg = %.3f\n", gaps.empty() ? -1.0 : gapAvg);
        std::fprintf(out, "gapMsMax = %.3f\n", gaps.empty() ? -1.0 : gapMax);
        std::fprintf(out, "gapLate = %zu\n", late);
        // Spread of the frame gaps: a steady 6.94 ms gives a small std and a p99 close to the average.
        double var = 0.0;
        for (double g : gaps) var += (g - gapAvg) * (g - gapAvg);
        std::vector<double> sorted(gaps);
        std::sort(sorted.begin(), sorted.end());
        std::fprintf(out, "gapMsStd = %.3f\n", gaps.empty() ? -1.0 : std::sqrt(var / gaps.size()));
        std::fprintf(out, "gapMsP99 = %.3f\n", gaps.empty() ? -1.0 : sorted[static_cast<size_t>(sorted.size() * 0.99)]);
        std::fclose(out);
        std::rename(tmp.c_str(), path.c_str());
    }
} // namespace vkBasalt
