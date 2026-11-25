#include "game_ui.hpp"
#include "../includes.hpp"

#include <Geode/modify/PlayLayer.hpp>

class HoldStepButton : public cocos2d::CCMenuItemSprite {
public:
  static HoldStepButton *create(cocos2d::CCNode *sprite,
                                cocos2d::CCObject *target,
                                cocos2d::SEL_MenuHandler selector) {
    auto ret = new HoldStepButton();
    if (ret &&
        ret->initWithNormalSprite(sprite, nullptr, nullptr, target, selector)) {
      ret->autorelease();
      return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
  }

  void selected() override {
    cocos2d::CCMenuItemSprite::selected();
    if (m_pNormalImage)
      m_pNormalImage->setVisible(true);
    Global::get().holdingStepForward = true;

    this->stopAllActions();
    this->runAction(cocos2d::CCScaleTo::create(0.1f, 1.2f));
  }

  void unselected() override {
    cocos2d::CCMenuItemSprite::unselected();
    Global::get().holdingStepForward = false;

    this->stopAllActions();
    this->runAction(cocos2d::CCScaleTo::create(0.1f, 1.0f));
  }
};

class $modify(PlayLayer) {

  struct Fields {
    CCLabelBMFont *frameLabel = nullptr;
  };

  void postUpdate(float dt) {
    PlayLayer::postUpdate(dt);
    auto &g = Global::get();

    if (g.state != state::none && g.frameLabel && !g.renderer.recording)
      m_fields->frameLabel->setString(
          ("Frame: " + std::to_string(Global::getCurrentFrame())).c_str());
  }

  bool init(GJGameLevel *level, bool b1) {
    if (!PlayLayer::init(level, b1))
      return false;

    Interface::addLabels(this);
    Interface::addButtons(this);

    m_fields->frameLabel =
        static_cast<CCLabelBMFont *>(getChildByID("frame-label"_spr));

    return true;
  }
};

void Interface::addLabels(PlayLayer *pl) {
  CCLabelBMFont *lbl = CCLabelBMFont::create("", "chatFont.fnt");
  lbl->setPosition(
      {CCDirector::sharedDirector()->getWinSize().width - 6.5f, 12});
  lbl->setAnchorPoint({1, 0.5});
  lbl->setID("state-label"_spr);
  lbl->setZOrder(300);
  lbl->setScale(0.625f);
  pl->addChild(lbl);

  lbl = CCLabelBMFont::create("", "chatFont.fnt");
  lbl->setPosition({6.5f, 12});
  lbl->setAnchorPoint({0, 0.5});
  lbl->setID("frame-label"_spr);
  lbl->setZOrder(300);
  lbl->setScale(0.625f);
  pl->addChild(lbl);

  lbl = CCLabelBMFont::create("Recording Audio", "bigFont.fnt");
  lbl->setPosition(pl->getContentSize() / 2);
  lbl->setID("recording-audio-label"_spr);
  lbl->setZOrder(300);
  lbl->setOpacity(75);
  lbl->setVisible(false);
  pl->addChild(lbl);

  Interface::updateLabels();
}

void Interface::addButtons(PlayLayer *pl) {
  cocos2d::CCSize winSize = CCDirector::sharedDirector()->getWinSize();

  CCMenu *menu = CCMenu::create();
  menu->setZOrder(300);
  menu->setPosition({0, 0});
  menu->setID("button-menu"_spr);
  pl->addChild(menu);

  CCSprite *spr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
  spr->setFlipX(true);

  CCMenuItemSpriteExtra *btn = CCMenuItemSpriteExtra::create(
      spr, pl, menu_selector(Interface::onFrameStepper));
  btn->setAnchorPoint({0, 0});
  btn->setID("step-frame-btn");
  CCSprite *sprite = btn->getChildByType<CCSprite>(0);
  sprite->setPosition({0, 0});

  menu->addChild(btn);

  spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
  spr->setFlipX(true);

  HoldStepButton *holdBtn =
      HoldStepButton::create(spr, pl, menu_selector(Interface::onHoldStepper));
  holdBtn->setAnchorPoint({0, 0});
  holdBtn->setID("hold-frame-btn");
  sprite = holdBtn->getChildByType<CCSprite>(0);
  sprite->setPosition({0, 0});

  menu->addChild(holdBtn);

  spr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");

  btn = CCMenuItemSpriteExtra::create(
      spr, pl, menu_selector(Interface::onFrameStepperOff));
  btn->setID("disable-stepper-btn");
  btn->setAnchorPoint({0, 0});
  sprite = btn->getChildByType<CCSprite>(0);
  sprite->setPosition({0, 0});

  menu->addChild(btn);

  spr = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");

  btn = CCMenuItemSpriteExtra::create(spr, pl,
                                      menu_selector(Interface::onSpeedhack));
  btn->setAnchorPoint({0, 0});
  btn->setID("speedhack-btn");
  sprite = btn->getChildByType<CCSprite>(0);
  sprite->setPosition({0, 0});

  menu->addChild(btn);

  Interface::updateButtons();
}

void Interface::updateLabels() {
  PlayLayer *pl = PlayLayer::get();
  auto &g = Global::get();

  if (!pl)
    return;

  if (g.state == state::none || !g.frameLabel)
    static_cast<CCLabelBMFont *>(pl->getChildByID("frame-label"_spr))
        ->setString("");

  CCLabelBMFont *label =
      typeinfo_cast<CCLabelBMFont *>(pl->getChildByID("state-label"_spr));

  if (!label)
    return;

  if (g.mod->getSavedValue<bool>("macro_hide_labels"))
    return label->setString("");

  state state = g.state;
  std::string labelText = state == state::none ? "" : "Playing";
  if (state == state::recording)
    labelText = "Recording";

  if (labelText == "Recording" && state == state::recording &&
      g.mod->getSavedValue<bool>("macro_hide_recording_label"))
    labelText = "";

  if (labelText == "Playing" && state == state::playing &&
      g.mod->getSavedValue<bool>("macro_hide_playing_label"))
    labelText = "";

  if (g.renderer.recording &&
      g.mod->getSavedValue<bool>("render_hide_labels")) {
    labelText = "";
    if (CCLabelBMFont *lbl =
            typeinfo_cast<CCLabelBMFont *>(pl->getChildByID("frame-label"_spr)))
      lbl->setString("");
  }

  label->setString(labelText.c_str());
}

void Interface::updateButtons() {
  PlayLayer *pl = PlayLayer::get();
  if (!pl)
    return;

  CCNode *menu = pl->getChildByID("button-menu"_spr);
  if (!menu)
    return;

  auto &g = Global::get();

#ifdef GEODE_IS_WINDOWS
  bool isWindows = true;
#else
  bool isWindows = false;
#endif

  CCNode *disableStepperBtn = menu->getChildByID("disable-stepper-btn");
  CCNode *stepFrameBtn = menu->getChildByID("step-frame-btn");
  CCNode *holdFrameBtn = menu->getChildByID("hold-frame-btn");
  CCNode *speedhackBtn = menu->getChildByID("speedhack-btn");

  disableStepperBtn->setPosition(
      ccp(g.mod->getSavedValue<float>("button_off_pos_x"),
          g.mod->getSavedValue<float>("button_off_pos_y")));

  float scale = g.mod->getSavedValue<float>("button_off_scale");

  CCSprite *sprite = disableStepperBtn->getChildByType<CCSprite>(0);
  sprite->setScale(scale);
  sprite->setOpacity(static_cast<int>(
      g.mod->getSavedValue<float>("button_off_opacity") * 255));
  sprite->setAnchorPoint({0, 0});

  cocos2d::CCSize size = sprite->getContentSize();
  disableStepperBtn->setContentSize({size.width * scale, size.height * scale});

  stepFrameBtn->setPosition(
      ccp(g.mod->getSavedValue<float>("button_advance_frame_pos_x"),
          g.mod->getSavedValue<float>("button_advance_frame_pos_y")));

  scale = g.mod->getSavedValue<float>("button_advance_frame_scale");

  sprite = stepFrameBtn->getChildByType<CCSprite>(0);
  sprite->setScale(scale);
  sprite->setOpacity(static_cast<int>(
      g.mod->getSavedValue<float>("button_advance_frame_opacity") * 255));
  sprite->setAnchorPoint({0, 0});

  size = sprite->getContentSize();
  stepFrameBtn->setContentSize({size.width * scale, size.height * scale});

  holdFrameBtn->setPosition(
      ccp(g.mod->getSavedValue<float>("button_advance_frame_pos_x") +
              size.width * scale + 5.f,
          g.mod->getSavedValue<float>("button_advance_frame_pos_y")));

  sprite = holdFrameBtn->getChildByType<CCSprite>(0);
  sprite->setScale(scale);
  sprite->setOpacity(static_cast<int>(
      g.mod->getSavedValue<float>("button_advance_frame_opacity") * 255));
  sprite->setAnchorPoint({0, 0});

  cocos2d::CCSize holdSize = sprite->getContentSize();
  holdFrameBtn->setContentSize(
      {holdSize.width * scale, holdSize.height * scale});

  speedhackBtn->setPosition(
      ccp(g.mod->getSavedValue<float>("button_speedhack_pos_x"),
          g.mod->getSavedValue<float>("button_speedhack_pos_y")));

  scale = g.mod->getSavedValue<float>("button_speedhack_scale");

  sprite = speedhackBtn->getChildByType<CCSprite>(0);
  sprite->setScale(scale);
  sprite->setOpacity(static_cast<int>(
      g.mod->getSavedValue<float>("button_speedhack_opacity") * 255));
  sprite->setAnchorPoint({0, 0});

  size = sprite->getContentSize();
  speedhackBtn->setContentSize({size.width * scale, size.height * scale});

  if (isWindows) {
    disableStepperBtn->setVisible(false);
    stepFrameBtn->setVisible(false);
    holdFrameBtn->setVisible(false);
    speedhackBtn->setVisible(false);

    return;
  }

  bool isRecordingMode =
      (g.state == state::recording ||
       g.mod->getSavedValue<bool>("macro_always_show_buttons"));

  speedhackBtn->setVisible(isRecordingMode &&
                           !g.mod->getSavedValue<bool>("macro_hide_speedhack"));

  if (g.mod->getSavedValue<bool>("macro_hide_stepper")) {
    disableStepperBtn->setVisible(false);
    stepFrameBtn->setVisible(false);
    holdFrameBtn->setVisible(false);
  } else {
    stepFrameBtn->setVisible(isRecordingMode || g.frameStepper);
    holdFrameBtn->setVisible(g.frameStepper);
    disableStepperBtn->setVisible(g.frameStepper);
  }
}