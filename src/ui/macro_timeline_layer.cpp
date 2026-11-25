#include "macro_timeline_layer.hpp"

#include "game_ui.hpp"
#include "macro_editor.hpp"

#include <algorithm>

namespace {
constexpr float kEdgeMargin = 10.f;
constexpr float kBottomDockHeight = 176.f;
constexpr float kTrackHeight = 24.f;
constexpr float kTrackY = 74.f;
constexpr float kLaneGap = 26.f;
constexpr float kSidePanelWidth = 182.f;
constexpr float kSidePanelHeight = 172.f;
constexpr ccColor3B kPanelBg = {18, 22, 30};
constexpr ccColor3B kPanelOutline = {255, 170, 35};
constexpr ccColor3B kTrackBg = {8, 12, 18};
constexpr ccColor3B kTrackGlow = {35, 255, 120};
constexpr ccColor3B kValueBoxBg = {14, 14, 18};
constexpr ccColor3B kGold = {255, 211, 90};
constexpr ccColor3B kMutedText = {170, 176, 190};
std::string getButtonName(int button) {
  if (btnNames.contains(button))
    return btnNames.at(button);
  return std::to_string(button);
}
float frameToRatio(int frame, int maxFrame) {
  if (maxFrame <= 0)
    return 0.f;
  return std::clamp(static_cast<float>(frame) / static_cast<float>(maxFrame),
                    0.f, 1.f);
}
} // namespace

// ══════════════════════════════════════════════════════════════════════
//  open()
// ══════════════════════════════════════════════════════════════════════
void MacroTimelineLayer::open() {
  if (!PlayLayer::get()) {
    FLAlertLayer::create("Timeline", "Open a level first.", "Ok")->show();
    return;
  }

  auto *layer = create();
  layer->m_noElasticity = true;
  layer->show();
}

// ══════════════════════════════════════════════════════════════════════
//  setup()  —  REDESIGNED LAYOUT
//  All coordinates use m_size (580x320) — the popup's local space.
//  (0,0) = bottom-left of popup, (m_size.width, m_size.height) = top-right.
// ══════════════════════════════════════════════════════════════════════
bool MacroTimelineLayer::setup() {
  auto &g = Global::get();
  auto winSize = CCDirector::sharedDirector()->getWinSize();

  // BYPASS Geode::Popup layout
  // We force the main layer to be fullscreen and anchored at bottom-left (0,0)
  m_mainLayer->ignoreAnchorPointForPosition(true);
  m_mainLayer->setAnchorPoint({0.f, 0.f});
  m_mainLayer->setPosition({0.f, 0.f});
  m_mainLayer->setContentSize(winSize);

  float W = winSize.width;
  float H = winSize.height;

#ifdef GEODE_IS_WINDOWS
  cursorWasHidden =
      cocos2d::CCEGLView::sharedOpenGLView()->getShouldHideCursor();
  cocos2d::CCEGLView::sharedOpenGLView()->showCursor(true);
#endif
  originalInputs = g.macro.inputs;
  inputs = originalInputs;
  if (!inputs.empty())
    maxFrame = std::max(1, static_cast<int>(inputs.back().frame));
  previewWasPlaying = g.state == state::playing;
  if (g.state == state::recording)
    Macro::resetState(true);
  g.state = state::playing;
  g.currentAction = 0;
  g.currentFrameFix = 0;
  g.macro.DllBotMacro = g.macro.botInfo.name == "Dll Bot";
  Interface::updateLabels();
  Interface::updateButtons();

  // Hide default popup background, keep m_mainLayer as-is (centered by Popup)
  m_bgSprite->setOpacity(0);
  if (m_closeBtn)
    m_closeBtn->setVisible(false);

  // Dim overlay — covers the full popup area
  auto *screenDim = CCLayerColor::create({0, 0, 0, 92});
  screenDim->setContentSize({W, H});
  screenDim->setPosition({0.f, 0.f});
  m_mainLayer->addChild(screenDim, -5);

  // ── BOTTOM DOCK ──────────────────────────────────────────────────
  float dockLeft = kEdgeMargin;
  float dockBottom = kEdgeMargin;
  float dockWidth = W - kEdgeMargin * 2.f;
  float dockCenterY = dockBottom + kBottomDockHeight / 2.f;

  auto *bottomDock =
      CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
  bottomDock->setContentSize({dockWidth, kBottomDockHeight});
  bottomDock->setColor(kTrackBg);
  bottomDock->setOpacity(220);
  bottomDock->setPosition({W / 2.f, dockCenterY});
  m_mainLayer->addChild(bottomDock, 1);

  // ── INSPECTOR PANEL (right side) ─────────────────────────────────
  float inspectorWidth = kSidePanelWidth;
  float inspectorCenterX = dockLeft + dockWidth - inspectorWidth / 2.f - 12.f;
  float inspectorCenterY = dockCenterY;

  auto *inspectorOutline =
      CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
  inspectorOutline->setContentSize(
      {inspectorWidth + 5.f, kSidePanelHeight + 5.f});
  inspectorOutline->setColor(kPanelOutline);
  inspectorOutline->setOpacity(118);
  inspectorOutline->setPosition({inspectorCenterX, inspectorCenterY});
  m_mainLayer->addChild(inspectorOutline, 2);

  auto *inspectorPanel =
      CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
  inspectorPanel->setContentSize({inspectorWidth, kSidePanelHeight});
  inspectorPanel->setColor(kPanelBg);
  inspectorPanel->setOpacity(228);
  inspectorPanel->setPosition({inspectorCenterX, inspectorCenterY});
  m_mainLayer->addChild(inspectorPanel, 3);

  // ── TIMELINE AREA (left side) ────────────────────────────────────
  float timelineLeft = dockLeft + 14.f;
  float timelineRight = inspectorCenterX - inspectorWidth / 2.f - 18.f;
  float timelineWidth = timelineRight - timelineLeft;
  float timelineCenterX = timelineLeft + timelineWidth / 2.f;
  float trackCenterY = dockBottom + kTrackY;
  float laneWidth = timelineWidth - 28.f;
  float laneStartX = timelineLeft + 14.f;

  auto *timelinePanel =
      CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
  timelinePanel->setContentSize({timelineWidth, kBottomDockHeight - 22.f});
  timelinePanel->setColor(kPanelBg);
  timelinePanel->setOpacity(206);
  timelinePanel->setPosition({timelineCenterX, dockCenterY});
  m_mainLayer->addChild(timelinePanel, 2);

  auto *glow = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
  glow->setContentSize({laneWidth, 88.f});
  glow->setColor(kTrackGlow);
  glow->setOpacity(34);
  glow->setPosition({timelineCenterX, trackCenterY});
  m_mainLayer->addChild(glow, 3);

  auto *laneBg = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
  laneBg->setContentSize({laneWidth, 82.f});
  laneBg->setColor(kValueBoxBg);
  laneBg->setOpacity(150);
  laneBg->setPosition({timelineCenterX, trackCenterY});
  m_mainLayer->addChild(laneBg, 4);

  auto *p1Lane = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
  p1Lane->setContentSize({laneWidth, kTrackHeight});
  p1Lane->setColor({48, 56, 68});
  p1Lane->setOpacity(220);
  p1Lane->setPosition({timelineCenterX, trackCenterY + kLaneGap / 2.f});
  m_mainLayer->addChild(p1Lane, 5);

  auto *p2Lane = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
  p2Lane->setContentSize({laneWidth, kTrackHeight});
  p2Lane->setColor({48, 56, 68});
  p2Lane->setOpacity(220);
  p2Lane->setPosition({timelineCenterX, trackCenterY - kLaneGap / 2.f});
  m_mainLayer->addChild(p2Lane, 5);

  auto *p1Label = CCLabelBMFont::create("P1", "goldFont.fnt");
  p1Label->setScale(0.34f);
  p1Label->setPosition(
      {timelineLeft + 14.f, trackCenterY + kLaneGap / 2.f + 1.f});
  p1Label->setColor(kGold);
  m_mainLayer->addChild(p1Label, 6);

  auto *p2Label = CCLabelBMFont::create("P2", "goldFont.fnt");
  p2Label->setScale(0.34f);
  p2Label->setPosition(
      {timelineLeft + 14.f, trackCenterY - kLaneGap / 2.f + 1.f});
  p2Label->setColor(kGold);
  m_mainLayer->addChild(p2Label, 6);

  // ── INFO LABELS ──────────────────────────────────────────────────
  frameLabel = CCLabelBMFont::create("Frame: 0", "bigFont.fnt");
  frameLabel->setAnchorPoint({0.f, 0.5f});
  frameLabel->setScale(0.34f);
  frameLabel->setPosition(
      {timelineLeft + 2.f, dockBottom + kBottomDockHeight - 18.f});
  m_mainLayer->addChild(frameLabel, 6);

  selectedLabel = CCLabelBMFont::create("No input selected", "bigFont.fnt");
  selectedLabel->setScale(0.28f);
  selectedLabel->setColor(kGold);
  selectedLabel->setPosition(
      {timelineCenterX, dockBottom + kBottomDockHeight - 18.f});
  m_mainLayer->addChild(selectedLabel, 6);

  statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
  statusLabel->setAnchorPoint({1.f, 0.5f});
  statusLabel->setScale(0.52f);
  statusLabel->setColor(kMutedText);
  statusLabel->setPosition(
      {timelineRight - 6.f, dockBottom + kBottomDockHeight - 18.f});
  m_mainLayer->addChild(statusLabel, 6);

  // ── SLIDER (invisible bar, drives playhead) ──────────────────────
  timelineSlider =
      Slider::create(this, menu_selector(MacroTimelineLayer::onSlider), 1.f);
  timelineSlider->setPosition({laneStartX, trackCenterY - 11.f});
  timelineSlider->setAnchorPoint({0.f, 0.f});
  timelineSlider->setScale(laneWidth / 200.f);
  timelineSlider->setValue(0.f);
  timelineSlider->m_sliderBar->setOpacity(0);
  timelineSlider->m_touchLogic->setOpacity(0);
  if (auto *thumb = timelineSlider->getThumb()) {
    thumb->setVisible(true);
    thumb->setScale(1.35f);
    thumb->setOpacity(0);
  }
  m_mainLayer->addChild(timelineSlider, 8);

  // ── MARKER DRAW NODE ─────────────────────────────────────────────
  markerNode = cocos2d::CCDrawNode::create();
  markerNode->setPosition({0.f, 0.f});
  m_mainLayer->addChild(markerNode, 7);

  // ── EDITOR MENU ──────────────────────────────────────────────────
  editorMenu = CCMenu::create();
  editorMenu->setPosition({0.f, 0.f});
  m_mainLayer->addChild(editorMenu, 12);

  // ── Helper lambdas ───────────────────────────────────────────────
  auto addInspectorLabel = [&](char const *text, float x, float y,
                               float scale = 0.3f,
                               cocos2d::ccColor3B color = ccc3(230, 230, 230)) {
    auto *label = CCLabelBMFont::create(text, "bigFont.fnt");
    label->setAnchorPoint({0.f, 0.5f});
    label->setScale(scale);
    label->setColor(color);
    label->setPosition({x, y});
    m_mainLayer->addChild(label, 13);
    return label;
  };

  auto addValueBox = [&](float x, float y, float width, CCLabelBMFont *&label) {
    auto *bg = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
    bg->setContentSize({width, 24.f});
    bg->setColor(kValueBoxBg);
    bg->setOpacity(150);
    bg->setPosition({x, y});
    m_mainLayer->addChild(bg, 13);
    label = CCLabelBMFont::create("", "bigFont.fnt");
    label->setScale(0.26f);
    label->setPosition({x, y});
    m_mainLayer->addChild(label, 14);
  };

  auto addButton = [&](char const *text, cocos2d::CCPoint pos,
                       cocos2d::SEL_MenuHandler callback, int tag = 0,
                       float scale = 0.26f) {
    auto *sprite = ButtonSprite::create(text);
    sprite->setScale(scale);
    auto *button = CCMenuItemSpriteExtra::create(sprite, this, callback);
    button->setPosition(pos);
    button->setTag(tag);
    editorMenu->addChild(button);
    return button;
  };

  // ── INSPECTOR CONTENTS ───────────────────────────────────────────
  float panelLeftX = inspectorCenterX - inspectorWidth / 2.f + 12.f;
  float panelTopY = inspectorCenterY + kSidePanelHeight / 2.f - 16.f;
  addInspectorLabel("Selected", panelLeftX, panelTopY, 0.34f, kGold);

  float row1 = panelTopY - 28.f;
  float row2 = row1 - 28.f;
  float row3 = row2 - 28.f;
  float row4 = row3 - 28.f;
  addInspectorLabel("Frame", panelLeftX, row1);
  addInspectorLabel("Action", panelLeftX, row2);
  addInspectorLabel("Button", panelLeftX, row3);
  addInspectorLabel("Player", panelLeftX, row4);

  auto *frameBg = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
  frameBg->setContentSize({62.f, 24.f});
  frameBg->setColor(kValueBoxBg);
  frameBg->setOpacity(150);
  frameBg->setPosition({inspectorCenterX - 2.f, row1});
  m_mainLayer->addChild(frameBg, 13);

  frameInput = TextInput::create(66, "Frame", "chatFont.fnt");
  frameInput->setPosition({inspectorCenterX - 2.f, row1});
  frameInput->setScale(0.55f);
  frameInput->setString("0");
  frameInput->setFilter("0123456789");
  frameInput->setMaxCharCount(8);
  frameInput->setCallback(
      [this](std::string const& text) { onFrameInput(text); });
  m_mainLayer->addChild(frameInput, 14);

  addValueBox(inspectorCenterX - 2.f, row2, 70.f, actionValueLabel);
  addValueBox(inspectorCenterX - 2.f, row3, 70.f, buttonValueLabel);
  addValueBox(inspectorCenterX - 2.f, row4, 70.f, playerValueLabel);

  addButton("-10", {inspectorCenterX - 54.f, row1},
            menu_selector(MacroTimelineLayer::nudgeFrame), -10, 0.22f);
  addButton("+10", {inspectorCenterX + 52.f, row1},
            menu_selector(MacroTimelineLayer::nudgeFrame), 10, 0.22f);
  addButton("Swap", {inspectorCenterX + 56.f, row2},
            menu_selector(MacroTimelineLayer::switchAction), 0, 0.22f);
  addButton("Cycle", {inspectorCenterX + 56.f, row3},
            menu_selector(MacroTimelineLayer::switchButton), 0, 0.20f);
  addButton("Switch", {inspectorCenterX + 56.f, row4},
            menu_selector(MacroTimelineLayer::switchPlayer), 0, 0.20f);

  float controlsY = inspectorCenterY - kSidePanelHeight / 2.f + 22.f;
  addButton("Prev", {inspectorCenterX - 52.f, controlsY + 22.f},
            menu_selector(MacroTimelineLayer::onPrevInput), 0, 0.24f);
  addButton("Next", {inspectorCenterX - 4.f, controlsY + 22.f},
            menu_selector(MacroTimelineLayer::onNextInput), 0, 0.24f);
  addButton("Save", {inspectorCenterX + 50.f, controlsY + 22.f},
            menu_selector(MacroTimelineLayer::onSave), 0, 0.26f);
  addButton("Add", {inspectorCenterX - 34.f, controlsY - 2.f},
            menu_selector(MacroTimelineLayer::onAddInput), 0, 0.24f);
  addButton("Delete", {inspectorCenterX + 36.f, controlsY - 2.f},
            menu_selector(MacroTimelineLayer::onDeleteInput), 0, 0.24f);

  // ── CLOSE BUTTON ─────────────────────────────────────────────────
  auto *closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
  closeSpr->setScale(0.6f);
  auto *closeBtn = CCMenuItemSpriteExtra::create(
      closeSpr, this, menu_selector(MacroTimelineLayer::onClose));
  closeBtn->setPosition({inspectorCenterX + inspectorWidth / 2.f - 6.f,
                         inspectorCenterY + kSidePanelHeight / 2.f - 6.f});
  editorMenu->addChild(closeBtn);

  // ── FINISH ───────────────────────────────────────────────────────
  setCurrentFrame(inputs.empty() ? 0 : static_cast<int>(inputs.front().frame));
  refreshStatus();
  rebuildMarkers();
  return true;
}

// ══════════════════════════════════════════════════════════════════════
//  onClose()
// ══════════════════════════════════════════════════════════════════════
void MacroTimelineLayer::onClose(CCObject *) {
  if (!saved) {
    geode::createQuickPopup(
        "Exit Timeline", "<cr>Exit</c> without saving timeline changes?",
        "Cancel", "Exit", [this](auto, bool btn2) {
          if (!btn2)
            return;
#ifdef GEODE_IS_WINDOWS
          cocos2d::CCEGLView::sharedOpenGLView()->showCursor(
              cursorWasHidden ? false : true);
#endif
          if (!previewWasPlaying)
            Macro::resetState(true);
          this->setKeypadEnabled(false);
          this->setTouchEnabled(false);
          this->removeFromParentAndCleanup(true);
        });
    return;
  }
#ifdef GEODE_IS_WINDOWS
  cocos2d::CCEGLView::sharedOpenGLView()->showCursor(cursorWasHidden ? false
                                                                     : true);
#endif
  if (!previewWasPlaying)
    Macro::resetState(true);
  this->setKeypadEnabled(false);
  this->setTouchEnabled(false);
  this->removeFromParentAndCleanup(true);
}

// ══════════════════════════════════════════════════════════════════════
//  sortInputs()
// ══════════════════════════════════════════════════════════════════════
void MacroTimelineLayer::sortInputs() {
  std::sort(inputs.begin(), inputs.end(),
            [](input const &left, input const &right) {
              if (left.frame != right.frame)
                return left.frame < right.frame;
              if (left.player2 != right.player2)
                return left.player2 < right.player2;
              if (left.button != right.button)
                return left.button < right.button;
              return left.down > right.down;
            });
  maxFrame =
      std::max(1, inputs.empty() ? 1 : static_cast<int>(inputs.back().frame));
}

// ══════════════════════════════════════════════════════════════════════
//  markUnsaved()
// ══════════════════════════════════════════════════════════════════════
void MacroTimelineLayer::markUnsaved() {
  saved = false;
  refreshStatus();
  rebuildMarkers();
}

// ══════════════════════════════════════════════════════════════════════
//  refreshStatus()
// ══════════════════════════════════════════════════════════════════════
void MacroTimelineLayer::refreshStatus() {
  statusLabel->setString(
      fmt::format("Inputs: {}{}", inputs.size(), saved ? "" : "  *unsaved")
          .c_str());
}

// ══════════════════════════════════════════════════════════════════════
//  rebuildMarkers()  —  uses m_size coordinates (popup local space)
// ══════════════════════════════════════════════════════════════════════
void MacroTimelineLayer::rebuildMarkers() {
  if (!markerNode)
    return;
  markerNode->clear();

  // All coordinates are in popup local space: (0,0) to (m_size.width,
  // m_size.height)
  float W = m_size.width;
  float dockLeft = kEdgeMargin;
  float dockBottom = kEdgeMargin;
  float dockWidth = W - kEdgeMargin * 2.f;
  float inspectorWidth = kSidePanelWidth;
  float inspectorCenterX = dockLeft + dockWidth - inspectorWidth / 2.f - 12.f;
  float timelineLeft = dockLeft + 14.f;
  float timelineRight = inspectorCenterX - inspectorWidth / 2.f - 18.f;
  float timelineWidth = timelineRight - timelineLeft;
  float trackWidth = timelineWidth - 28.f;
  float trackStartX = timelineLeft + 14.f;
  float trackCenterY = dockBottom + kTrackY;
  float laneTopY = trackCenterY + kLaneGap / 2.f;
  float laneBottomY = trackCenterY - kLaneGap / 2.f;

  // Remove old tick labels
  for (int tag = 9000; tag <= 9050; ++tag) {
    if (auto *node = m_mainLayer->getChildByTag(tag))
      node->removeFromParentAndCleanup(true);
  }

  // Frame ruler ticks
  int tickSpacing = std::max(1, maxFrame / 10);
  int tagIndex = 0;
  for (int frame = 0; frame <= maxFrame; frame += tickSpacing) {
    float x = trackStartX + trackWidth * frameToRatio(frame, maxFrame);
    markerNode->drawSegment({x, trackCenterY + 46.f}, {x, trackCenterY + 58.f},
                            0.65f, ccc4f(1.f, 1.f, 1.f, 0.20f));
    auto *label =
        CCLabelBMFont::create(std::to_string(frame).c_str(), "chatFont.fnt");
    label->setScale(0.3f);
    label->setOpacity(110);
    label->setPosition({x, trackCenterY + 68.f});
    m_mainLayer->addChild(label, 6, 9000 + tagIndex++);
  }

  // Ruler baseline
  markerNode->drawSegment({trackStartX, trackCenterY + 46.f},
                          {trackStartX + trackWidth, trackCenterY + 46.f}, 0.8f,
                          ccc4f(1.f, 1.f, 1.f, 0.12f));

  // Input markers
  for (auto const &inp : inputs) {
    float x = trackStartX +
              trackWidth * frameToRatio(static_cast<int>(inp.frame), maxFrame);
    float laneY = inp.player2 ? laneBottomY : laneTopY;
    if (inp.down) {
      markerNode->drawSegment({x, laneY - kTrackHeight / 2.f + 2.f},
                              {x, laneY + kTrackHeight / 2.f - 2.f}, 1.6f,
                              ccc4f(0.18f, 1.f, 0.35f, 0.98f));
      markerNode->drawSegment({x - 2.f, laneY - kTrackHeight / 2.f + 2.f},
                              {x - 2.f, laneY + kTrackHeight / 2.f - 2.f}, 0.9f,
                              ccc4f(0.35f, 1.f, 0.50f, 0.35f));
    } else {
      markerNode->drawSegment({x, laneY - kTrackHeight / 2.f + 3.f},
                              {x, laneY + kTrackHeight / 2.f - 3.f}, 1.3f,
                              ccc4f(1.f, 0.30f, 0.30f, 0.98f));
      markerNode->drawSegment({x, laneY - kTrackHeight / 2.f - 9.f},
                              {x, laneY - kTrackHeight / 2.f - 1.f}, 0.9f,
                              ccc4f(1.f, 0.30f, 0.30f, 0.90f));
    }
  }

  // Highlight selected input
  if (selectedIndex >= 0 && selectedIndex < static_cast<int>(inputs.size())) {
    float x =
        trackStartX +
        trackWidth * frameToRatio(static_cast<int>(inputs[selectedIndex].frame),
                                  maxFrame);
    markerNode->drawSegment({x, laneBottomY - 22.f}, {x, laneTopY + 22.f}, 1.8f,
                            ccc4f(1.f, 0.85f, 0.18f, 0.94f));
  }

  // Playhead
  float playheadX =
      trackStartX + trackWidth * frameToRatio(currentFrame, maxFrame);
  markerNode->drawSegment({playheadX, laneBottomY - 26.f},
                          {playheadX, laneTopY + 26.f}, 1.5f,
                          ccc4f(1.f, 0.95f, 0.18f, 0.88f));
  markerNode->drawDot({playheadX, trackCenterY + 64.f}, 13.f,
                      ccc4f(1.f, 0.95f, 0.15f, 0.92f));
  markerNode->drawDot({playheadX, trackCenterY + 64.f}, 8.f,
                      ccc4f(1.f, 0.82f, 0.08f, 1.f));
}

// ══════════════════════════════════════════════════════════════════════
//  selectNearestInput()
// ══════════════════════════════════════════════════════════════════════
void MacroTimelineLayer::selectNearestInput() {
  if (inputs.empty()) {
    selectedIndex = -1;
    refreshSelectedInfo();
    return;
  }

  int bestIndex = 0;
  int bestDistance = std::abs(static_cast<int>(inputs[0].frame) - currentFrame);
  for (int index = 1; index < static_cast<int>(inputs.size()); ++index) {
    int distance =
        std::abs(static_cast<int>(inputs[index].frame) - currentFrame);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = index;
    }
  }

  selectedIndex = bestIndex;
  refreshSelectedInfo();
}

// ══════════════════════════════════════════════════════════════════════
//  refreshSelectedInfo()
// ══════════════════════════════════════════════════════════════════════
void MacroTimelineLayer::refreshSelectedInfo() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(inputs.size())) {
    selectedLabel->setString("No input selected");
    frameInput->setString(std::to_string(currentFrame).c_str());
    if (frameValueLabel)
      frameValueLabel->setString("—");
    if (actionValueLabel)
      actionValueLabel->setString("—");
    if (buttonValueLabel)
      buttonValueLabel->setString("—");
    if (playerValueLabel)
      playerValueLabel->setString("—");
    return;
  }

  auto const &inp = inputs[selectedIndex];
  selectedLabel->setString(
      fmt::format("#{} at {}", selectedIndex + 1, inp.frame).c_str());
  frameInput->setString(std::to_string(inp.frame).c_str());
  if (frameValueLabel)
    frameValueLabel->setString(std::to_string(inp.frame).c_str());
  if (actionValueLabel)
    actionValueLabel->setString((inp.down ? "Hold" : "Release"));
  if (buttonValueLabel)
    buttonValueLabel->setString(getButtonName(inp.button).c_str());
  if (playerValueLabel)
    playerValueLabel->setString(inp.player2 ? "Two" : "One");
}

// ══════════════════════════════════════════════════════════════════════
//  setCurrentFrame()
// ══════════════════════════════════════════════════════════════════════
void MacroTimelineLayer::setCurrentFrame(int frame, bool updateSlider) {
  currentFrame = std::clamp(frame, 0, maxFrame);
  frameLabel->setString(fmt::format("Frame: {}", currentFrame).c_str());
  if (timelineSlider && updateSlider)
    timelineSlider->setValue(frameToRatio(currentFrame, maxFrame));
  selectNearestInput();

  // Remove old frame-ruler labels & redraw everything
  for (int tag = 9000; tag <= 9050; ++tag) {
    if (auto *node = m_mainLayer->getChildByTag(tag))
      node->removeFromParentAndCleanup(true);
  }

  rebuildMarkers();
}

// ══════════════════════════════════════════════════════════════════════
//  onSlider()
// ══════════════════════════════════════════════════════════════════════
void MacroTimelineLayer::onSlider(CCObject *) {
  float value = timelineSlider->getValue();
  setCurrentFrame(static_cast<int>(std::round(value * maxFrame)), false);
}

// ══════════════════════════════════════════════════════════════════════
//  navigation / editing
// ══════════════════════════════════════════════════════════════════════
void MacroTimelineLayer::onPrevInput(CCObject *) {
  if (inputs.empty())
    return;
  if (selectedIndex <= 0)
    selectedIndex = static_cast<int>(inputs.size()) - 1;
  else
    selectedIndex--;
  setCurrentFrame(static_cast<int>(inputs[selectedIndex].frame));
}

void MacroTimelineLayer::onNextInput(CCObject *) {
  if (inputs.empty())
    return;
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(inputs.size()) - 1)
    selectedIndex = 0;
  else
    selectedIndex++;
  setCurrentFrame(static_cast<int>(inputs[selectedIndex].frame));
}

void MacroTimelineLayer::onDeleteInput(CCObject *) {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(inputs.size()))
    return;
  inputs.erase(inputs.begin() + selectedIndex);
  if (selectedIndex >= static_cast<int>(inputs.size()))
    selectedIndex = static_cast<int>(inputs.size()) - 1;
  sortInputs();
  markUnsaved();
  setCurrentFrame(currentFrame);
}

void MacroTimelineLayer::onAddInput(CCObject *) {
  input inp(currentFrame, 1, false, true);
  inputs.push_back(inp);
  sortInputs();
  selectNearestInput();
  markUnsaved();
  setCurrentFrame(currentFrame);
}

void MacroTimelineLayer::onSave(CCObject *) {
  auto &g = Global::get();
  sortInputs();
  g.macro.inputs = inputs;
  g.macro.lastRecordedFrame =
      inputs.empty() ? 0 : static_cast<int>(inputs.back().frame);
  originalInputs = inputs;
  saved = true;
  refreshStatus();
  Notification::create("Timeline saved", NotificationIcon::Success)->show();
}

void MacroTimelineLayer::switchButton(CCObject *) {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(inputs.size()))
    return;
  auto &inp = inputs[selectedIndex];
  inp.button = inp.button == 3 ? 1 : inp.button + 1;
  markUnsaved();
  refreshSelectedInfo();
  rebuildMarkers();
}

void MacroTimelineLayer::switchAction(CCObject *) {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(inputs.size()))
    return;
  inputs[selectedIndex].down = !inputs[selectedIndex].down;
  markUnsaved();
  refreshSelectedInfo();
  rebuildMarkers();
}

void MacroTimelineLayer::switchPlayer(CCObject *) {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(inputs.size()))
    return;
  inputs[selectedIndex].player2 = !inputs[selectedIndex].player2;
  markUnsaved();
  refreshSelectedInfo();
  rebuildMarkers();
}

void MacroTimelineLayer::nudgeFrame(CCObject *obj) {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(inputs.size()))
    return;
  auto *node = static_cast<CCNode *>(obj);
  int delta = node->getTag();
  inputs[selectedIndex].frame =
      std::max(0, static_cast<int>(inputs[selectedIndex].frame) + delta);
  sortInputs();
  markUnsaved();
  setCurrentFrame(static_cast<int>(inputs[selectedIndex].frame));
}

void MacroTimelineLayer::onFrameInput(std::string const& text) {
  if (!frameInput)
    return;
  if (text.empty())
    return;
  int frame = geode::utils::numFromString<int>(text).unwrapOr(currentFrame);
  if (selectedIndex >= 0 && selectedIndex < static_cast<int>(inputs.size())) {
    inputs[selectedIndex].frame = std::max(0, frame);
    sortInputs();
    markUnsaved();
  }
  setCurrentFrame(frame);
}
