#include "version_manager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/MDPopup.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace geode::prelude;

/*
 * Luồng tổng quát
 * ---------------
 *  vào game  -> (MenuLayer) check release mới ở nền
 *            -> có bản mới: popup "Update available - open mod settings"
 *  settings  -> nút Check / Download / bánh răng (danh sách version)
 *  Download  -> tải <mod>.geode.tmp.part -> kiểm tra (zip + mod.json) -> <mod>.geode.tmp
 *            -> popup "Restart required"
 *  Restart   -> <mod>.geode  ->  <mod>.geode.bak ; .tmp -> <mod>.geode ; xoá .bak ; restart game
 *  "Later"   -> lần mở game sau, $on_mod(Loaded) sẽ cài .tmp còn sót rồi nhắc restart
 *  Bánh răng -> popup liệt kê release (theo update_channel); tải bản cũ dùng chung luồng trên.
 */

namespace {

constexpr char const* REPOSITORY = "NeonGD1667/White-Bot";
constexpr auto CACHE_TTL = std::chrono::seconds(120);

// ---------------------------------------------------------------- state
std::vector<UpdaterClient::ListCallback> s_listWaiters;
bool s_listInFlight = false;
std::optional<ReleaseListResult> s_cache;
std::chrono::steady_clock::time_point s_cacheTime;

bool s_downloading = false;
bool s_checking = false;
bool s_checkedOnce = false;
bool s_installedAtLoad = false;
bool s_startupDone = false;

std::optional<ReleaseInfo> s_knownUpdate;
std::vector<std::function<void()>> s_checkWaiters;

enum class CheckMode {
    Startup,     // im lặng, nếu có bản mới thì hiện popup "open mod settings"
    Background,  // im lặng hoàn toàn (chỉ cập nhật s_knownUpdate)
    Manual       // người dùng bấm nút: luôn báo kết quả
};

enum class Compat { Note, Warning, Danger };

// ---------------------------------------------------------------- paths
std::filesystem::path getModPath() {
    return Mod::get()->getPackagePath();
}

std::filesystem::path suffixed(char const* suffix) {
    auto path = getModPath();
    path += suffix;
    return path;
}

bool isPackageInstall() {
    return getModPath().extension() == ".geode";
}

std::string modID() {
    return std::string(Mod::get()->getID());
}

// ---------------------------------------------------------------- misc helpers
bool isDevChannel() {
    return Mod::get()->getSettingValue<std::string>("update_channel") == "Dev-beta";
}

bool channelAccepts(ReleaseInfo const& release) {
    return isDevChannel() || !release.prerelease;
}

std::string describeHttp(int code) {
    if (code <= 0)
        return "Could not reach GitHub. Check your internet connection.";
    if (code == 403 || code == 429)
        return "GitHub rate limit reached. Try again later.";
    if (code == 404)
        return "Not found on GitHub (HTTP 404).";
    return fmt::format("HTTP error {}.", code);
}

std::string truncateUtf8(std::string const& text, size_t maxBytes) {
    if (text.size() <= maxBytes)
        return text;

    size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
        --n;

    return text.substr(0, n) + "\n...";
}

void showAlert(char const* title, std::string const& message) {
    FLAlertLayer::create(title, message, "OK")->show();
}

std::string jsonString(matjson::Value const& obj, char const* key) {
    if (!obj.contains(key))
        return {};
    return obj[key].asString().unwrapOr("");
}

bool jsonBool(matjson::Value const& obj, char const* key) {
    if (!obj.contains(key))
        return false;
    return obj[key].asBool().unwrapOr(false);
}

// Chọn asset .geode: ưu tiên đúng "<mod id>.geode", nếu không có thì lấy file .geode đầu tiên.
std::string pickAssetUrl(matjson::Value const& release) {
    if (!release.contains("assets"))
        return {};

    auto assets = release["assets"].asArray();
    if (!assets)
        return {};

    auto const wanted = modID() + ".geode";
    std::string fallback;

    for (auto const& asset : assets.unwrap()) {
        auto name = jsonString(asset, "name");
        auto url = jsonString(asset, "browser_download_url");

        if (url.empty())
            continue;

        if (name == wanted)
            return url;

        if (
            fallback.empty() &&
            name.size() > 6 &&
            name.compare(name.size() - 6, 6, ".geode") == 0
        ) {
            fallback = url;
        }
    }

    return fallback;
}

ReleaseListResult parseReleaseList(web::WebResponse const& res) {
    ReleaseListResult out;
    out.code = res.code();

    if (!res.ok()) {
        out.error = describeHttp(res.code());
        return out;
    }

    auto json = res.json();
    if (!json) {
        out.error = "Invalid response from GitHub.";
        return out;
    }

    auto array = json.unwrap().asArray();
    if (!array) {
        out.error = "Unexpected response from GitHub.";
        return out;
    }

    for (auto const& item : array.unwrap()) {
        if (!item.isObject() || jsonBool(item, "draft"))
            continue;

        ReleaseInfo release;
        release.tag = jsonString(item, "tag_name");
        release.name = jsonString(item, "name");
        release.body = jsonString(item, "body");
        release.prerelease = jsonBool(item, "prerelease");
        release.publishedAt = jsonString(item, "published_at").substr(0, 10);
        release.downloadUrl = pickAssetUrl(item);

        auto version = VersionInfo::parse(release.tag);
        if (!version)
            continue;  // tag không phải version hợp lệ (vd "nightly")

        release.version = version.unwrap();
        out.releases.push_back(std::move(release));
    }

    std::sort(
        out.releases.begin(),
        out.releases.end(),
        [](ReleaseInfo const& a, ReleaseInfo const& b) {
            return a.version > b.version;
        }
    );

    out.ok = true;
    return out;
}

// ---------------------------------------------------------------- package validation
struct PackageCheck {
    std::string version;
    std::vector<std::string> warnings;
};

// Kiểm tra file tải về đúng là .geode của chính mod này trước khi cho phép cài.
Result<PackageCheck> validatePackage(std::filesystem::path const& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec || size < 1024)
        return Err("The downloaded file is empty or too small.");

    {
        std::ifstream file(path, std::ios::binary);
        char magic[4] = {};
        file.read(magic, 4);

        if (!file || magic[0] != 'P' || magic[1] != 'K' || magic[2] != 3 || magic[3] != 4)
            return Err("The downloaded file is not a valid .geode package.");
    }

    auto meta = ModMetadata::createFromGeodeFile(path);

    if (meta.hasErrors()) {
        auto const& errors = meta.getErrors();
        return Err(
            "Invalid mod package: " +
            (errors.empty() ? std::string("unknown error") : errors.front())
        );
    }

    if (std::string(meta.getID()) != modID()) {
        return Err(fmt::format(
            "The package belongs to a different mod ({}).",
            std::string(meta.getID())
        ));
    }

    PackageCheck out;
    out.version = meta.getVersion().toVString();

    auto loader = Loader::get();

    auto geodeNeeded = meta.getGeodeVersion();
    auto geodeHave = loader->getVersion();
    if (
        geodeNeeded.getMajor() != geodeHave.getMajor() ||
        geodeNeeded > geodeHave
    ) {
        out.warnings.push_back(fmt::format(
            "Requires Geode {} (you have {}).",
            geodeNeeded.toVString(),
            geodeHave.toVString()
        ));
    }

    if (auto gd = meta.getGameVersion()) {
        if (*gd != "*" && *gd != loader->getGameVersion()) {
            out.warnings.push_back(fmt::format(
                "Built for Geometry Dash {} (you have {}).",
                *gd,
                loader->getGameVersion()
            ));
        }
    }

    return Ok(std::move(out));
}

// ---------------------------------------------------------------- install
// <mod>.geode -> <mod>.geode.bak ; <mod>.geode.tmp -> <mod>.geode ; xoá .bak
// Nếu bước 2 lỗi thì khôi phục lại bản cũ.
Result<> installPendingUpdate() {
    if (!isPackageInstall()) {
        return Err(
            "This copy of White Bot is not installed as a .geode package, "
            "so it cannot update itself."
        );
    }

    auto modPath = getModPath();
    auto tmpPath = suffixed(".tmp");
    auto bakPath = suffixed(".bak");

    std::error_code ec;

    if (!std::filesystem::exists(tmpPath, ec))
        return Err("No downloaded update was found.");

    std::filesystem::remove(bakPath, ec);
    ec.clear();

    bool hadOld = std::filesystem::exists(modPath, ec);
    ec.clear();

    if (hadOld) {
        std::filesystem::rename(modPath, bakPath, ec);
        if (ec) {
            return Err(fmt::format(
                "Could not move the old version out of the way: {}",
                ec.message()
            ));
        }
    }

    std::filesystem::rename(tmpPath, modPath, ec);
    if (ec) {
        auto message = ec.message();

        std::error_code restoreEc;
        if (hadOld)
            std::filesystem::rename(bakPath, modPath, restoreEc);

        return Err(fmt::format(
            "Could not install the new version: {}",
            message
        ));
    }

    std::filesystem::remove(bakPath, ec);

    log::info("Installed downloaded update: {}", modPath.string());
    return Ok();
}

// Xoá file rác còn sót từ lần cài/tải dang dở.
void cleanupLeftovers() {
    std::error_code ec;
    std::filesystem::remove(suffixed(".bak"), ec);
    std::filesystem::remove(suffixed(".tmp.part"), ec);
}

// Người dùng bấm "Later" ở lần trước: cài .tmp còn sót ngay khi mod load.
void applyLeftoverUpdate() {
    if (!isPackageInstall())
        return;

    auto tmpPath = suffixed(".tmp");
    std::error_code ec;

    if (!std::filesystem::exists(tmpPath, ec))
        return;

    auto check = validatePackage(tmpPath);
    if (!check) {
        log::warn("Discarding invalid pending update: {}", check.unwrapErr());
        std::filesystem::remove(tmpPath, ec);
        return;
    }

    if (check.unwrap().version == Mod::get()->getVersion().toVString()) {
        std::filesystem::remove(tmpPath, ec);  // đã là bản đang chạy
        return;
    }

    auto result = installPendingUpdate();
    if (!result) {
        log::error("Failed to apply pending update: {}", result.unwrapErr());
        return;
    }

    s_installedAtLoad = true;
}

} // namespace


/*
 * UpdaterClient
 */

geode::async::TaskHolder<
    geode::utils::web::WebResponse
> UpdaterClient::s_listHolder;

geode::async::TaskHolder<
    geode::utils::web::WebResponse
> UpdaterClient::s_downloadHolder;


bool UpdaterClient::isDownloading() {
    return s_downloading;
}


void UpdaterClient::fetchReleases(
    ListCallback callback,
    bool forceRefresh
) {
    if (
        !forceRefresh &&
        s_cache &&
        s_cache->ok &&
        (std::chrono::steady_clock::now() - s_cacheTime) < CACHE_TTL
    ) {
        callback(*s_cache);
        return;
    }

    // Gộp các lời gọi đồng thời: không để request sau huỷ request trước.
    s_listWaiters.push_back(std::move(callback));

    if (s_listInFlight)
        return;

    s_listInFlight = true;

    geode::utils::web::WebRequest req;
    req.userAgent("White-Bot-Updater");
    req.header("Accept", "application/vnd.github+json");
    req.param("per_page", 50);
    req.timeout(std::chrono::seconds(20));

    auto url = fmt::format(
        "https://api.github.com/repos/{}/releases",
        REPOSITORY
    );

    s_listHolder.spawn(
        req.get(url),
        [](geode::utils::web::WebResponse res) {
            auto result = parseReleaseList(res);

            s_listInFlight = false;

            if (result.ok) {
                s_cache = result;
                s_cacheTime = std::chrono::steady_clock::now();
            }

            auto waiters = std::move(s_listWaiters);
            s_listWaiters.clear();

            for (auto& waiter : waiters)
                waiter(result);
        }
    );
}


void UpdaterClient::downloadRelease(
    ReleaseInfo release,
    DownloadCallback callback
) {
    DownloadResult fail;

    if (s_downloading) {
        fail.error = "A download is already in progress.";
        callback(fail);
        return;
    }

    if (release.downloadUrl.empty()) {
        fail.error = "This release has no .geode file attached.";
        callback(fail);
        return;
    }

    if (!isPackageInstall()) {
        fail.error =
            "This copy of White Bot is not installed as a .geode package, "
            "so it cannot update itself.";
        callback(fail);
        return;
    }

    s_downloading = true;

    auto tmpPath = suffixed(".tmp");
    auto partPath = suffixed(".tmp.part");

    geode::utils::web::WebRequest req;
    req.userAgent("White-Bot-Updater");
    req.followRedirects(true);
    req.timeout(std::chrono::seconds(180));

    s_downloadHolder.spawn(
        req.get(release.downloadUrl),
        [callback = std::move(callback), tmpPath, partPath](
            geode::utils::web::WebResponse res
        ) {
            s_downloading = false;

            DownloadResult out;
            out.code = res.code();

            std::error_code ec;

            if (!res.ok()) {
                out.error = describeHttp(res.code());
                callback(out);
                return;
            }

            // Ghi ra .part trước để file dang dở không bao giờ trông như bản cập nhật hợp lệ.
            auto written = res.into(partPath);
            if (!written) {
                out.error = "Failed to save the download: " + written.unwrapErr();
                std::filesystem::remove(partPath, ec);
                callback(out);
                return;
            }

            auto check = validatePackage(partPath);
            if (!check) {
                out.error = check.unwrapErr();
                std::filesystem::remove(partPath, ec);
                callback(out);
                return;
            }

            std::filesystem::remove(tmpPath, ec);
            ec.clear();

            std::filesystem::rename(partPath, tmpPath, ec);
            if (ec) {
                out.error = "Failed to finalize the download: " + ec.message();
                std::filesystem::remove(partPath, ec);
                callback(out);
                return;
            }

            out.ok = true;
            out.version = check.unwrap().version;
            out.warnings = check.unwrap().warnings;
            callback(out);
        }
    );
}


namespace {

// ---------------------------------------------------------------- compatibility
Compat compatOf(ReleaseInfo const& release) {
    auto current = Mod::get()->getVersion();

    if (release.prerelease || release.version.getMajor() != current.getMajor())
        return Compat::Danger;

    if (release.version < current)
        return Compat::Warning;

    return Compat::Note;
}

std::string compatMarkdown(ReleaseInfo const& release) {
    auto current = Mod::get()->getVersion();

    std::string md = fmt::format("## White Bot {}\n", release.version.toVString());

    if (!release.publishedAt.empty())
        md += fmt::format("Published: {}\n\n", release.publishedAt);

    if (release.version == current) {
        md += "- This is the version you are running.\n";
    }
    else if (release.version < current) {
        md += fmt::format(
            "- Older than your current version ({}). Macros or settings saved "
            "by a newer version may not load correctly after downgrading.\n",
            current.toVString()
        );
    }
    else {
        md += fmt::format(
            "- Newer than your current version ({}).\n",
            current.toVString()
        );
    }

    if (release.prerelease)
        md += "- Pre-release build: it may be unstable.\n";

    if (release.version.getMajor() != current.getMajor())
        md += "- Different major version: saved data and settings may be incompatible.\n";

    if (release.downloadUrl.empty())
        md += "- This release has no .geode file attached, so it cannot be installed.\n";

    md += "- Geode / Geometry Dash requirements are verified after the download, "
          "before anything is installed.\n";

    if (!release.body.empty()) {
        md += "\n---\n### Release notes\n";
        md += truncateUtf8(release.body, 1500);
    }

    return md;
}

void showCompat(ReleaseInfo const& release) {
    if (auto popup = MDPopup::create("Compatibility", compatMarkdown(release), "OK"))
        popup->show();
}

// ---------------------------------------------------------------- install flow
void promptRestart(std::string const& tag, std::vector<std::string> const& warnings) {
    std::string content = fmt::format(
        "White Bot <cg>{}</c> has been downloaded.\n"
        "Restart the game to finish installing it.",
        tag
    );

    if (!warnings.empty()) {
        content += "\n\n<cy>Warnings:</c>";
        for (auto const& warning : warnings)
            content += "\n- " + warning;
    }

    createQuickPopup(
        "Restart required",
        content,
        "Later",
        "Restart",
        [](auto, bool restart) {
            if (!restart)
                return;

            auto result = installPendingUpdate();
            if (!result) {
                showAlert("Update failed", result.unwrapErr());
                return;
            }

            utils::game::restart(true);
        }
    );
}

void startInstall(ReleaseInfo const& release) {
    if (UpdaterClient::isDownloading()) {
        Notification::create(
            "A download is already in progress.",
            NotificationIcon::Warning
        )->show();
        return;
    }

    Ref<Notification> notification = Notification::create(
        fmt::format("Downloading {}...", release.tag),
        NotificationIcon::Loading,
        0.f
    );
    notification->show();

    UpdaterClient::downloadRelease(
        release,
        [notification, tag = release.tag](DownloadResult const& result) {
            notification->hide();

            if (!result.ok) {
                showAlert("Download failed", result.error);
                return;
            }

            promptRestart(tag, result.warnings);
        }
    );
}

void confirmInstall(ReleaseInfo const& release) {
    auto current = Mod::get()->getVersion();
    bool downgrade = release.version < current;

    std::string content = fmt::format(
        "{} <cy>{}</c> (you have {})?\n"
        "The game must restart to finish.",
        downgrade ? "Downgrade to" : "Update to",
        release.version.toVString(),
        current.toVString()
    );

    if (downgrade)
        content += "\n<cr>Macros or settings saved by a newer version may not load.</c>";

    if (release.prerelease)
        content += "\n<cy>This is a pre-release build.</c>";

    createQuickPopup(
        downgrade ? "Downgrade" : "Update",
        content,
        "Cancel",
        "Download",
        [release](auto, bool confirmed) {
            if (confirmed)
                startInstall(release);
        }
    );
}

// ---------------------------------------------------------------- update check
void promptUpdate(ReleaseInfo const& latest, CheckMode mode) {
    if (mode == CheckMode::Manual) {
        confirmInstall(latest);
        return;
    }

    if (mode == CheckMode::Startup) {
        createQuickPopup(
            "Update available",
            fmt::format(
                "White Bot <cg>{}</c> is available (you have {}).\n"
                "Open the mod settings to update.",
                latest.version.toVString(),
                Mod::get()->getVersion().toVString()
            ),
            "Later",
            "Open Settings",
            [](auto, bool open) {
                if (open)
                    openSettingsPopup(Mod::get());
            }
        );
    }
}

void checkForUpdates(CheckMode mode, std::function<void()> onDone = nullptr) {
    if (onDone)
        s_checkWaiters.push_back(std::move(onDone));

    if (s_checking) {
        if (mode == CheckMode::Manual) {
            Notification::create(
                "Already checking for updates...",
                NotificationIcon::Loading
            )->show();
        }
        return;
    }

    s_checking = true;

    UpdaterClient::fetchReleases(
        [mode](ReleaseListResult const& result) {
            s_checking = false;
            s_checkedOnce = true;

            std::optional<ReleaseInfo> latest;
            if (result.ok) {
                // releases đã sắp xếp mới -> cũ
                for (auto const& release : result.releases) {
                    if (channelAccepts(release)) {
                        latest = release;
                        break;
                    }
                }
            }

            auto current = Mod::get()->getVersion();

            if (latest && latest->version > current)
                s_knownUpdate = latest;
            else
                s_knownUpdate = std::nullopt;

            if (!result.ok) {
                if (mode == CheckMode::Manual)
                    showAlert("Update", "Failed to check for updates.\n" + result.error);
            }
            else if (!latest) {
                if (mode == CheckMode::Manual)
                    showAlert("Update", "No releases were found for this update channel.");
            }
            else if (s_knownUpdate) {
                promptUpdate(*s_knownUpdate, mode);
            }
            else if (mode == CheckMode::Manual) {
                if (latest->version < current) {
                    showAlert(
                        "Update",
                        fmt::format(
                            "You are running {}, which is newer than the latest release ({}).",
                            current.toVString(),
                            latest->version.toVString()
                        )
                    );
                }
                else {
                    showAlert("Update", "White Bot is already up to date.");
                }
            }

            auto waiters = std::move(s_checkWaiters);
            s_checkWaiters.clear();

            for (auto& waiter : waiters)
                waiter();
        },
        mode == CheckMode::Manual
    );
}

void onMenuReady() {
    if (s_startupDone)
        return;

    s_startupDone = true;

    if (s_installedAtLoad) {
        createQuickPopup(
            "Update installed",
            "A downloaded update of White Bot was installed.\n"
            "Restart the game to start using it.",
            "Later",
            "Restart",
            [](auto, bool restart) {
                if (restart)
                    utils::game::restart(true);
            }
        );
        return;
    }

    // Có thể tắt bằng setting bool "check_updates_on_startup" trong mod.json (không bắt buộc).
    bool enabled =
        !Mod::get()->hasSetting("check_updates_on_startup") ||
        Mod::get()->getSettingValue<bool>("check_updates_on_startup");

    if (enabled)
        checkForUpdates(CheckMode::Startup);
}

// ---------------------------------------------------------------- UI helpers
void fitHeight(CCNode* node, float height) {
    if (node && node->getContentHeight() > 0.f)
        node->setScale(height / node->getContentHeight());
}

template <class F>
CCMenuItemSpriteExtra* makeIconButton(char const* frame, float height, F&& callback) {
    auto sprite = CCSprite::createWithSpriteFrameName(frame);
    if (!sprite)
        return nullptr;

    fitHeight(sprite, height);
    return CCMenuItemExt::createSpriteExtra(sprite, std::forward<F>(callback));
}

// ---------------------------------------------------------------- versions popup
class VersionListPopup : public geode::Popup {
protected:
    static constexpr float LIST_W = 330.f;
    static constexpr float LIST_H = 160.f;
    static constexpr float ROW_H = 34.f;

    ScrollLayer* m_scroll = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCMenuItemSpriteExtra* m_retryBtn = nullptr;

    bool init() {
        if (!Popup::init(380.f, 290.f))
            return false;

        this->setTitle("WHITE BOT VERSIONS");

        auto header = CCLabelBMFont::create("VERSION", "bigFont.fnt");
        header->setScale(0.4f);
        header->setOpacity(160);
        header->setAnchorPoint({0.f, 0.5f});
        header->setPosition({28.f, 233.f});
        m_mainLayer->addChild(header);

        auto channel = CCLabelBMFont::create(
            fmt::format("Channel: {}", isDevChannel() ? "Dev-beta" : "Main").c_str(),
            "chatFont.fnt"
        );
        channel->setScale(0.5f);
        channel->setOpacity(160);
        channel->setAnchorPoint({1.f, 0.5f});
        channel->setPosition({352.f, 233.f});
        m_mainLayer->addChild(channel);

        auto listBg = NineSlice::create("square02b_001.png");
        listBg->setColor({0, 0, 0});
        listBg->setOpacity(90);
        listBg->setContentSize({LIST_W + 10.f, LIST_H + 10.f});
        listBg->setPosition({190.f, 135.f});
        m_mainLayer->addChild(listBg);

        m_scroll = ScrollLayer::create({LIST_W, LIST_H});
        m_scroll->setPosition({25.f, 55.f});
        m_scroll->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(4.f));
        m_mainLayer->addChild(m_scroll);

        m_status = CCLabelBMFont::create("Loading versions...", "bigFont.fnt");
        m_status->setScale(0.45f);
        m_status->setPosition({190.f, 135.f});
        m_mainLayer->addChild(m_status, 5);

        auto retrySpr = ButtonSprite::create("Retry");
        retrySpr->setScale(0.7f);
        m_retryBtn = CCMenuItemExt::createSpriteExtra(
            retrySpr,
            [this](CCMenuItemSpriteExtra*) { this->load(true); }
        );
        m_retryBtn->setVisible(false);
        m_buttonMenu->addChildAtPosition(m_retryBtn, Anchor::Center, ccp(0.f, -35.f));

        this->buildLegend();
        this->load(false);

        return true;
    }

    void buildLegend() {
        struct Entry {
            char const* frame;
            char const* text;
            float x;
        };

        Entry const entries[] = {
            {"geode.loader/info-alert.png", "Compatibility info", 28.f},
            {"GJ_downloadBtn_001.png", "Install version", 160.f},
            {"GJ_completesIcon_001.png", "Installed", 275.f},
        };

        for (auto const& entry : entries) {
            auto icon = CCSprite::createWithSpriteFrameName(entry.frame);
            if (icon) {
                fitHeight(icon, 16.f);
                icon->setPosition({entry.x, 30.f});
                m_mainLayer->addChild(icon);
            }

            auto label = CCLabelBMFont::create(entry.text, "chatFont.fnt");
            label->setScale(0.5f);
            label->setOpacity(190);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({entry.x + 14.f, 30.f});
            m_mainLayer->addChild(label);
        }
    }

    void load(bool force) {
        m_status->setString("Loading versions...");
        m_status->setScale(0.45f);
        m_status->setVisible(true);
        m_retryBtn->setVisible(false);

        UpdaterClient::fetchReleases(
            [ref = WeakRef<VersionListPopup>(this)](ReleaseListResult const& result) {
                if (auto self = ref.lock())
                    self->populate(result);
            },
            force
        );
    }

    void populate(ReleaseListResult const& result) {
        m_scroll->m_contentLayer->removeAllChildren();

        if (!result.ok) {
            m_status->setString(("Failed to load versions:\n" + result.error).c_str());
            m_status->limitLabelWidth(LIST_W - 20.f, 0.4f, 0.2f);
            m_status->setVisible(true);
            m_retryBtn->setVisible(true);
            return;
        }

        size_t shown = 0;
        for (auto const& release : result.releases) {
            if (!channelAccepts(release))
                continue;

            m_scroll->m_contentLayer->addChild(this->makeRow(release));
            ++shown;
        }

        if (shown == 0) {
            m_status->setString("No versions found for this channel.");
            m_status->limitLabelWidth(LIST_W - 20.f, 0.45f, 0.2f);
            m_status->setVisible(true);
            return;
        }

        m_status->setVisible(false);
        m_scroll->m_contentLayer->updateLayout();
        m_scroll->scrollToTop();
    }

    CCNode* makeRow(ReleaseInfo const& release) {
        auto current = Mod::get()->getVersion();
        bool isCurrent = release.version == current;

        auto row = CCNode::create();
        row->setContentSize({LIST_W, ROW_H});

        auto bg = NineSlice::create("square02b_001.png");
        bg->setColor(isCurrent ? ccColor3B{40, 100, 40} : ccColor3B{0, 0, 0});
        bg->setOpacity(isCurrent ? 120 : 70);
        bg->setContentSize({LIST_W, ROW_H});
        bg->setPosition({LIST_W / 2.f, ROW_H / 2.f});
        row->addChild(bg);

        auto name = CCLabelBMFont::create(release.version.toVString().c_str(), "bigFont.fnt");
        name->setScale(0.5f);
        name->setAnchorPoint({0.f, 0.5f});
        name->setPosition({12.f, ROW_H / 2.f});
        row->addChild(name);

        auto menu = CCMenu::create();
        menu->setContentSize({LIST_W, ROW_H});
        menu->setPosition({0.f, 0.f});
        row->addChild(menu);

        // [!] compatibility
        if (auto alertBtn = makeIconButton(
                "geode.loader/info-alert.png",
                22.f,
                [release](CCMenuItemSpriteExtra*) { showCompat(release); }
            )) {
            auto level = compatOf(release);
            if (auto image = typeinfo_cast<CCRGBAProtocol*>(alertBtn->getNormalImage())) {
                if (level == Compat::Danger)
                    image->setColor({255, 90, 90});
                else if (level == Compat::Note)
                    image->setColor({190, 190, 190});
            }

            alertBtn->setPosition({112.f, ROW_H / 2.f});
            menu->addChild(alertBtn);
        }

        if (release.prerelease) {
            auto beta = CCLabelBMFont::create("BETA", "bigFont.fnt");
            beta->setScale(0.3f);
            beta->setColor({255, 170, 60});
            beta->setAnchorPoint({0.f, 0.5f});
            beta->setPosition({130.f, ROW_H / 2.f});
            row->addChild(beta);
        }

        // Bên phải: CURRENT / nút tải / không có file
        if (isCurrent) {
            auto label = CCLabelBMFont::create("CURRENT", "bigFont.fnt");
            label->setScale(0.38f);
            label->setColor({140, 255, 140});
            label->setAnchorPoint({1.f, 0.5f});
            label->setPosition({LIST_W - 12.f, ROW_H / 2.f});
            row->addChild(label);

            if (auto check = CCSprite::createWithSpriteFrameName("GJ_completesIcon_001.png")) {
                fitHeight(check, 20.f);
                check->setPosition({
                    LIST_W - 12.f - label->getScaledContentSize().width - 14.f,
                    ROW_H / 2.f
                });
                row->addChild(check);
            }
        }
        else if (!release.downloadUrl.empty()) {
            if (auto downloadBtn = makeIconButton(
                    "GJ_downloadBtn_001.png",
                    26.f,
                    [release](CCMenuItemSpriteExtra*) { confirmInstall(release); }
                )) {
                downloadBtn->setPosition({LIST_W - 28.f, ROW_H / 2.f});
                menu->addChild(downloadBtn);
            }
        }
        else {
            auto label = CCLabelBMFont::create("NO FILE", "bigFont.fnt");
            label->setScale(0.3f);
            label->setOpacity(130);
            label->setAnchorPoint({1.f, 0.5f});
            label->setPosition({LIST_W - 12.f, ROW_H / 2.f});
            row->addChild(label);
        }

        return row;
    }

public:
    static VersionListPopup* create() {
        auto ret = new VersionListPopup();

        if (ret->init()) {
            ret->autorelease();
            return ret;
        }

        delete ret;
        return nullptr;
    }

    static void open() {
        if (auto popup = VersionListPopup::create())
            popup->show();
    }
};

} // namespace


/*
 * Version Manager Setting
 */

Result<std::shared_ptr<SettingV3>>
VersionManagerSettingV3::parse(
    std::string const& key,
    std::string const& modID,
    matjson::Value const& json
) {
    auto res =
        std::make_shared<VersionManagerSettingV3>();

    auto root = checkJson(
        json,
        "VersionManagerSettingV3"
    );

    res->init(
        key,
        modID,
        root
    );

    res->parseNameAndDescription(root);
    res->parseEnableIf(root);

    root.checkUnknownKeys();

    return root.ok(
        std::static_pointer_cast<SettingV3>(res)
    );
}


bool VersionManagerSettingV3::load(
    matjson::Value const&
) {
    return true;
}


bool VersionManagerSettingV3::save(
    matjson::Value&
) const {
    return true;
}


bool VersionManagerSettingV3::isDefaultValue() const {
    return true;
}


void VersionManagerSettingV3::reset() {}


SettingNodeV3*
VersionManagerSettingV3::createNode(
    float width
) {
    return VersionManagerSettingNodeV3::create(
        std::static_pointer_cast<
            VersionManagerSettingV3
        >(
            shared_from_this()
        ),
        width
    );
}


/*
 * Version Manager Node
 *
 *  [ Current v3.0.1  |  update info ]        [update] [download] [gear]
 *   - update   : kiểm tra bản mới (luôn báo kết quả)
 *   - download : chỉ hiện khi đã biết có bản mới, tải bản đó
 *   - gear     : mở danh sách version để downgrade / cài bản bất kỳ
 */

bool VersionManagerSettingNodeV3::init(
    std::shared_ptr<VersionManagerSettingV3> setting,
    float width
) {
    if (!SettingNodeV3::init(setting, width))
        return false;

    m_infoLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_infoLabel->setAnchorPoint({0.f, 0.5f});
    m_infoLabel->setPosition({12.f, this->getContentHeight() / 2.f});
    this->addChild(m_infoLabel);

    auto menu = this->getButtonMenu();

    auto checkButton = makeIconButton(
        "GJ_updateBtn_001.png",
        26.f,
        [this](CCMenuItemSpriteExtra*) { this->onCheckUpdate(nullptr); }
    );

    m_downloadBtn = makeIconButton(
        "GJ_downloadBtn_001.png",
        26.f,
        [this](CCMenuItemSpriteExtra*) { this->onDownloadUpdate(nullptr); }
    );

    auto gearButton = makeIconButton(
        "GJ_optionsBtn_001.png",
        26.f,
        [this](CCMenuItemSpriteExtra*) { this->onOpenVersions(nullptr); }
    );

    if (!checkButton || !m_downloadBtn || !gearButton)
        return false;

    menu->addChild(checkButton);
    menu->addChild(m_downloadBtn);
    menu->addChild(gearButton);

    this->refreshStatus();
    this->updateState(nullptr);

    return true;
}


void VersionManagerSettingNodeV3::refreshStatus() {
    if (!m_infoLabel)
        return;

    auto current = Mod::get()->getVersion().toVString();

    if (s_knownUpdate) {
        m_infoLabel->setString(
            fmt::format(
                "{}  ->  {} available",
                current,
                s_knownUpdate->version.toVString()
            ).c_str()
        );
        m_infoLabel->setColor({120, 255, 120});
    }
    else {
        m_infoLabel->setString(fmt::format("White Bot {}", current).c_str());
        m_infoLabel->setColor({255, 255, 255});
    }

    m_infoLabel->limitLabelWidth(this->getContentWidth() * 0.5f, 0.6f, 0.2f);

    if (m_downloadBtn)
        m_downloadBtn->setVisible(s_knownUpdate.has_value());

    if (auto menu = this->getButtonMenu())
        menu->updateLayout();
}


void VersionManagerSettingNodeV3::updateState(
    CCNode* invoker
) {
    SettingNodeV3::updateState(invoker);
}


void VersionManagerSettingNodeV3::onCheckUpdate(
    CCObject*
) {
    checkForUpdates(
        CheckMode::Manual,
        [self = Ref<VersionManagerSettingNodeV3>(this)] {
            self->refreshStatus();
        }
    );
}


void VersionManagerSettingNodeV3::onDownloadUpdate(
    CCObject*
) {
    if (s_knownUpdate) {
        confirmInstall(*s_knownUpdate);
        return;
    }

    this->onCheckUpdate(nullptr);
}


void VersionManagerSettingNodeV3::onOpenVersions(
    CCObject*
) {
    VersionListPopup::open();
}


void VersionManagerSettingNodeV3::onCommit() {}


void VersionManagerSettingNodeV3::onResetToDefault() {}


VersionManagerSettingNodeV3*
VersionManagerSettingNodeV3::create(
    std::shared_ptr<VersionManagerSettingV3> setting,
    float width
) {
    auto ret =
        new VersionManagerSettingNodeV3();

    if (
        ret &&
        ret->init(setting, width)
    ) {
        ret->autorelease();

        // Chưa từng check trong phiên này: check nền để hiện đúng trạng thái.
        if (!s_checkedOnce) {
            checkForUpdates(
                CheckMode::Background,
                [self = Ref<VersionManagerSettingNodeV3>(ret)] {
                    self->refreshStatus();
                }
            );
        }

        return ret;
    }

    CC_SAFE_DELETE(ret);

    return nullptr;
}


bool VersionManagerSettingNodeV3::hasUncommittedChanges()
    const {
    return false;
}


bool VersionManagerSettingNodeV3::hasNonDefaultValue()
    const {
    return false;
}


std::shared_ptr<VersionManagerSettingV3>
VersionManagerSettingNodeV3::getSetting() const {
    return std::static_pointer_cast<
        VersionManagerSettingV3
    >(
        SettingNodeV3::getSetting()
    );
}


/*
 * Startup check: chạy 1 lần khi MenuLayer xuất hiện.
 */
class $modify(WhiteBotUpdaterMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init())
            return false;

        onMenuReady();

        return true;
    }
};


/*
 * Register custom setting
 */

$on_mod(Loaded) {
    cleanupLeftovers();

    /*
     * Cài bản cập nhật còn sót lại (người dùng đã bấm "Later").
     */
    applyLeftoverUpdate();

    /*
     * IMPORTANT:
     * "custom:" is only used in mod.json.
     */
    auto result =
        Mod::get()->registerCustomSettingType(
            "version-manager",
            &VersionManagerSettingV3::parse
        );

    if (!result) {
        log::error(
            "Failed to register custom setting 'version-manager': {}",
            result.unwrapErr()
        );
    }
}