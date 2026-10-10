#include "load_macro_layer.hpp"
#include "autosave_settings_layer.hpp"
#include "macro_editor.hpp"
#include "rgb_effect.hpp"

#ifdef _MSC_VER
#pragma optimize("", off)
#endif

#include <Geode/modify/CCMenu.hpp>

class $modify(CCMenu) {
  virtual bool ccTouchBegan(cocos2d::CCTouch *touch, cocos2d::CCEvent *event) {
    CCScene *scene = CCDirector::sharedDirector()->getRunningScene();
    LoadMacroLayer *layer = scene->getChildByType<LoadMacroLayer>(0);

    if (!layer)
      return CCMenu::ccTouchBegan(touch, event);

    cocos2d::CCPoint pos = touch->getLocation();
    float yCenter = CCDirector::sharedDirector()->getWinSize().height / 2.f;

    if (pos.y > yCenter - 100)
      return CCMenu::ccTouchBegan(touch, event);

    for (MacroCell *cell : layer->allMacros) {
      if (cell->menu == this)
        return false;
    }

    return CCMenu::ccTouchBegan(touch, event);
  }
};

void LoadMacroLayer::open(geode::Popup *layer, geode::Popup *layer2,
                          bool autosaves) {
  std::filesystem::path path =
      Mod::get()->getSettingValue<std::filesystem::path>("macros_folder");

  if (!std::filesystem::exists(path)) {
    if (utils::file::createDirectoryAll(path).isErr())
      return FLAlertLayer::create(
                 "Error", "There was an error getting the folder. ID: 6", "Ok")
          ->show();
  }

  path = Mod::get()->getSettingValue<std::filesystem::path>("autosaves_folder");

  if (!std::filesystem::exists(path)) {
    if (utils::file::createDirectoryAll(path).isErr())
      return FLAlertLayer::create(
                 "Error", "There was an error getting the folder. ID: 61", "Ok")
          ->show();
  }

  LoadMacroLayer *layerReal = create(layer, layer2, autosaves);
  layerReal->m_noElasticity = true;
  layerReal->show();
}

void LoadMacroLayer::textChanged(CCTextInputNode *node) {
  search = Utils::toLower(node->getString());
  if (search != "") {
    searchOff->setVisible(true);
    searchOff->setOpacity(184);
  } else
    searchOff->setVisible(false);

  reloadList(0);
}

void LoadMacroLayer::reloadList(int amount) {
  if (CCNode *scrollbar = m_buttonMenu->getChildByID("scrollbar"))
    scrollbar->removeFromParentAndCleanup(true);

  if (CCNode *lbl = menu->getChildByID("no-macros-label"))
    lbl->removeFromParentAndCleanup(true);

  CCNode *listLayer = m_buttonMenu->getChildByID("list-layer");
  if (!listLayer)
    return;

  ListView *listView = listLayer->getChildByType<ListView>(0);

  CCLayer *contentLayer = nullptr;
  contentLayer = typeinfo_cast<CCLayer *>(
      listView->m_tableView->getChildren()->objectAtIndex(0));

  int childrenCount = 0;
  float posY = 0.f;
  if (contentLayer) {
    if (CCArray *children = contentLayer->getChildren())
      childrenCount = children->count();

    posY = contentLayer->getPositionY();
  }
  listLayer->removeFromParentAndCleanup(true);
  if (CCNode *bg = m_buttonMenu->getChildByID("background"))
    bg->removeFromParentAndCleanup(true);

  selectedMacros.clear();
  allMacros.clear();

  if (!isMerge)
    selectAllToggle->toggle(false);

  addList(childrenCount > 7 && amount != 0, posY + (35.f * amount));
}

void LoadMacroLayer::deleteSelected(CCObject *) {
  int amount = selectedMacros.size();
  if (amount < 1)
    return;

  geode::createQuickPopup(
      "Warning",
      "Are you sure you want to <cr>delete</c> <cy>" + std::to_string(amount) +
          "</c> " + (isAutosaves ? "autosave" : "macro") + "(s)?",
      "Cancel", "Yes", [this, amount](auto, bool btn2) {
        if (btn2) {
          for (size_t i = 0; i < this->selectedMacros.size(); i++)
            this->selectedMacros[i]->deleteMacro(false);

          this->reloadList(amount);
          Notification::create("Macros Deleted", NotificationIcon::Success)
              ->show();
        }
      });
}

void LoadMacroLayer::onSelectAll(CCObject *obj) {
  bool on = !static_cast<CCMenuItemToggler *>(obj)->isToggled();

  for (size_t i = 0; i < allMacros.size(); i++) {
    CCMenuItemToggler *toggle = allMacros[i]->toggler;
    if (toggle->isToggled() == on)
      continue;

    toggle->toggle(on);
    allMacros[i]->selectMacro(false);
  }
}

LoadMacroLayer *LoadMacroLayer::create(geode::Popup *layer,
                                       geode::Popup *layer2, bool autosaves) {
  LoadMacroLayer *ret = new LoadMacroLayer();
  if (ret->init(385, 291, Utils::getTexture().c_str())) {
    ret->menuLayer = layer;
    ret->mergeLayer = layer2;
    ret->isAutosaves = autosaves;
    ret->setup();
    ret->autorelease();
    return ret;
  }

  delete ret;
  return nullptr;
}

void LoadMacroLayer::onImportMacroFinished(file::PickResult res) {
  if (res.isOk() && res.unwrap().has_value()) {
    std::filesystem::path path = res.unwrap().value();

    auto &g = Global::get();
    Macro tempMacro;
    std::vector<std::uint8_t> macroData;
    bool gdr2Macro = false;

    if (path.extension() == ".xd") {
      tempMacro = Macro::XDtoGDR(path);

      if (tempMacro.description == "fail") {
        FLAlertLayer::create(
            "Error", "There was an error importing this macro. ID: 46", "Ok")
            ->show();
        return;
      }
    } else {

      std::ifstream f(path, std::ios::binary);

      f.seekg(0, std::ios::end);
      size_t fileSize = f.tellg();
      f.seekg(0, std::ios::beg);

      macroData.resize(fileSize);

      f.read(reinterpret_cast<char *>(macroData.data()), fileSize);
      f.close();

      if (Macro::isGDR2Data(macroData)) {
        gdr2Macro = true;
        auto imported = Macro::importGDR2(macroData);
        if (!imported.has_value()) {
          FLAlertLayer::create(
              "Error", "There was an error importing this macro. ID: 48", "Ok")
              ->show();
          return;
        }

        tempMacro = std::move(imported.value());
      } else {
        tempMacro = Macro::importData(macroData);
      }
    }

    bool xdMacro = path.extension() == ".xd";

    int iterations = 0;

    std::string name = path.filename().string().substr(
        0, path.filename().string().find_last_of('.'));

    std::filesystem::path newPath =
        Mod::get()->getSettingValue<std::filesystem::path>("macros_folder") /
        name;

    std::filesystem::path finalPath =
        Mod::get()->getSettingValue<std::filesystem::path>("macros_folder") /
        name;

    iterations = 0;
    std::string finalExtension = gdr2Macro ? ".gdr2" : ".gdr.json";
    while (std::filesystem::exists(finalPath.string() + finalExtension)) {
      iterations++;
      std::string suffix = " (" + std::to_string(iterations) + ")";
      finalPath =
          finalPath.parent_path() / (finalPath.stem().string() + suffix);
    }

    finalPath += finalExtension;

    std::ofstream f2(finalPath, std::ios::binary);
    if (!f2.is_open()) {
      FLAlertLayer::create("Error", "Failed to open file for writing. ID: 47",
                           "Ok")
          ->show();
      return;
    }

    if (gdr2Macro) {
      f2.write(reinterpret_cast<const char *>(macroData.data()), macroData.size());
    } else {
      auto data = tempMacro.exportData(true);
      f2.write(reinterpret_cast<const char *>(data.data()), data.size());
    }
    f2.close();

    this->reloadList(0);

    if (xdMacro)
      FLAlertLayer::create("Warning",
                           "<cl>.xd</c> extension macros may not function "
                           "correctly in this version.",
                           "Ok")
          ->show();

    Notification::create("Macro Imported", NotificationIcon::Success)->show();
  }
}

void LoadMacroLayer::onImportMacro(CCObject *) {
  file::FilePickOptions::Filter textFilter;
  file::FilePickOptions fileOptions;
  textFilter.description = "Macro Files";
  textFilter.files = {"*.gdr", "*.gdr2", "*.xd", "*.json"};
  fileOptions.filters.push_back(textFilter);

  geode::async::spawn(
      file::pick(file::PickMode::OpenFile, {dirs::getGameDir(), {textFilter}}),
      [this](file::PickResult res) {
        this->onImportMacroFinished(std::move(res));
      });
}

bool LoadMacroLayer::setup() {

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
  invertSort = true;
#endif

  menu = CCMenu::create();
  menu->setZOrder(110);
  m_mainLayer->addChild(menu);

  // Utils::setBackgroundColor(m_bgSprite);

  // menuLayer and mergeLayer are set in create()
  isMerge = mergeLayer != nullptr;

  setTitle(isMerge ? "Merge Macro" : "Load Macro");
  m_title->setPositionY(m_title->getPositionY() + 5);
  m_closeBtn->getNormalImage()->setScale(0.6f);

  cocos2d::CCPoint offset = (CCDirector::sharedDirector()->getWinSize() -
                             m_mainLayer->getContentSize()) /
                            2;
  m_mainLayer->setPosition(m_mainLayer->getPosition() - offset);
  m_bgSprite->setPosition(m_bgSprite->getPosition() + offset);
  m_closeBtn->setPosition(m_closeBtn->getPosition() + offset);
  m_title->setPosition(m_title->getPosition() + offset);

  if (!isMerge) {
    CCSprite *icon = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
    icon->setScale(0.585f);
    CCMenuItemSpriteExtra *btn = CCMenuItemSpriteExtra::create(
        icon, this, menu_selector(LoadMacroLayer::onImportMacro));
    btn->setPosition(ccp(165, -121));

    menu->addChild(btn);

    searchInput = TextInput::create(235, "Search Macro", "bigFont.fnt");
    searchInput->setPositionY(100);
    searchInput->setDelegate(this);
    menu->addChild(searchInput);

    CCSprite *emptyBtn =
        CCSprite::createWithSpriteFrameName("GJ_plainBtn_001.png");
    emptyBtn->setScale(0.585f);
    CCSprite *folderIcon =
        CCSprite::createWithSpriteFrameName("folderIcon_001.png");
    folderIcon->setPosition(emptyBtn->getContentSize() / 2);
    folderIcon->setScale(0.7f);
    emptyBtn->addChild(folderIcon);
    btn = CCMenuItemSpriteExtra::create(
        emptyBtn, this, menu_selector(LoadMacroLayer::openFolder));
    btn->setPosition(ccp(115, -121));

    menu->addChild(btn);

    CCSprite *spr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
    spr->setScale(0.585f);
    btn = CCMenuItemSpriteExtra::create(
        spr, this, menu_selector(LoadMacroLayer::deleteSelected));
    btn->setPosition(ccp(65, -121));

    menu->addChild(btn);

    if (isAutosaves) {
      spr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
      spr->setScale(0.55f);
      btn = CCMenuItemSpriteExtra::create(spr, this,
                                          menu_selector(AutoSaveLayer::open));
      btn->setPosition(ccp(15, -121));

      menu->addChild(btn);
    }
  }

  CCSprite *spr1 = CCSprite::create("GJ_button_01.png");
  CCSprite *spr2 = CCSprite::createWithSpriteFrameName("GJ_sortIcon_001.png");
  spr2->setPosition({20, 20});
  spr1->addChild(spr2);

  CCSprite *spr3 = CCSprite::create("GJ_button_02.png");
  CCSprite *spr4 = CCSprite::createWithSpriteFrameName("GJ_sortIcon_001.png");
  spr4->setPosition({20, 20});
  spr3->addChild(spr4);

  sortToggle = CCMenuItemToggler::create(
      spr1, spr3, this, menu_selector(LoadMacroLayer::updateSort));
  sortToggle->setPosition({-145, 100});
  sortToggle->setScale(0.55f);
  sortToggle->toggle(false);
  menu->addChild(sortToggle);

  CCSprite *spriteOn =
      CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
  CCSprite *spriteOff =
      CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");

  selectAllToggle = CCMenuItemToggler::create(
      spriteOff, spriteOn, this, menu_selector(LoadMacroLayer::onSelectAll));
  selectAllToggle->setScale(0.585f);
  selectAllToggle->setPosition({-165, -121});

  if (!isMerge)
    menu->addChild(selectAllToggle);

  CCLabelBMFont *lbl = CCLabelBMFont::create("Select all", "bigFont.fnt");
  lbl->setScale(0.4f);
  lbl->setPosition({-110, -121});

  if (!isMerge)
    menu->addChild(lbl);

  CCSprite *spr = CCSprite::createWithSpriteFrameName("gj_findBtnOff_001.png");
  spr->setScale(0.685f);
  searchOff = CCMenuItemSpriteExtra::create(
      spr, this, menu_selector(LoadMacroLayer::clearSearch));
  searchOff->setPosition(ccp(137, 100));
  searchOff->setVisible(false);
  menu->addChild(searchOff);

  macroCountLbl = CCLabelBMFont::create("13 Macros", "chatFont.fnt");
  macroCountLbl->setOpacity(108);
  macroCountLbl->setScale(0.55f);
  macroCountLbl->setAnchorPoint({1.f, 0.5f});
  macroCountLbl->setPosition({180, 130});
  menu->addChild(macroCountLbl);

  if (isMerge) {
    p1Toggle = CCMenuItemToggler::create(spriteOff, spriteOn, this, nullptr);
    p1Toggle->setID("p1-toggle");
    p1Toggle->setScale(0.675f);
    p1Toggle->setPosition({-23, -121});
    menu->addChild(p1Toggle);

    p2Toggle = CCMenuItemToggler::create(spriteOff, spriteOn, this, nullptr);
    p2Toggle->setID("p2-toggle");
    p2Toggle->setScale(0.675f);
    p2Toggle->setPosition({98, -121});
    menu->addChild(p2Toggle);

    owToggle = CCMenuItemToggler::create(spriteOff, spriteOn, this, nullptr);
    owToggle->setID("ow-toggle");
    owToggle->setScale(0.675f);
    owToggle->setPosition({-166, -121});
    owToggle->toggle(true);
    menu->addChild(owToggle);

    lbl = CCLabelBMFont::create("Overwrite", "bigFont.fnt");
    lbl->setPosition({-111, -121});
    lbl->setScale(0.44f);
    menu->addChild(lbl);

    lbl = CCLabelBMFont::create("P1 only", "bigFont.fnt");
    lbl->setPosition({21, -121});
    lbl->setScale(0.44f);
    menu->addChild(lbl);

    lbl = CCLabelBMFont::create("P2 only", "bigFont.fnt");
    lbl->setPosition({144, -121});
    lbl->setScale(0.44f);
    menu->addChild(lbl);
  }

  addList();

  return true;
}

void LoadMacroLayer::clearSearch(CCObject *) {
  searchOff->setVisible(false);
  searchInput->setString("");
  search = "";

  reloadList(0);
}

void LoadMacroLayer::updateSort(CCObject *) {
  if (!sortToggle)
    return;

  invertSort = !sortToggle->isToggled();

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
  invertSort = !invertSort;
#endif

  reloadList(0);
}

void LoadMacroLayer::addList(bool refresh, float prevScroll) {
  cocos2d::CCSize winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();

  std::filesystem::path path =
      Mod::get()->getSettingValue<std::filesystem::path>(
          isAutosaves ? "autosaves_folder" : "macros_folder");
  std::vector<std::filesystem::path> macros =
      file::readDirectory(path).unwrapOrDefault();

  CCArray *cells = CCArray::create();

  int start = invertSort ? macros.size() - 1 : 0;
  int end = invertSort ? -1 : macros.size();
  int step = invertSort ? -1 : 1;

  for (int i = start; i != end; i += step) {

   if (macros[i].extension() != ".gdr" && macros[i].extension() != ".gdr2" &&
    macros[i].extension() != ".xd" && macros[i].extension() != ".json" &&
    macros[i].extension() != ".slc2" && macros[i].extension() != ".slc3")
  continue;

    std::string name = macros[i].filename().string().substr(
        0, macros[i].filename().string().find_last_of('.'));

    if (macros[i].extension() == ".json")
      name = name.substr(0, name.find_last_of('.'));

    if (Utils::toLower(name).find(search) == std::string::npos && search != "")
      continue;

    std::time_t date;

#ifdef GEODE_IS_WINDOWS
    date = Utils::getFileCreationTime(macros[i]);
#endif

    MacroCell *cell =
        MacroCell::create(macros[i], name, date, menuLayer, mergeLayer,
                          static_cast<CCLayer *>(this));
    cells->addObject(cell);
  }

  std::string countStr = std::to_string(cells->count()) + " Macros";
  macroCountLbl->setString(countStr.c_str());

  if (cells->count() == 0) {
    CCLabelBMFont *lbl = CCLabelBMFont::create(
        isAutosaves ? "No Autosaves" : "No Macros", "bigFont.fnt");
    lbl->setPosition(winSize / 2);
    lbl->setScale(0.5f);
    lbl->setOpacity(100);
    lbl->setID("no-macros-label");
    menu->addChild(lbl);
  }

  ListView *listView = ListView::create(cells, 35, 323, 180);
  CCNode *contentLayer = static_cast<CCNode *>(
      listView->m_tableView->getChildren()->objectAtIndex(0));

  if (refresh)
    contentLayer->setPositionY(prevScroll);

  cocos2d::ccColor3B color =
      Mod::get()->getSettingValue<cocos2d::ccColor3B>("background_color");

  CCArray *children = contentLayer->getChildren();
  CCObject *child;
  int it = 0;

  cocos2d::ccColor3B color1 =
      ccc3(std::max(0, color.r - 70), std::max(0, color.g - 70),
           std::max(0, color.b - 70));
  cocos2d::ccColor3B color2 =
      ccc3(std::max(0, color.r - 55), std::max(0, color.g - 55),
           std::max(0, color.b - 55));

  for (auto child : CCArrayExt<CCObject *>(children)) {
    if (GenericListCell *cell = typeinfo_cast<GenericListCell *>(child)) {
      allMacros.push_back(
          static_cast<MacroCell *>(cell->getChildren()->objectAtIndex(2)));

      cocos2d::ccColor3B col = (it % 2 == 0) ? color1 : color2;
      it++;
      cell->m_backgroundLayer->setColor(col);
    }
  }

  GJCommentListLayer *listLayer = GJCommentListLayer::create(
      listView, "Custom Labels", ccc4(255, 255, 255, 0), 323, 180, true);
  listLayer->setPosition((winSize / 2) - (listLayer->getContentSize() / 2) -
                         CCPoint((it >= 5) ? 6 : 0, 0) + ccp(0, 1));
  listLayer->setZOrder(1);
  listLayer->setID("list-layer");
  listView->setPositionY(-12);
  m_buttonMenu->addChild(listLayer);

  listLayer->setUserObject("dont-correct-borders",
                           cocos2d::CCBool::create(true));

  CCSprite *topBorder = listLayer->getChildByType<CCSprite>(1);
  CCSprite *bottomBorder = listLayer->getChildByType<CCSprite>(0);
  CCSprite *rightBorder = listLayer->getChildByType<CCSprite>(3);
  CCSprite *leftBorder = listLayer->getChildByType<CCSprite>(2);

  if (color != ccc3(51, 68, 153)) {
    CCSprite *topSprite = CCSprite::create("GJ_commentTop2_001_White.png"_spr);
    CCSprite *bottomSprite =
        CCSprite::create("GJ_commentTop2_001_White.png"_spr);
    CCSprite *rightSprite =
        CCSprite::create("GJ_commentSide2_001_White.png"_spr);
    CCSprite *leftSprite =
        CCSprite::create("GJ_commentSide2_001_White.png"_spr);
    rightSprite->setScaleX(-1);
    bottomSprite->setScaleY(-1);

    topSprite->setColor(color);
    bottomSprite->setColor(color);
    rightSprite->setColor(color);
    leftSprite->setColor(color);

    topSprite->setAnchorPoint({0, 0});
    bottomSprite->setAnchorPoint({0, 1});
    rightSprite->setAnchorPoint({1, 0});
    leftSprite->setAnchorPoint({0, 0});

    topBorder->addChild(topSprite);
    bottomBorder->addChild(bottomSprite);
    rightBorder->addChild(rightSprite);
    leftBorder->addChild(leftSprite);
  }

  topBorder->setScaleX(0.945f);
  topBorder->setScaleY(1.f);
  topBorder->setPosition(ccp(161.25, 162.f));

  bottomBorder->setScaleX(0.945f);
  bottomBorder->setScaleY(1.f);
  bottomBorder->setPosition({161.25, -7.f});

  rightBorder->setScaleX(0.8f);
  rightBorder->setScaleY(5.9f);
  rightBorder->setPosition({328, -12});

  leftBorder->setScaleX(0.8f);
  leftBorder->setScaleY(5.6f);
  leftBorder->setPosition({-5.45, -1});

  CCScale9Sprite *listBackground =
      CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
  listBackground->setScale(0.7f);
  listBackground->setColor({0, 0, 0});
  listBackground->setOpacity(75);
  listBackground->setPosition(winSize / 2 +
                              ccp(-0.11f - (it >= 5 ? 6 : 0), -10.5f));
  listBackground->setContentSize({461.1f, 255.1f});
  listBackground->setID("background");
  m_buttonMenu->addChild(listBackground);

  if (it >= 5) {
    Scrollbar *scrollbar = Scrollbar::create(listView->m_tableView);
    scrollbar->setPosition({(winSize.width / 2) +
                                (listLayer->getScaledContentSize().width / 2) +
                                4,
                            winSize.height / 2});
    scrollbar->setID("scrollbar");
  m_buttonMenu->addChild(scrollbar);
}

bool rgbEnabled = Mod::get()->getSettingValue<bool>("rgb_ui");

if (rgbEnabled) {
  // Xóa RGB effect cũ nếu addList() được gọi lại
  if (CCNode* oldFx = m_buttonMenu->getChildByID("rgb-effect"))
    oldFx->removeFromParentAndCleanup(true);

  std::vector<cocos2d::CCNode*> rgbTargets = {
    topBorder,
    bottomBorder,
    leftBorder,
    rightBorder,
    listBackground
  };

  RGBEffect* fx = RGBEffect::create(
    rgbTargets,
    60.f
  );

  fx->setID("rgb-effect");
  m_buttonMenu->addChild(fx);
} else {
  topBorder->setColor(color);
  bottomBorder->setColor(color);
  leftBorder->setColor(color);
  rightBorder->setColor(color);
}

}
