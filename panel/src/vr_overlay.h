// Connection to OpenVR, and the dashboard panel (plus its thumbnail).
#pragma once

#include "vk_texture.h"

#include <cstdint>
#include <string>
#include <vector>

/** A pointer action on the dashboard panel (coordinates are px, origin at the image's top-left). */
struct PointerInput {
    enum class Type { Move, Down, Up, Leave };
    Type type;
    double x = 0.0;
    double y = 0.0;
};

/** Result of pollEvents(). */
struct VrEvents {
    bool quit = false;                  ///< SteamVR asked us to quit (VREvent_Quit; SteamVR itself is shutting down)
    bool closeRequested = false;        ///< the dashboard icon's "close" was pressed (VREvent_OverlayClosed)
    std::vector<PointerInput> pointer;  ///< actions on the panel (in arrival order)
};

/**
 * Wraps the connection to OpenVR as an overlay app.
 * Creates and shows a dashboard panel. The only SteamVR settings it ever writes are the two
 * eye-tracking corrections (steamvr.gazeDepReprojectEnable / gazeDepEyeToHeadEnable), and only
 * when the user flips them on the SteamVR tab (setSteamVrBool).
 * Images are sent as Vulkan textures via SetOverlayTexture rather than SetOverlayRaw.
 */
class VrOverlay {
public:
    /** Result of connect(). */
    enum class ConnectResult {
        Ok,          ///< connected
        NotRunning,  ///< SteamVR isn't running (wait and retry)
        Error,       ///< any other failure
    };

    VrOverlay();
    ~VrOverlay();
    VrOverlay(const VrOverlay&) = delete;
    VrOverlay& operator=(const VrOverlay&) = delete;

    /**
     * If SteamVR is running, connect as an overlay app and set up Vulkan and the dashboard panel.
     * Checks first with a Background-type init to avoid accidentally launching SteamVR if it
     * isn't running.
     * @param width panel image width (px)
     * @param height panel image height (px)
     * @param message reason for failure
     * @return the connection result
     */
    ConnectResult connect(int width, int height, std::string& message);

    /**
     * Tear down in a safe order: clear the texture -> destroy the overlay -> wait a few compositor
     * frames -> VR_Shutdown -> destroy the Vulkan images and device. Every API return value is
     * logged.
     */
    void shutdown();

    /**
     * Process pending events. Answers SteamVR's shutdown (VREvent_Quit) with AcknowledgeQuit_Exiting.
     * @return quit request and panel pointer actions
     */
    VrEvents pollEvents();

    /**
     * Whether the vrserver process found at connect time is still alive.
     * @return true if alive (or if it can't be determined)
     */
    bool steamVrAlive() const;

    /**
     * Whether the panel (this app selected on the dashboard) is currently visible.
     * @return true if visible
     */
    bool panelVisible() const;

    /**
     * Open the dashboard and show this app's panel (IVROverlay::ShowDashboard).
     */
    void showPanel();

    /**
     * Send the dashboard thumbnail image (once, right after connecting).
     * @param rgba non-premultiplied RGBA
     * @param size side length in px
     * @return true if it was sent
     */
    bool submitThumbnail(const uint8_t* rgba, int size);

    /**
     * Send the panel image.
     * @param rgba non-premultiplied 8-bit RGBA (sized as given to connect)
     * @return true if it was sent
     */
    bool submitPanel(const uint8_t* rgba);

    /**
     * Read SteamVR's two eye-tracking corrections (steamvr.gazeDepReprojectEnable /
     * steamvr.gazeDepEyeToHeadEnable) with IVRSettings::GetBool (same as `vrcmd --settings-bool`;
     * the driver's built-in default is returned when the user file has no entry).
     * @param reproject depth (gaze-aware reprojection) correction
     * @param eyeToHead eye position (pupil compensation) correction
     * @return true if both could be read
     */
    bool readGazeSettings(bool& reproject, bool& eyeToHead) const;

    /**
     * Write a bool in SteamVR's `steamvr` section (IVRSettings::SetBool, same as
     * `vrcmd --set-settings-bool`). Saved to steamvr.vrsettings.
     * @param key the key (gazeDepReprojectEnable or gazeDepEyeToHeadEnable)
     * @param value the new value
     * @param message reason for failure
     * @return true if written
     */
    bool setSteamVrBool(const char* key, bool value, std::string& message);

    /**
     * Diagnostic: look up the overlay again by key and log its texture size and so on.
     * @param when what point this check is at (for logging)
     */
    void logOverlayState(const char* when) const;

    /**
     * Diagnostic (--probe): connect as Background type, look up the running instance's dashboard
     * overlay by key, and print whether it's visible, its flags, and its texture size to stdout.
     * Creates neither an overlay nor Vulkan.
     * @return 0 if found, 1 if SteamVR isn't running or the overlay wasn't found
     */
    static int probe();

private:
    bool connected_ = false;
    uint64_t dashboardHandle_ = 0;  ///< vr::VROverlayHandle_t (the dashboard panel)
    uint64_t thumbnailHandle_ = 0;  ///< the dashboard thumbnail
    int panelHeight_ = 0;
    int vrserverPid_ = -1;
    std::string lastPanelError_;

    VulkanContext vulkan_;
    OverlayTexture panelTexture_;
    OverlayTexture thumbnailTexture_;

    /**
     * Scan /proc once to find the vrserver PID.
     * @return the PID found, or -1 if not found
     */
    static int findVrserverPid();
};
