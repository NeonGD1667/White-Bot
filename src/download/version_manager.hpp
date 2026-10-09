#pragma once

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Một release trên GitHub (đã parse sẵn).
struct ReleaseInfo {
    std::string tag;          // "v3.0.1"
    std::string name;         // tiêu đề release
    std::string body;         // release notes (markdown)
    std::string publishedAt;  // "2026-10-09"
    std::string downloadUrl;  // URL asset .geode, rỗng = release không có file .geode
    bool prerelease = false;
    geode::VersionInfo version;
};

struct ReleaseListResult {
    bool ok = false;
    int code = 0;
    std::string error;
    std::vector<ReleaseInfo> releases;  // đã sắp xếp mới -> cũ
};

struct DownloadResult {
    bool ok = false;
    int code = 0;
    std::string error;
    std::string version;                // version đọc từ mod.json trong file đã tải
    std::vector<std::string> warnings;  // cảnh báo tương thích (Geode / GD version)
};

class UpdaterClient {
public:
    using ListCallback = std::function<void(ReleaseListResult const&)>;
    using DownloadCallback = std::function<void(DownloadResult const&)>;

    // Lấy danh sách release. Nhiều lời gọi cùng lúc được gộp thành 1 request,
    // kết quả được cache ngắn hạn trừ khi forceRefresh = true.
    // callback luôn chạy trên main thread.
    static void fetchReleases(ListCallback callback, bool forceRefresh = false);

    // Tải file .geode của release về `<mod>.geode.tmp` (chưa cài).
    static void downloadRelease(ReleaseInfo release, DownloadCallback callback);

    static bool isDownloading();

private:
    static geode::async::TaskHolder<
        geode::utils::web::WebResponse
    > s_listHolder;

    static geode::async::TaskHolder<
        geode::utils::web::WebResponse
    > s_downloadHolder;
};

class VersionManagerSettingV3 : public geode::SettingV3 {
public:
    static geode::Result<std::shared_ptr<geode::SettingV3>> parse(
        std::string const& key,
        std::string const& modID,
        matjson::Value const& json
    );

    bool load(matjson::Value const& json) override;
    bool save(matjson::Value& json) const override;

    bool isDefaultValue() const override;
    void reset() override;

    geode::SettingNodeV3* createNode(float width) override;
};

class VersionManagerSettingNodeV3 : public geode::SettingNodeV3 {
protected:
    CCMenuItemSpriteExtra* m_downloadBtn = nullptr;
    cocos2d::CCLabelBMFont* m_infoLabel = nullptr;

    bool init(
        std::shared_ptr<VersionManagerSettingV3> setting,
        float width
    );

    void updateState(CCNode* invoker) override;

    void onCheckUpdate(CCObject*);
    void onDownloadUpdate(CCObject*);
    void onOpenVersions(CCObject*);

    void onCommit() override;
    void onResetToDefault() override;

public:
    static VersionManagerSettingNodeV3* create(
        std::shared_ptr<VersionManagerSettingV3> setting,
        float width
    );

    // Cập nhật nhãn "Current ... | Update available ..." và nút Download.
    void refreshStatus();

    bool hasUncommittedChanges() const override;
    bool hasNonDefaultValue() const override;

    std::shared_ptr<VersionManagerSettingV3> getSetting() const;
};