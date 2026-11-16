#pragma once

#include "../hacks/noclip_tint.hpp"
#include "../includes.hpp"

// Noclip settings: which players are protected, and the death tint (toggle, color, opacity, time).
// Saved values: macro_noclip_p1 / _p2 / _tint (bool), macro_noclip_color (ccColor3B),
// macro_noclip_opacity (0-100) and macro_noclip_time (0-5 seconds).
class NoclipSettingsLayer : public geode::Popup {

public:
  STATIC_CREATE(NoclipSettingsLayer, 240, 260)

  void open(CCObject *) { create()->show(); }

private:
  static constexpr float kDefaultOpacity = 90.f;
  static constexpr float kMaxOpacity = 100.f;
  static constexpr float kMaxTime = 5.f;

  ColorChannelSprite *colorSprite = nullptr;
  TextInput *opacityInput = nullptr;
  TextInput *timeInput = nullptr;
  CCMenuItemToggler *tintToggle = nullptr;

  static cocos2d::ccColor3B defaultColor() { return {255, 0, 0}; }

  static cocos2d::ccColor3B savedColor() {
    return Mod::get()->getSavedValue<cocos2d::ccColor3B>("macro_noclip_color",
                                                         defaultColor());
  }

  CCMenuItemToggler *addToggle(const char *name, const char *id, bool value,
                               float y) {
    CCSprite *spriteOn = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    CCSprite *spriteOff = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");

    CCMenuItemToggler *toggle = CCMenuItemToggler::create(
        spriteOff, spriteOn, this, menu_selector(NoclipSettingsLayer::onToggle));
    toggle->setPosition({40, y});
    toggle->setScale(0.875f);
    toggle->setID(id);
    toggle->toggle(value);
    m_buttonMenu->addChild(toggle);

    addLabel(name, 66, y);
    return toggle;
  }

  void addLabel(const char *text, float x, float y) {
    CCLabelBMFont *lbl = CCLabelBMFont::create(text, "bigFont.fnt");
    lbl->setAnchorPoint({0.f, 0.5f});
    lbl->setPosition({x, y});
    lbl->setScale(0.5f);
    m_mainLayer->addChild(lbl);
  }

  TextInput *addNumberInput(const char *name, float y, const std::string &value) {
    TextInput *input = TextInput::create(60.f, "0");
    input->setPosition({40, y});
    input->setScale(0.65f);
    input->getInputNode()->setAllowedChars("0123456789.");
    input->getInputNode()->setDelegate(this);
    input->setString(value);
    m_mainLayer->addChild(input);

    addLabel(name, 82, y);
    return input;
  }

  bool setup() {
    setTitle("Noclip");

    auto *mod = Mod::get();

    addToggle("Player 1", "p1", mod->getSavedValue<bool>("macro_noclip_p1"), 210);
    addToggle("Player 2", "p2", mod->getSavedValue<bool>("macro_noclip_p2"), 178);
    tintToggle = addToggle("Tint", "tint",
                           mod->getSavedValue<bool>("macro_noclip_tint", false), 146);

    // Color
    colorSprite = ColorChannelSprite::create();
    colorSprite->setColor(savedColor());
    colorSprite->setScale(0.5f);

    CCMenuItemSpriteExtra *colorButton = CCMenuItemSpriteExtra::create(
        colorSprite, this,
        menu_selector(NoclipSettingsLayer::openColorPicker));
    colorButton->setPosition({40, 114});
    m_buttonMenu->addChild(colorButton);
    addLabel("Color", 66, 114);

    opacityInput = addNumberInput(
        "Opacity (%)", 82,
        fmt::format("{}", mod->getSavedValue<float>("macro_noclip_opacity",
                                                    kDefaultOpacity)));

    timeInput = addNumberInput(
        "Fade time (s)", 50,
        fmt::format("{}", mod->getSavedValue<float>("macro_noclip_time", 0.f)));

    ButtonSprite *btnSpr = ButtonSprite::create("Ok");
    btnSpr->setScale(0.73f);
    CCMenuItemSpriteExtra *btn = CCMenuItemSpriteExtra::create(
        btnSpr, this, menu_selector(NoclipSettingsLayer::onClose));
    btn->setPosition({m_size.width / 2, 20});
    m_buttonMenu->addChild(btn);

    btnSpr = ButtonSprite::create("Restore");
    btnSpr->setScale(0.3f);
    btn = CCMenuItemSpriteExtra::create(
        btnSpr, this, menu_selector(NoclipSettingsLayer::onRestore));
    btn->setPosition({m_size.width - 27, 14});
    m_buttonMenu->addChild(btn);

    return true;
  }

public:
  void onToggle(CCObject *obj) {
    CCMenuItemToggler *toggle = typeinfo_cast<CCMenuItemToggler *>(obj);
    if (!toggle)
      return;

    std::string id = toggle->getID();

    // The callback runs before the toggler flips, so the new value is the opposite.
    Mod::get()->setSavedValue("macro_noclip_" + id, !toggle->isToggled());
    NoclipTint::reloadSettings();
  }

private:
  void onRestore(CCObject *) {
    // Players keep their current choice; only the tint is reset.
    opacityInput->setString(fmt::format("{}", kDefaultOpacity));
    timeInput->setString("0");
    colorSprite->setColor(defaultColor());

    if (tintToggle->isToggled())
      tintToggle->toggle(false);

    Mod::get()->setSavedValue("macro_noclip_tint", false);
    Mod::get()->setSavedValue("macro_noclip_opacity", kDefaultOpacity);
    Mod::get()->setSavedValue("macro_noclip_time", 0.f);
    saveColor();
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

    Mod::get()->setSavedValue("macro_noclip_color", colorSprite->getColor());
    NoclipTint::reloadSettings();
  }

  // Only a valid number inside its range is saved; anything else (empty box, "0.",
  // "150") is ignored so typing a decimal does not wipe the field.
  void saveNumber(TextInput *input, const char *key, float maxValue) {
    auto parsed = geode::utils::numFromString<float>(input->getString());
    if (parsed.isErr())
      return;

    float value = parsed.unwrap();
    if (value < 0.f || value > maxValue)
      return;

    Mod::get()->setSavedValue(key, value);
    NoclipTint::reloadSettings();
  }

  void textChanged(CCTextInputNode *node) override {
    if (!opacityInput || !timeInput)
      return;

    if (node == opacityInput->getInputNode())
      saveNumber(opacityInput, "macro_noclip_opacity", kMaxOpacity);
    else if (node == timeInput->getInputNode())
      saveNumber(timeInput, "macro_noclip_time", kMaxTime);
  }
};
