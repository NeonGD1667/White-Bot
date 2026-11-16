#pragma once

#include "../pathfinder.hpp"

#include <algorithm>

class PathFinderSettingsLayer : public geode::Popup {
private:
  TextInput *stepInput = nullptr;
  TextInput *shiftInput = nullptr;
  TextInput *actionsInput = nullptr;
  TextInput *warmupInput = nullptr;
  TextInput *startFrameInput = nullptr;
  TextInput *searchWindowInput = nullptr;
  CCMenuItemToggler *consoleToggle = nullptr;

  STATIC_CREATE(PathFinderSettingsLayer, 230, 260)

  void textChanged(CCTextInputNode *) override {
    auto *mod = Mod::get();

    auto step = geode::utils::numFromString<int>(stepInput->getString()).unwrapOr(4);
    auto shifts =
        geode::utils::numFromString<int>(shiftInput->getString()).unwrapOr(6);
    auto actions =
        geode::utils::numFromString<int>(actionsInput->getString()).unwrapOr(8);
    auto warmup =
        geode::utils::numFromString<int>(warmupInput->getString()).unwrapOr(12);
    auto startFrame =
        geode::utils::numFromString<int>(startFrameInput->getString()).unwrapOr(6);
    auto searchWindow = geode::utils::numFromString<int>(
                            searchWindowInput->getString())
                            .unwrapOr(120);

    mod->setSavedValue("pathfinder_backtrack_step", std::clamp(step, 1, 60));
    mod->setSavedValue("pathfinder_max_shifts", std::clamp(shifts, 1, 32));
    mod->setSavedValue("pathfinder_actions_back", std::clamp(actions, 1, 64));
    mod->setSavedValue("pathfinder_warmup_frames", std::clamp(warmup, 0, 120));
    mod->setSavedValue("pathfinder_min_action_frame",
                       std::clamp(startFrame, 1, 120));
    mod->setSavedValue("pathfinder_search_window",
                       std::clamp(searchWindow, 15, 600));
    PathFinder::loadSettings();
  }

  void onToggle(CCObject *) {
#ifdef GEODE_IS_WINDOWS
    Mod::get()->setSavedValue("pathfinder_console_logs", !consoleToggle->isToggled());
    PathFinder::loadSettings();
#endif
  }

  bool setup() {
    setTitle("Path Finder");
    m_title->setScale(0.625f);
    m_title->setPositionY(242);

    auto addLabel = [&](char const *text, cocos2d::CCPoint pos) {
      auto *label = CCLabelBMFont::create(text, "bigFont.fnt");
      label->setPosition(pos);
      label->setScale(0.3f);
      m_mainLayer->addChild(label);
    };

    auto addInput = [&](TextInput *&input, char const *placeholder,
                        std::string const &value, cocos2d::CCPoint pos,
                        int maxLength) {
      input = TextInput::create(70, placeholder, "chatFont.fnt");
      input->setPosition(pos);
      input->setString(value.c_str());
      input->getInputNode()->setDelegate(this);
      input->getInputNode()->setAllowedChars("0123456789");
      input->getInputNode()->setMaxLabelLength(maxLength);
      m_mainLayer->addChild(input);
    };

    addLabel("Backtrack Step", {m_size.width / 2, 214});
    addInput(stepInput, "Step",
             std::to_string(Mod::get()->getSavedValue<int64_t>(
                 "pathfinder_backtrack_step", 4)),
             {m_size.width / 2, 194}, 2);

    addLabel("Max Shifts / Action", {m_size.width / 2, 170});
    addInput(shiftInput, "Shifts",
             std::to_string(
                 Mod::get()->getSavedValue<int64_t>("pathfinder_max_shifts", 6)),
             {m_size.width / 2, 150}, 2);

    addLabel("Actions Back", {m_size.width / 2, 126});
    addInput(actionsInput, "Actions",
             std::to_string(
                 Mod::get()->getSavedValue<int64_t>("pathfinder_actions_back", 8)),
             {m_size.width / 2, 106}, 2);

    addLabel("Warmup Frames", {m_size.width / 2, 82});
    addInput(warmupInput, "Warmup",
             std::to_string(Mod::get()->getSavedValue<int64_t>(
                 "pathfinder_warmup_frames", 12)),
             {m_size.width / 2, 62}, 3);

    addLabel("Min Action Frame", {m_size.width / 2, 38});
    addInput(startFrameInput, "Start",
             std::to_string(Mod::get()->getSavedValue<int64_t>(
                 "pathfinder_min_action_frame", 6)),
             {m_size.width / 2, 18}, 3);

    addLabel("Search Window", {m_size.width / 2, -6});
    addInput(searchWindowInput, "Window",
             std::to_string(Mod::get()->getSavedValue<int64_t>(
                 "pathfinder_search_window", 120)),
             {m_size.width / 2, -26}, 3);

#ifdef GEODE_IS_WINDOWS
    addLabel("Console Logs", {68, 234});
    consoleToggle = CCMenuItemToggler::create(
        CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"),
        CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"), this,
        menu_selector(PathFinderSettingsLayer::onToggle));
    consoleToggle->setPosition({24, 234});
    consoleToggle->setScale(0.6f);
    consoleToggle->toggle(
        Mod::get()->getSavedValue<bool>("pathfinder_console_logs", true));
    m_buttonMenu->addChild(consoleToggle);
#endif

    return true;
  }

public:
  void open(CCObject *) { create()->show(); }
};
