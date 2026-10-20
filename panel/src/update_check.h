// 新しい版の確認と、新しい版のインストール。どちらも contrib/betterframe-xp-update（POSIX sh）がやる。
// パネルは別スレッド（std::async）で呼び、結果はメインループの tick で受け取る（描画を止めない）。
#pragma once

#include <future>
#include <map>
#include <string>

/** 更新タブに出す内容（確認の結果と、インストールの進み）。 */
struct UpdateInfo {
    std::string current;         ///< 動いている版（起動時に VERSION から読んだもの）
    std::string latest;          ///< 最新のリリースの版（確認できたとき）
    std::string url;             ///< そのリリースのページ
    std::string error;           ///< 確認できなかった理由（not-found / network / rate-limited / bad-version / bad-response / other）
    bool checking = false;       ///< 確認の途中
    bool checked = false;        ///< 一度でも確認が終わった
    bool available = false;      ///< 最新が動いている版より新しい
    bool installable = false;    ///< リリースに入れるファイル（tar.gz と SHA256SUMS）がそろっている
    std::string install;         ///< インストール: "" / running / done / failed
    std::string step;            ///< running のとき: start / download / verify / extract / install
    std::string installVersion;  ///< done / failed のとき、入れようとした版
    std::string installError;    ///< failed のとき: no-checksums / checksum-mismatch / install-failed など
};

/**
 * 確認とインストールを、描画を止めずに行う。メインループから毎回 tick を呼ぶ。
 * 起動直後と 1 時間おきに確認する。インストールは systemd の一時ユニットで走り、パネルが閉じても続く。
 */
class Updater {
public:
    /** @param current 動いている版（VERSION） */
    explicit Updater(std::string current);
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    /** 今すぐ確かめる。確認中なら何もしない。 */
    void startCheck();

    /** 新しい版を入れ始める。新しい版があって、ここから入れられるときだけ。 */
    void startInstall();

    /**
     * 終わった結果を取り込み、インストールの進みを読み直す。毎フレーム呼んでよい（重い処理は別スレッド）。
     * @param now 単調増加の時刻（秒）
     * @return 表示が変わったら true
     */
    bool tick(double now);

    /** @param on false にすると、起動直後と 1 時間おきの自動の確認をしない（「今すぐ確認」は効く） */
    void setAutoCheck(bool on) { autoCheck_ = on; }

    /** @return 今の表示内容 */
    const UpdateInfo& info() const { return info_; }

private:
    using Fields = std::map<std::string, std::string>;

    void applyCheck(const Fields& r, double now);
    void applyStatus(const Fields& r);

    UpdateInfo info_;
    std::future<std::string> check_;    ///< 確認（betterframe-xp-update check）
    std::future<std::string> install_;  ///< インストールの開始（betterframe-xp-update start）
    std::future<std::string> status_;   ///< 進みの読み直し（betterframe-xp-update status）
    double lastCheck_ = -1e9;
    double lastStatus_ = -1e9;
    bool statusOnce_ = false;
    bool autoCheck_ = true;
};

/**
 * 動いている版（~/.local/share/betterframe-xp/VERSION）。無ければ "?"。
 * @return 版の番号
 */
std::string installedVersion();

/**
 * betterframe-xp-update の出力（1 行の平らな JSON）を key → 値 に読む。入れ子は読まない。
 * @param text JSON の文字列
 * @return key → 値（文字列は引用符を外したもの。true / false / 数は書かれた形のまま）
 */
std::map<std::string, std::string> parseFlat(const std::string& text);
