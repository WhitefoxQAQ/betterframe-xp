#ifndef FRAME_STATS_HPP_INCLUDED
#define FRAME_STATS_HPP_INCLUDED
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace vkBasalt
{
    // Collects the cost of betterframe-xp itself and writes it once a second to
    // $XDG_RUNTIME_DIR/betterframe-xp/stats (key = value lines) for the panel.
    //   gpu: time between the GPU timestamps around our command buffer
    //   cpu: wall time spent inside our vkQueuePresentKHR hook
    class FrameStats
    {
    public:
        FrameStats();
        void addGpuNs(double ns);
        void addCpuNs(double ns);
        // Time the real vkQueuePresentKHR call blocked the compositor thread.
        void addPresentNs(double ns);
        // Stamp taken at the start of vkQueuePresentKHR (before our submit), for the late-frame trace.
        void presentEntered();
        // GPU time of our command buffer read back this frame (ns), 0 if none.
        void noteGpuNs(double ns) { lastGpuNs = ns; }
        // Counts one presented frame and writes the file when a second has passed.
        // Also tracks the gap between consecutive frames (max, and how many were > 1.25x the average).
        void frameDone();

    private:
        std::string                           path;
        std::chrono::steady_clock::time_point windowStart;
        uint64_t                              frames   = 0;
        uint64_t                              gpuCount = 0;
        double                                gpuSum   = 0.0;
        double                                gpuMax   = 0.0;
        uint64_t                              cpuCount = 0;
        double                                cpuSum   = 0.0;
        double                                cpuMax   = 0.0;
        double                                presentMax = 0.0;
        double                                presentSum = 0.0;
        uint64_t                              presentCount = 0;
        std::chrono::steady_clock::time_point lastFrame{};
        std::vector<double>                   gaps; // frame gaps in ms within the window
        std::chrono::steady_clock::time_point enteredAt{};
        double                                lastGpuNs = 0.0;
        double                                presentLastNs = 0.0;
        double                                lastEnterGapMs = 0.0; // time between two present entries
        FILE*                                 trace = nullptr;       // late-frame trace (betterframe-xp/stats.late)
        // Every frame, kept in memory and appended to stats.frames once a second:
        // float gap, enterGap, layerGpu, presentBlock (ms), double epoch (s). 24 bytes each.
        struct FrameRecord { float gap, enterGap, layerGpu, presentBlock; double epoch; };
        std::vector<FrameRecord>              records;
        FILE*                                 framesFile = nullptr;
        // Both files live in RAM (tmpfs) and are appended to across sessions, so each is capped: past the limit it
        // moves to "<name>.1" (replacing the older one) and starts again. At most two generations exist.
        size_t                                traceBytes  = 0;
        size_t                                framesBytes = 0;
        static void                           append(FILE*& file, size_t& bytes, const std::string& path, size_t limit, const void* data, size_t length);
        void                                  write(double seconds);
    };
} // namespace vkBasalt

#endif // FRAME_STATS_HPP_INCLUDED
