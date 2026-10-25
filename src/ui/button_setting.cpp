#include "../includes.hpp"
#include "record_layer.hpp"
#include "../download/version_manager.hpp"  // openVersionManager()

#include <Geode/loader/SettingV3.hpp>
#include <Geode/loader/Mod.hpp>

class MyButtonSettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key,
        std::string const& modID,
        matjson::Value const& json
    ) {
        auto res = std::make_shared<MyButtonSettingV3>();
        auto root = checkJson(json, "MyButtonSettingV3");

        res->init(key, modID, root);
        res->parseNameAndDescription(root);
        res->parseEnableIf(root);

        root.checkUnknownKeys();
        return root.ok(std::static_pointer_cast<SettingV3>(res));
    }

    bool load(matjson::Value const& json) override {
        return true;
    }

    bool save(matjson::Value& json) const override {
        return true;
    }

    bool isDefaultValue() const override {
        return true;
    }

    void reset() override {}

    SettingNodeV3* createNode(float width) override;
};

class MyButtonSettingNodeV3 : public SettingNodeV3 {
protected:
    bool init(
        std::shared_ptr<MyButtonSettingV3> setting,
        float width
    ) {
        if (!SettingNodeV3::init(setting, width))
            return false;

        // Cả "button-setting" (Open Menu) và "downgrade-button" dùng chung icon.
        auto sprite = CCSprite::createWithSpriteFrameName(
            "GJ_playBtn2_001.png"
        );

        if (!sprite)
            return false;

        sprite->setScale(0.325f);

        auto btn = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            menu_selector(MyButtonSettingNodeV3::onButton)
        );

        // Menu riêng phủ kín dòng, nút đặt tay sát mép phải và giữa dòng theo chiều dọc.
        // Không dùng getButtonMenu() + Anchor::Center vì nó đặt nút ở GIỮA nửa phải của dòng
        // (lệch so với các setting khác như Update Channel).
        auto menu = CCMenu::create();
        menu->setAnchorPoint({0.f, 0.f});
        menu->setPosition({0.f, 0.f});
        menu->setContentSize(this->getContentSize());

        btn->setPosition({
            this->getContentWidth() - 38.f,
            this->getContentHeight() / 2.f
        });

        menu->addChild(btn);
        this->addChild(menu);

        updateState(nullptr);

        return true;
    }

    void updateState(CCNode* invoker) override {
        SettingNodeV3::updateState(invoker);
    }

    void onButton(CCObject*) {
        auto key = getSetting()->getKey();

        // Downgrade: mở Version Manager đè lên trang settings (KHÔNG đóng settings).
        if (key == "downgrade-button") {
            openVersionManager();
            return;
        }

        if (key == "button-setting") {
            // Đóng popup settings đang mở rồi mở menu bot.
            if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
                if (auto children = scene->getChildren()) {
                    if (auto layer = typeinfo_cast<FLAlertLayer*>(children->lastObject()))
                        layer->keyBackClicked();
                }
            }

            RecordLayer::openMenu();
        }
    }

    void onCommit() override {}

    void onResetToDefault() override {}

public:
    static MyButtonSettingNodeV3* create(
        std::shared_ptr<MyButtonSettingV3> setting,
        float width
    ) {
        auto ret = new MyButtonSettingNodeV3();

        if (ret && ret->init(setting, width)) {
            ret->autorelease();
            return ret;
        }

        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool hasUncommittedChanges() const override {
        return false;
    }

    bool hasNonDefaultValue() const override {
        return false;
    }

    std::shared_ptr<MyButtonSettingV3> getSetting() const {
        return std::static_pointer_cast<MyButtonSettingV3>(
            SettingNodeV3::getSetting()
        );
    }
};

SettingNodeV3* MyButtonSettingV3::createNode(float width) {
    return MyButtonSettingNodeV3::create(
        std::static_pointer_cast<MyButtonSettingV3>(
            shared_from_this()
        ),
        width
    );
}

$execute {
    (void)Mod::get()->registerCustomSettingType(
        "button",
        &MyButtonSettingV3::parse
    );
}
