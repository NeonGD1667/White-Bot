#pragma once

#include "../includes.hpp"

class AutoSwiftClickSettingsLayer : public geode::Popup {

private:
  TextInput *countInput = nullptr;

  STATIC_CREATE(AutoSwiftClickSettingsLayer, 200, 120)

  // Geode v6: TextInput callbacks replace the removed TextInputDelegate.
  void textChanged() {
    auto &g = Global::get();
    g.autoSwiftClickCount =
        geode::utils::numFromString<int>(countInput->getString()).unwrapOr(2);

    if (g.autoSwiftClickCount < 2)
      g.autoSwiftClickCount = 2;
    if (g.autoSwiftClickCount > 50)
      g.autoSwiftClickCount = 50;

    g.mod->setSavedValue("auto_swift_click_count", g.autoSwiftClickCount);
  }

  bool setup() {
    setTitle("Auto Swift Click");
    m_title->setScale(0.55f);
    m_title->setPositionY(106);

    // Click Count
    CCLabelBMFont *lbl = CCLabelBMFont::create("Clicks / Press", "bigFont.fnt");
    lbl->setPosition({m_size.width / 2, 80});
    lbl->setScale(0.35f);
    m_mainLayer->addChild(lbl);

    countInput = TextInput::create(60, "Num", "chatFont.fnt");
    countInput->setPosition({m_size.width / 2, 58});
    countInput->setString(std::to_string(Mod::get()->getSavedValue<int64_t>(
                                             "auto_swift_click_count", 2))
                              .c_str());
    countInput->setCallback([this](std::string const &) { textChanged(); });
    countInput->setFilter("0123456789");
    countInput->setMaxCharCount(2);
    m_mainLayer->addChild(countInput);

    CCLabelBMFont *hint =
        CCLabelBMFont::create("Every press = N clicks", "chatFont.fnt");
    hint->setPosition({m_size.width / 2, 38});
    hint->setScale(0.3f);
    hint->setOpacity(100);
    m_mainLayer->addChild(hint);

    return true;
  }

public:
  void open(CCObject *) { create()->show(); }
};
