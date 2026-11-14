#pragma once

#include "../includes.hpp"
#include "../utils/utils.hpp"

class RenderSettingsLayer : public geode::Popup {

public:
  Slider *sfxSlider = nullptr;
  Slider *musicSlider = nullptr;
  TextInput *fadeInInput = nullptr;
  TextInput *fadeOutInput = nullptr;
  TextInput *extensionInput = nullptr;

  CCTextInputNode *argsInput = nullptr;
  CCTextInputNode *audioArgsInput = nullptr;
  CCTextInputNode *secondsInput = nullptr;
  CCTextInputNode *videoArgsInput = nullptr;

  CCMenuItemToggler *onlySongToggle = nullptr;
  CCMenuItemToggler *recordAudioToggle = nullptr;
  CCMenuItemSpriteExtra *hdrCodecBtn = nullptr;

  Mod *mod = nullptr;

private:
  bool setup();

public:
  STATIC_CREATE(RenderSettingsLayer, 396, 277)

  void open(CCObject *) { create()->show(); }

  void close(CCObject *) { keyBackClicked(); }

  void textChanged(CCTextInputNode *node) override;

  void onSlider(CCObject *);

  void onDefaults(CCObject *);

  void showInfoPopup(CCObject *);

  void updateManualArgState();

  void onHDRCodec(CCObject *);
};
