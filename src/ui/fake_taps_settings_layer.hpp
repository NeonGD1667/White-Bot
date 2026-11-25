#pragma once

#include "../hacks/fake_taps.hpp"
#include "../includes.hpp"

// Settings for the Faketaps hack: circle scale and color.
// Saved values: "faketaps_scale" (float, default 0.5) and "faketaps_color" (ccColor3B, default white).
class FakeTapsSettingsLayer : public geode::Popup {

public:
  STATIC_CREATE(FakeTapsSettingsLayer, 220, 130)

  void open(CCObject *) { create()->show(); }

private:
  static constexpr float kMinScale = 0.1f;
  static constexpr float kMaxScale = 5.f;

  ColorChannelSprite *colorSprite = nullptr;
  TextInput *scaleInput = nullptr;

  bool setup() {
    setTitle("Faketaps");

    float x1 = 60.f;
    float x2 = m_size.width - 60.f;

    CCLabelBMFont *lbl = CCLabelBMFont::create("Faketaps Scale", "bigFont.fnt");
    lbl->setPosition({x1, 90});
    lbl->setScale(0.35f);
    m_mainLayer->addChild(lbl);

    lbl = CCLabelBMFont::create("Faketaps Color", "bigFont.fnt");
    lbl->setPosition({x2, 90});
    lbl->setScale(0.35f);
    m_mainLayer->addChild(lbl);

    scaleInput = TextInput::create(50.f, "0.5");
    scaleInput->setPosition({x1, 66});
    scaleInput->setScale(0.65f);
    scaleInput->setFilter("0123456789.");
    scaleInput->setCallback([this](std::string const &) { textChanged(); });
    scaleInput->setString(fmt::format("{}", FakeTapsSettingsLayer::savedScale()));
    m_mainLayer->addChild(scaleInput);

    colorSprite = ColorChannelSprite::create();
    colorSprite->setColor(FakeTapsSettingsLayer::savedColor());
    colorSprite->setScale(0.5f);

    CCMenuItemSpriteExtra *colorButton = CCMenuItemSpriteExtra::create(
        colorSprite, this,
        menu_selector(FakeTapsSettingsLayer::openColorPicker));
    colorButton->setPosition({x2, 68});
    m_buttonMenu->addChild(colorButton);

    ButtonSprite *btnSpr = ButtonSprite::create("Ok");
    btnSpr->setScale(0.73f);
    CCMenuItemSpriteExtra *btn = CCMenuItemSpriteExtra::create(
        btnSpr, this, menu_selector(FakeTapsSettingsLayer::onClose));
    btn->setPosition({m_size.width / 2, 25});
    m_buttonMenu->addChild(btn);

    btnSpr = ButtonSprite::create("Restore");
    btnSpr->setScale(0.3f);
    btn = CCMenuItemSpriteExtra::create(
        btnSpr, this, menu_selector(FakeTapsSettingsLayer::onRestore));
    btn->setPosition({m_size.width - 27, 14});
    m_buttonMenu->addChild(btn);

    return true;
  }

  static float savedScale() {
    return Mod::get()->getSavedValue<float>("faketaps_scale", 0.5f);
  }

  static cocos2d::ccColor3B savedColor() {
    return Mod::get()->getSavedValue<cocos2d::ccColor3B>(
        "faketaps_color", cocos2d::ccColor3B{255, 255, 255});
  }

  void onRestore(CCObject *) {
    scaleInput->setString("0.5");
    colorSprite->setColor(cocos2d::ccColor3B{255, 255, 255});

    saveColor();
    textChanged();
  }

  void openColorPicker(CCObject *) {
    ColorPickPopup *popup = ColorPickPopup::create(colorSprite->getColor());
    popup->setColorTarget(colorSprite);
    popup->setCallback(
        [this](const cocos2d::ccColor4B &) { this->saveColor(); });
    popup->show();
  }

  void saveColor() {
    if (!colorSprite)
      return;

    Mod::get()->setSavedValue("faketaps_color", colorSprite->getColor());
    FakeTaps::reloadSettings();
  }

  // Only a valid number inside [kMinScale, kMaxScale] is saved; anything else (empty box,
  // "0.", "9") is ignored so typing a decimal does not wipe the field.
  // Geode v6: TextInput callbacks replace the removed TextInputDelegate.
  void textChanged() {
    if (!scaleInput)
      return;

    auto parsed = geode::utils::numFromString<float>(scaleInput->getString());
    if (parsed.isErr())
      return;

    float scale = parsed.unwrap();
    if (scale < kMinScale || scale > kMaxScale)
      return;

    Mod::get()->setSavedValue("faketaps_scale", scale);
    FakeTaps::reloadSettings();
  }
};
