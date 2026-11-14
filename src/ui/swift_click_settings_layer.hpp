#pragma once

#include "../includes.hpp"
#include "../utils/utils.hpp"

class SwiftClickSettingsLayer : public geode::Popup {

private:
  TextInput *countInput = nullptr;
  TextInput *keyInput = nullptr;

  STATIC_CREATE(SwiftClickSettingsLayer, 200, 150)

  void textChanged(CCTextInputNode *) override {
    auto &g = Global::get();
    g.swiftClickCount =
        geode::utils::numFromString<int>(countInput->getString()).unwrapOr(2);
    g.swiftClickKey =
        geode::utils::numFromString<int>(keyInput->getString()).unwrapOr(72);

    if (g.swiftClickCount < 1)
      g.swiftClickCount = 1;
    if (g.swiftClickCount > 20)
      g.swiftClickCount = 20;

    g.mod->setSavedValue("swift_click_count", g.swiftClickCount);
    g.mod->setSavedValue("swift_click_key", g.swiftClickKey);
  }

  bool setup() {
    setTitle("Swift Click");
    m_title->setScale(0.625f);
    m_title->setPositionY(136);

    // Swift Count
    CCLabelBMFont *lbl = CCLabelBMFont::create("Click Count", "bigFont.fnt");
    lbl->setPosition({m_size.width / 2, 110});
    lbl->setScale(0.35f);
    m_mainLayer->addChild(lbl);

    countInput = TextInput::create(60, "Count", "chatFont.fnt");
    countInput->setPosition({m_size.width / 2, 88});
    countInput->setString(std::to_string(Mod::get()->getSavedValue<int64_t>(
                                             "swift_click_count", 2))
                              .c_str());
    countInput->getInputNode()->setDelegate(this);
    countInput->getInputNode()->setAllowedChars("0123456789");
    countInput->getInputNode()->setMaxLabelLength(2);
    m_mainLayer->addChild(countInput);

    // Key Code
    lbl = CCLabelBMFont::create("Key Code", "bigFont.fnt");
    lbl->setPosition({m_size.width / 2, 68});
    lbl->setScale(0.35f);
    m_mainLayer->addChild(lbl);

    keyInput = TextInput::create(60, "Key", "chatFont.fnt");
    keyInput->setPosition({m_size.width / 2, 46});
    keyInput->setString(std::to_string(Mod::get()->getSavedValue<int64_t>(
                                           "swift_click_key", 72))
                            .c_str());
    keyInput->getInputNode()->setDelegate(this);
    keyInput->getInputNode()->setAllowedChars("0123456789");
    keyInput->getInputNode()->setMaxLabelLength(3);
    m_mainLayer->addChild(keyInput);

    CCLabelBMFont *hint =
        CCLabelBMFont::create("72=H, 71=G, 74=J", "chatFont.fnt");
    hint->setPosition({m_size.width / 2, 30});
    hint->setScale(0.35f);
    hint->setOpacity(100);
    m_mainLayer->addChild(hint);

    return true;
  }

public:
  void open(CCObject *) { create()->show(); }
};
