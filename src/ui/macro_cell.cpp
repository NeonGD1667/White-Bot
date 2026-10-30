#include "load_macro_layer.hpp"
#include "macro_editor.hpp"

#ifdef _MSC_VER
#pragma optimize("", off)
#endif

MacroCell* MacroCell::create(std::filesystem::path path, std::string name, std::time_t date, geode::Popup* menuLayer, geode::Popup* mergeLayer, CCLayer* loadLayer) {
    MacroCell* ret = new MacroCell();
    if (!ret->init(path, name, date, menuLayer, mergeLayer, loadLayer)) {
        delete ret;
        return nullptr;
    }

    ret->autorelease();
    return ret;
}

bool MacroCell::init(std::filesystem::path path, std::string name, std::time_t date, geode::Popup* menuLayer, geode::Popup* mergeLayer, CCLayer* loadLayer) {

    this->path = path;
    this->date = date;
    this->name = name;
    this->menuLayer = menuLayer;
    this->mergeLayer = mergeLayer;
    this->loadLayer = loadLayer;
    this->isMerge = mergeLayer != nullptr;

    bool autosave = false;

    size_t pos = name.find('_');
    if (pos != std::string::npos) {
        std::string firstPart = name.substr(0, pos);
        std::string secondPart = name.substr(pos + 1);

        if (firstPart == "autosave") {
            pos = secondPart.find('_');

            if (pos != std::string::npos) {
                std::string str = secondPart.substr(pos + 1);

                if (std::all_of(str.begin(), str.end(), ::isdigit)) {
                    autosave = true;
                    this->name = secondPart.substr(0, pos);
                }
            }
        }
    }

    menu = CCMenu::create();
    menu->setPosition({0, 0});
    addChild(menu);

    CCLabelBMFont* lbl = CCLabelBMFont::create(this->name.c_str(), "chatFont.fnt");
    lbl->limitLabelWidth(194.f, 0.8f, 0.01f);
    lbl->setAnchorPoint({0, 0.5});
    lbl->updateLabel();
    addChild(lbl);

    lbl->setPosition({10, 23});

#ifdef GEODE_IS_WINDOWS
    std::string subText = Utils::formatTime(date) + " | ";
    subText += autosave ? "Auto Save" : path.extension().string();

    lbl = CCLabelBMFont::create(subText.c_str(), "chatFont.fnt");
#else
    std::string subText = autosave ? "Auto Save" : path.extension().string();

    lbl = CCLabelBMFont::create(subText.c_str(), "chatFont.fnt");
#endif

    lbl->setPosition({10, 9});
    lbl->setScale(0.55f);
    lbl->setSkewX(2);
    lbl->setAnchorPoint({0, 0.5});
    lbl->setOpacity(80);
    addChild(lbl);

    std::string btnText = isMerge ? "Merge" : "Load";

    ButtonSprite* spr = ButtonSprite::create(btnText.c_str());
    spr->setScale(isMerge ? 0.5425f : 0.62f);

    CCMenuItemSpriteExtra* btn = CCMenuItemSpriteExtra::create(
        spr,
        this,
        menu_selector(MacroCell::onLoad)
    );

    btn->setPosition(ccp(isMerge ? 277.26f : 288.26f, 17.5f));
    menu->addChild(btn);

    CCSprite* spr2 = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
    spr2->setScale(0.485f);

    btn = CCMenuItemSpriteExtra::create(
        spr2,
        this,
        menu_selector(MacroCell::onDelete)
    );

    btn->setPosition(ccp(246, 17.5f));

    if (!isMerge)
        menu->addChild(btn);

    CCSprite* spriteOn = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    CCSprite* spriteOff = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");

    toggler = CCMenuItemToggler::create(
        spriteOff,
        spriteOn,
        this,
        menu_selector(MacroCell::onSelect)
    );

    toggler->setScale(0.485f);
    toggler->setPosition({220, 17.5});

    if (!isMerge)
        menu->addChild(toggler);

    return true;
}

void MacroCell::handleLoad() {
    auto& g = Global::get();

    Macro newMacro;

    // Single loading path for every format (.gdr, .gdr.json, .gdr2, .wbr, .xd, .slc2, .slc3). The format of GDR1 /
    // GDR2 / WBR files is detected from the content, and the current macro (g.macro) is never modified here, so a
    // failed load or a merge leaves it intact.
    auto loaded = Macro::readFile(path, newMacro);

    if (!loaded.ok) {
        if (!isMerge) {
            std::string text =
                "There was an error loading this macro. ID: " +
                std::to_string(loaded.errorId);

            if (!loaded.message.empty())
                text += "\n<cy>" + loaded.message + "</c>";

            return FLAlertLayer::create(
                "Error",
                text.c_str(),
                "Ok"
            )->show();
        }

        return;
    }

    if (isMerge) {
        bool players[2] = {true, true};

        bool p1 = static_cast<LoadMacroLayer*>(loadLayer)
            ->p1Toggle->isToggled();

        bool p2 = static_cast<LoadMacroLayer*>(loadLayer)
            ->p2Toggle->isToggled();

        if (p1)
            players[1] = false;
        else if (p2)
            players[0] = false;

        if (mergeLayer) {
            typeinfo_cast<MacroEditLayer*>(mergeLayer)->mergeMacro(
                newMacro.inputs,
                players,
                static_cast<LoadMacroLayer*>(loadLayer)
                    ->owToggle->isToggled()
            );

            loadLayer->setKeypadEnabled(false);
            loadLayer->setTouchEnabled(false);
            loadLayer->removeFromParentAndCleanup(true);
        }

        return;
    }

    g.macro = newMacro;
    g.currentAction = 0;
    g.currentFrameFix = 0;
    g.restart = true;
    g.macro.canChangeFPS = false;

    // Check whether the loaded macro was created by Dll Bot.
    g.macro.DllBotMacro = g.macro.botInfo.name == "Dll Bot";

    bool legacyGdrMacro =
        path.extension() == ".gdr" && !g.macro.DllBotMacro;

    loadLayer->setKeypadEnabled(false);
    loadLayer->setTouchEnabled(false);
    loadLayer->removeFromParentAndCleanup(true);

    RecordLayer* newLayer = nullptr;

    if (RecordLayer* layer = typeinfo_cast<RecordLayer*>(menuLayer)) {
        layer->onClose(nullptr);
        newLayer = RecordLayer::openMenu(true);
    }

    if (!newLayer)
        newLayer = g.layer != nullptr
            ? static_cast<RecordLayer*>(g.layer)
            : nullptr;

    if (newLayer)
        newLayer->updateTPS();

    if (!PlayLayer::get() && g.state != state::playing)
        Macro::togglePlaying();
    else if (g.state == state::recording) {
        if (newLayer) {
            newLayer->recording->toggle(
                Global::get().state != state::recording
            );

            newLayer->toggleRecording(nullptr);
        }
        else {
            RecordLayer* layer = RecordLayer::create();
            layer->toggleRecording(nullptr);
            layer->onClose(nullptr);
        }
    }

    if (path.extension() == ".xd")
        FLAlertLayer::create(
            "Warning",
            "<cl>.xd</c> extension macros may not function correctly in this version.",
            "Ok"
        )->show();

    if (legacyGdrMacro) {
        Notification::create(
            "Using Legacy .gdr format. Trajectory can be broken",
            NotificationIcon::Warning
        )->show();
    }

    Notification::create(
        "Macro Loaded",
        NotificationIcon::Success
    )->show();
}

void MacroCell::onLoad(CCObject*) {
    if (Global::get().macro.inputs.empty() || isMerge)
        return handleLoad();

    geode::createQuickPopup(
        "Warning",
        "Replace the current <cy>" +
            std::to_string(Global::get().macro.inputs.size()) +
            "</c> macro actions?",
        "Cancel",
        "Yes",
        [this](auto, bool btn2) {
            if (btn2)
                this->handleLoad();
        }
    );
}

void MacroCell::onDelete(CCObject*) {
    geode::createQuickPopup(
        "Warning",
        "Are you sure you want to <cr>delete</c> this macro? (\"<cl>" +
            name +
            "</c>\")",
        "Cancel",
        "Yes",
        [this](auto, bool btn2) {
            if (btn2)
                this->deleteMacro(true);
        }
    );
}

void MacroCell::deleteMacro(bool reload) {
    std::error_code ec;

    std::filesystem::remove(path, ec);

    if (ec) {
        return FLAlertLayer::create(
            "Error",
            "There was an error deleting this macro. ID: 7",
            "Ok"
        )->show();
    }
    else {
        if (reload) {
            static_cast<LoadMacroLayer*>(loadLayer)->reloadList();

            Notification::create(
                "Macro Deleted",
                NotificationIcon::Success
            )->show();
        }

        this->removeFromParentAndCleanup(true);
    }
}

void MacroCell::onSelect(CCObject*) {
    selectMacro(true);
}

void MacroCell::selectMacro(bool single) {
    LoadMacroLayer* layer =
        static_cast<LoadMacroLayer*>(loadLayer);

    std::vector<MacroCell*>& selectedMacros =
        layer->selectedMacros;

    auto it = std::remove(
        selectedMacros.begin(),
        selectedMacros.end(),
        this
    );

    if (it != selectedMacros.end()) {
        selectedMacros.erase(
            it,
            selectedMacros.end()
        );

        if (single)
            layer->selectAllToggle->toggle(false);
    }
    else {
        selectedMacros.push_back(this);
    }

    if (selectedMacros.size() == layer->allMacros.size() && single)
        layer->selectAllToggle->toggle(true);
}

#ifdef _MSC_VER
#pragma optimize("", on)
#endif
