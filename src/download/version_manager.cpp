#include "version_manager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/web.hpp>

#include <fstream>

using namespace geode::prelude;

namespace {

constexpr char const* REPOSITORY =
    "NeonGD1667/White-Bot";

std::string getCurrentVersion() {
    return "v" + fmt::to_string(
        Mod::get()->getVersion()
    );
}

std::filesystem::path getModPath() {
    return Mod::get()->getSaveDir().parent_path()
        / (Mod::get()->getID() + ".geode");
}

std::filesystem::path getTempPath() {
    auto path = getModPath();
    path += ".tmp";
    return path;
}

void showComingSoon(char const* title) {
    FLAlertLayer::create(
        title,
        "Version manager is not implemented yet.",
        "OK"
    )->show();
}

void showAlert(
    char const* title,
    char const* message
) {
    FLAlertLayer::create(
        title,
        message,
        "OK"
    )->show();
}

bool responseOK(int code) {
    return code >= 200 && code < 300;
}

} // namespace


/*
 * UpdaterClient
 */

geode::async::TaskHolder<
    geode::utils::web::WebResponse
> UpdaterClient::s_getHolder;

void UpdaterClient::getLatestRelease(
    ReleaseCallback callback
) {
    geode::utils::web::WebRequest req;

    req.userAgent("geode");

    auto url =
        "https://api.github.com/repos/" +
        std::string(REPOSITORY) +
        "/releases/latest";

    s_getHolder.spawn(
        req.get(url),
        [callback](geode::utils::web::WebResponse res) {
            GithubReleaseResponseDto dto;

            if (res.ok()) {
                auto json = res.json();

                if (json) {
                    auto const& root =
                        json.unwrap();

                    if (root.contains("tag_name")) {
                        dto.tagName =
                            root["tag_name"]
                                .asString()
                                .unwrapOr("");

                        dto.valid =
                            !dto.tagName.empty();
                    }
                }
            }

            callback(
                dto,
                res.code()
            );
        }
    );
}

void UpdaterClient::getLatestDownload(
    DownloadCallback callback
) {
    geode::utils::web::WebRequest req;

    req.userAgent("geode");

    auto url =
        "https://github.com/" +
        std::string(REPOSITORY) +
        "/releases/latest/download/" +
        Mod::get()->getID() +
        ".geode";

    auto tempPath =
        getTempPath();

    s_getHolder.spawn(
        req.get(url),
        [callback, tempPath](
            geode::utils::web::WebResponse res
        ) {
            EmptyResponseDto dto;

            if (res.ok()) {
                auto result =
                    res.into(tempPath);

                if (!result) {
                    log::error(
                        "Failed to save update: {}",
                        result.unwrapErr()
                    );
                }
            }

            callback(
                dto,
                res.code()
            );
        }
    );
}


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


/*
 * Version Manager Node
 */

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

bool VersionManagerSettingNodeV3::init(
    std::shared_ptr<VersionManagerSettingV3> setting,
    float width
) {
    if (!SettingNodeV3::init(setting, width))
        return false;

    auto menu = getButtonMenu();

    /*
     * Check for updates
     */

    auto checkSprite =
        CCSprite::createWithSpriteFrameName(
            "GJ_updateBtn_001.png"
        );

    if (!checkSprite)
        return false;

    checkSprite->setScale(0.45f);

    auto checkButton =
        CCMenuItemSpriteExtra::create(
            checkSprite,
            this,
            menu_selector(
                VersionManagerSettingNodeV3::onCheckUpdate
            )
        );

    menu->addChild(checkButton);

    /*
     * Downgrade
     */

    auto downgradeSprite =
        CCSprite::createWithSpriteFrameName(
            "GJ_downloadBtn_001.png"
        );

    if (!downgradeSprite)
        return false;

    downgradeSprite->setScale(0.45f);

    auto downgradeButton =
        CCMenuItemSpriteExtra::create(
            downgradeSprite,
            this,
            menu_selector(
                VersionManagerSettingNodeV3::onDowngrade
            )
        );

    menu->addChild(downgradeButton);

    menu->updateLayout();

    updateState(nullptr);

    return true;
}

void VersionManagerSettingNodeV3::updateState(
    CCNode* invoker
) {
    SettingNodeV3::updateState(invoker);
}

void VersionManagerSettingNodeV3::onCheckUpdate(
    CCObject*
) {
    UpdaterClient::getLatestRelease(
        [this](
            GithubReleaseResponseDto const& release,
            int responseCode
        ) {
            if (
                !responseOK(responseCode) ||
                !release.valid
            ) {
                showAlert(
                    "Update",
                    "Failed to check for updates."
                );
                return;
            }

            auto currentVersion =
                getCurrentVersion();

            if (
                release.tagName ==
                currentVersion
            ) {
                showAlert(
                    "Update",
                    "White Bot is already up to date."
                );
                return;
            }

            log::info(
                "Update available: {} -> {}",
                currentVersion,
                release.tagName
            );

            /*
             * Download update to .geode.tmp
             */

            UpdaterClient::getLatestDownload(
                [this](
                    EmptyResponseDto const&,
                    int downloadResponseCode
                ) {
                    if (
                        !responseOK(
                            downloadResponseCode
                        )
                    ) {
                        showAlert(
                            "Update",
                            "Failed to download update."
                        );
                        return;
                    }

                    showAlert(
                        "Update",
                        "Update downloaded. Restart to apply."
                    );
                }
            );
        }
    );
}

void VersionManagerSettingNodeV3::onDowngrade(
    CCObject*
) {
    showComingSoon("Downgrade");
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
 * Register custom setting
 */

$execute {
    (void)Mod::get()->registerCustomSettingType(
        "custom:version-manager",
        &VersionManagerSettingV3::parse
    );
}
