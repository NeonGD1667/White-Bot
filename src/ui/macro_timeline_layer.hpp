#pragma once

#include "../includes.hpp"

#include <Geode/ui/GeodeUI.hpp>

class MacroTimelineLayer : public geode::Popup {
private:
  bool setup();
  void onClose(CCObject *) override;
  void keyBackClicked() override { onClose(nullptr); }
  void textChanged(CCTextInputNode *input) override;

  std::vector<input> originalInputs;
  std::vector<input> inputs;

  Slider *timelineSlider = nullptr;
  cocos2d::CCDrawNode *markerNode = nullptr;

  CCLabelBMFont *frameLabel = nullptr;
  CCLabelBMFont *selectedLabel = nullptr;
  CCLabelBMFont *statusLabel = nullptr;
  CCLabelBMFont *frameValueLabel = nullptr;
  CCLabelBMFont *actionValueLabel = nullptr;
  CCLabelBMFont *buttonValueLabel = nullptr;
  CCLabelBMFont *playerValueLabel = nullptr;

  TextInput *frameInput = nullptr;

  CCMenu *editorMenu = nullptr;

  int currentFrame = 0;
  int selectedIndex = -1;
  int maxFrame = 1;
  bool saved = true;
  bool cursorWasHidden = false;
  bool previewWasPlaying = false;

  void rebuildMarkers();
  void setCurrentFrame(int frame, bool updateSlider = true);
  void selectNearestInput();
  void refreshSelectedInfo();
  void refreshStatus();
  void markUnsaved();
  void sortInputs();

  void onSlider(CCObject *);
  void onPrevInput(CCObject *);
  void onNextInput(CCObject *);
  void onDeleteInput(CCObject *);
  void onAddInput(CCObject *);
  void onSave(CCObject *);
  void switchButton(CCObject *);
  void switchAction(CCObject *);
  void switchPlayer(CCObject *);
  void nudgeFrame(CCObject *);

public:
  STATIC_CREATE(MacroTimelineLayer, 580, 320)

  static void open();
};
