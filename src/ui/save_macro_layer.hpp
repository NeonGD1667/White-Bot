#pragma once

#include "../includes.hpp"
#include "../utils/utils.hpp"

class SaveMacroLayer : public geode::Popup {
    TextInput* authorInput = nullptr;
    TextInput* descInput = nullptr;
    TextInput* nameInput = nullptr;

    enum class Format {
        Gdr,
        GdrJson,
        Gdr2,
        Slc2,
        Slc3
    };

    Format currentFormat = Format::Gdr;

    CCMenuItemSpriteExtra* formatBtn = nullptr;

private:
    std::string formatLabel() const {
        switch (currentFormat) {
            case Format::Gdr:
                return "Format: GDR";

            case Format::GdrJson:
                return "Format: JSON";

            case Format::Gdr2:
                return "Format: GDR2";

            case Format::Slc2:
                return "Format: SLC2";

            case Format::Slc3:
                return "Format: SLC3";
        }

        return "Format: GDR";
    }

    void updateFormatButton() {
        if (!formatBtn)
            return;

        if (auto* children = formatBtn->getChildren()) {
            if (children->count() > 0) {
                if (auto* bs = typeinfo_cast<ButtonSprite*>(
                        children->objectAtIndex(0)
                    )) {
                    bs->setString(formatLabel().c_str());
                }
            }
        }
    }

    bool setup() {
        setTitle("Save Macro");

        cocos2d::CCPoint offset =
            (CCDirector::sharedDirector()->getWinSize() -
             m_mainLayer->getContentSize()) / 2;

        m_mainLayer->setPosition(
            m_mainLayer->getPosition() - offset
        );

        m_closeBtn->setPosition(
            m_closeBtn->getPosition() + offset
        );

        m_bgSprite->setPosition(
            m_bgSprite->getPosition() + offset
        );

        m_title->setPosition(
            m_title->getPosition() + offset
        );

        CCMenu* menu = CCMenu::create();
        m_mainLayer->addChild(menu);

        // Author
        authorInput = TextInput::create(
            104,
            "Author",
            "chatFont.fnt"
        );

        authorInput->setPosition({61, 42});

        authorInput->setString(
            GJAccountManager::sharedState()->m_username.c_str()
        );

        menu->addChild(authorInput);

        CCLabelBMFont* optionalLabel =
            CCLabelBMFont::create(
                "(optional)",
                "chatFont.fnt"
            );

        optionalLabel->setPosition({61, 20});
        optionalLabel->setOpacity(73);
        optionalLabel->setScale(0.575f);

        menu->addChild(optionalLabel);

        // Name
        nameInput = TextInput::create(
            104,
            "Name",
            "chatFont.fnt"
        );

        nameInput->setPosition({-61, 42});

        nameInput->setString(
            Global::get().macro.levelInfo.name
        );

        menu->addChild(nameInput);

        // Description
        descInput = TextInput::create(
            226,
            "Description (optional)",
            "chatFont.fnt"
        );

        descInput->setPositionY(-8);

        menu->addChild(descInput);

        // Save button
        ButtonSprite* saveSpr =
            ButtonSprite::create("Save");

        saveSpr->setScale(0.725f);

        CCMenuItemSpriteExtra* saveBtn =
            CCMenuItemSpriteExtra::create(
                saveSpr,
                this,
                menu_selector(SaveMacroLayer::onSave)
            );

        saveBtn->setPositionY(-56);

        menu->addChild(saveBtn);

        // Format button
        ButtonSprite* formatSpr =
            ButtonSprite::create(
                formatLabel().c_str(),
                90,
                true,
                "bigFont.fnt",
                "GJ_button_04.png",
                0,
                0.6f
            );

        formatSpr->setScale(0.6f);

        formatBtn =
            CCMenuItemSpriteExtra::create(
                formatSpr,
                this,
                menu_selector(SaveMacroLayer::onCycleFormat)
            );

        formatBtn->setPosition({
            -83,
            -78
        });

        menu->addChild(formatBtn);

        return true;
    }

public:
    STATIC_CREATE(SaveMacroLayer, 285, 194)

    static void open() {
        if (Global::get().macro.inputs.empty()) {
            return FLAlertLayer::create(
                "Save Macro",
                "You can't save an <cl>empty</c> macro.",
                "Ok"
            )->show();
        }

        asp::fs::Path path =
            Mod::get()->getSettingValue<
                asp::fs::Path
            >("macros_folder");

        if (!std::filesystem::exists(path)) {
            if (!utils::file::createDirectoryAll(path).isOk()) {
                return FLAlertLayer::create(
                    "Error",
                    (
                        "There was an error getting the folder \"" +
                        path.string() +
                        "\". ID: 10"
                    ).c_str(),
                    "Ok"
                )->show();
            }
        }

        SaveMacroLayer* layerReal = create();

        layerReal->m_noElasticity = true;
        layerReal->show();
    }

    void onCycleFormat(CCObject*) {
        switch (currentFormat) {
            case Format::Gdr:
                currentFormat = Format::GdrJson;
                break;

            case Format::GdrJson:
                currentFormat = Format::Gdr2;
                break;

            case Format::Gdr2:
                currentFormat = Format::Slc2;
                break;

            case Format::Slc2:
                currentFormat = Format::Slc3;
                break;

            case Format::Slc3:
                currentFormat = Format::Gdr;
                break;
        }

        updateFormatButton();
    }

    void onSave(CCObject*) {
        std::string macroName =
            nameInput->getString();

        if (macroName.empty()) {
            return FLAlertLayer::create(
                "Save Macro",
                "Give a <cl>name</c> to the macro.",
                "Ok"
            )->show();
        }

        asp::fs::Path path =
            Mod::get()->getSettingValue<
                asp::fs::Path
            >("macros_folder") /
            macroName;

        std::string author =
            authorInput->getString();

        std::string desc =
            descInput->getString();

        int result = 0;

        switch (currentFormat) {
            case Format::Gdr:
                result = Macro::save(
                    author,
                    desc,
                    path.string(),
                    false
                );
                break;

            case Format::GdrJson:
                result = Macro::save(
                    author,
                    desc,
                    path.string(),
                    true
                );
                break;

            case Format::Gdr2:
                result = Macro::saveGDR2(
                    author,
                    desc,
                    path.string()
                );
                break;

            case Format::Slc2:
                result = Macro::saveSLC2(
                    author,
                    desc,
                    path.string()
                );
                break;

            case Format::Slc3:
                result = Macro::saveSLC3(
                    author,
                    desc,
                    path.string()
                );
                break;
        }

        if (result != 0) {
            return FLAlertLayer::create(
                "Error",
                (
                    "There was an error saving the macro. ID: " +
                    std::to_string(result)
                ).c_str(),
                "Ok"
            )->show();
        }

        this->keyBackClicked();

        Notification::create(
            "Macro Saved",
            NotificationIcon::Success
        )->show();
    }
};