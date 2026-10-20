// SteamVR の再起動が要るかどうか。今の設定（有効のドロップイン）と、動いている vrcompositor が読んでいる
// レイヤーのファイルを比べる。SteamVR や設定は変えない（/proc と設定ファイルを読むだけ）。
#pragma once

/** 有効の設定と、動いている vrcompositor の状態。 */
struct LayerState {
    bool vrcompositorFound = false;  ///< 動いている vrcompositor が見つかった（無ければ判断できない）
    bool enabled = false;            ///< 次の SteamVR 起動で読む設定になっている（betterframectl enable 済み）
    bool loaded = false;             ///< vrcompositor がレイヤーを読み込んでいる
    bool stale = false;              ///< 読んでいるのは入れ替える前のファイル（インストールし直した）

    /** @return SteamVR を再起動すると今の設定どおりになる（つまり、再起動が要る） */
    bool restartNeeded() const { return vrcompositorFound && (enabled != loaded || (loaded && stale)); }
};

/**
 * 今の設定と、動いている vrcompositor の状態を読む。
 * @return 読んだ状態
 */
LayerState readLayerState();
