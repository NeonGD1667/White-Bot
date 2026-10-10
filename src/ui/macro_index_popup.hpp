#pragma once

// Macro Index popup — dựng theo đúng layout của LoadMacroLayer:
//   - ô search + nút clear + nút sort + nhãn "N Macros" ở trên
//   - ListView (GJCommentListLayer) có viền, nền tối, hàng xen kẽ màu, scrollbar
//   - hàng dưới: ô Index URL + Apply + Refresh
//
// Chỉ include file này từ record_layer.cpp.

#include "../includes.hpp"
#include "rgb_effect.hpp"

#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

// Menu của mỗi hàng: chỉ nhận chạm khi điểm chạm nằm trong vùng nhìn thấy của
// list. Nếu không, hàng đã cuộn ra ngoài (bị cắt) vẫn bấm xuyên qua được.
class IndexRowMenu : public CCMenu {
public:
  CCRect clipRect = CCRectZero; // world space
  bool hasClip = false;

  static IndexRowMenu *create() {
    auto ret = new IndexRowMenu();

    if (ret->init()) {
      ret->autorelease();
      return ret;
    }

    delete ret;
    return nullptr;
  }

  bool ccTouchBegan(CCTouch *touch, CCEvent *event) override {
    if (hasClip && !clipRect.containsPoint(touch->getLocation()))
      return false;

    return CCMenu::ccTouchBegan(touch, event);
  }
};

class MacroIndexPopup : public geode::Popup {
private:
  struct MacroEntry {
    std::string levelName;
    std::string lowerName;
    int levelId = 0;
    std::string difficulty;
    std::string uploader;
    float rating = 0.f;
    std::string format;
    std::string downloadUrl;
  };

  static constexpr float LIST_WIDTH = 323.f;
  static constexpr float LIST_HEIGHT = 180.f;
  static constexpr float CELL_HEIGHT = 35.f;
  static constexpr size_t MAX_ROWS = 300;

  CCMenu *menu = nullptr;
  TextInput *searchInput = nullptr;
  TextInput *hostInput = nullptr;
  CCMenuItemSpriteExtra *searchOff = nullptr;
  CCMenuItemToggler *sortToggle = nullptr;
  CCLabelBMFont *countLabel = nullptr;
  CCLabelBMFont *messageLabel = nullptr;

  std::vector<MacroEntry> allEntries;
  std::vector<MacroEntry> filteredEntries;
  std::vector<IndexRowMenu *> rowMenus; // chỉ hợp lệ giữa 2 lần rebuildList()

  std::string search;
  std::string errorText;
  bool loading = false;
  bool invertSort = false;
  bool downloading = false;

  Ref<Notification> downloadNotification;

  geode::async::TaskHolder<geode::utils::web::WebResponse> indexTask;
  geode::async::TaskHolder<geode::utils::web::WebResponse> downloadTask;

  // ========================================================
  // Helpers
  // ========================================================

  static std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });

    return value;
  }

  // Cắt chuỗi UTF-8 an toàn (không cắt giữa 1 ký tự nhiều byte).
  static std::string truncate(std::string const &text, size_t maxBytes) {
    if (text.size() <= maxBytes)
      return text;

    size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
      --n;

    return text.substr(0, n) + "...";
  }

  static std::string sanitizeFilename(std::string name) {
    constexpr char invalid[] = "<>:\"/\\|?*";

    for (char &c : name) {
      if (std::find(std::begin(invalid), std::end(invalid) - 1, c) !=
          std::end(invalid) - 1) {
        c = '_';
      }
    }

    while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
      name.pop_back();

    if (name.empty())
      name = "macro";

    return name;
  }

  static std::string getExtension(std::string format) {
    format = toLower(format);

    if (!format.empty() && format.front() == '.')
      format.erase(format.begin());

    // Chỉ cho phép các định dạng macro đã hỗ trợ.
    if (format == "gdr")
      return ".gdr";

    if (format == "gdr2")
      return ".gdr2";

    if (format == "json" || format == "gdr.json")
      return ".gdr.json";

    if (format == "xd")
      return ".xd";

    if (format == "slc2")
      return ".slc2";

    if (format == "slc3")
      return ".slc3";

    return ".gdr2";
  }

  // ========================================================
  // UI (cùng bố cục với LoadMacroLayer::setup)
  // ========================================================

  bool setup() {
    menu = CCMenu::create();
    menu->setZOrder(110);
    m_mainLayer->addChild(menu);

    setTitle("Macro Index");
    m_title->setPositionY(m_title->getPositionY() + 5);
    m_closeBtn->getNormalImage()->setScale(0.6f);

    cocos2d::CCPoint offset = (CCDirector::sharedDirector()->getWinSize() -
                               m_mainLayer->getContentSize()) /
                              2;
    m_mainLayer->setPosition(m_mainLayer->getPosition() - offset);
    m_bgSprite->setPosition(m_bgSprite->getPosition() + offset);
    m_closeBtn->setPosition(m_closeBtn->getPosition() + offset);
    m_title->setPosition(m_title->getPosition() + offset);

    // Search
    searchInput = TextInput::create(235, "Search Macro", "bigFont.fnt");
    searchInput->setPositionY(100);
    searchInput->setCallback([this](std::string const &text) {
      search = toLower(text);

      if (searchOff) {
        searchOff->setVisible(!search.empty());
        searchOff->setOpacity(184);
      }

      applyFilter();
    });
    menu->addChild(searchInput);

    // Sort (A-Z / Z-A)
    CCSprite *spr1 = CCSprite::create("GJ_button_01.png");
    CCSprite *spr2 = CCSprite::createWithSpriteFrameName("GJ_sortIcon_001.png");
    spr2->setPosition({20, 20});
    spr1->addChild(spr2);

    CCSprite *spr3 = CCSprite::create("GJ_button_02.png");
    CCSprite *spr4 = CCSprite::createWithSpriteFrameName("GJ_sortIcon_001.png");
    spr4->setPosition({20, 20});
    spr3->addChild(spr4);

    sortToggle = CCMenuItemToggler::create(
        spr1, spr3, this, menu_selector(MacroIndexPopup::updateSort));
    sortToggle->setPosition({-145, 100});
    sortToggle->setScale(0.55f);
    sortToggle->toggle(false);
    menu->addChild(sortToggle);

    // Clear search
    CCSprite *spr = CCSprite::createWithSpriteFrameName("gj_findBtnOff_001.png");
    spr->setScale(0.685f);
    searchOff = CCMenuItemSpriteExtra::create(
        spr, this, menu_selector(MacroIndexPopup::clearSearch));
    searchOff->setPosition(ccp(137, 100));
    searchOff->setVisible(false);
    menu->addChild(searchOff);

    // Count
    countLabel = CCLabelBMFont::create("0 Macros", "chatFont.fnt");
    countLabel->setOpacity(108);
    countLabel->setScale(0.55f);
    countLabel->setAnchorPoint({1.f, 0.5f});
    countLabel->setPosition({180, 130});
    menu->addChild(countLabel);

    // Message ở giữa list (loading / lỗi / không có macro)
    messageLabel = CCLabelBMFont::create("", "bigFont.fnt");
    messageLabel->setScale(0.5f);
    messageLabel->setOpacity(100);
    messageLabel->setPosition({0, -10});
    menu->addChild(messageLabel);

    // Hàng dưới: Index URL + Apply + Refresh
    hostInput = TextInput::create(215, "Index URL", "chatFont.fnt");
    hostInput->setPosition({-52, -121});
    hostInput->setString(
        Mod::get()->getSettingValue<std::string>("macro_index_url"));
    menu->addChild(hostInput);

    auto applySprite = ButtonSprite::create("Apply");
    applySprite->setScale(0.5f);
    auto applyBtn = CCMenuItemSpriteExtra::create(
        applySprite, this, menu_selector(MacroIndexPopup::onApplyHost));
    applyBtn->setPosition(ccp(92, -121));
    menu->addChild(applyBtn);

    auto refreshSprite =
        CCSprite::createWithSpriteFrameName("GJ_updateBtn_001.png");
    refreshSprite->setScale(0.55f);
    auto refreshBtn = CCMenuItemSpriteExtra::create(
        refreshSprite, this, menu_selector(MacroIndexPopup::onRefresh));
    refreshBtn->setPosition(ccp(143, -121));
    menu->addChild(refreshBtn);

    rebuildList();
    fetchIndex();

    return true;
  }

  // ========================================================
  // List (copy từ LoadMacroLayer::addList, hàng = IndexRow)
  // ========================================================

  void rebuildList() {
    if (CCNode *node = m_buttonMenu->getChildByID("scrollbar"))
      node->removeFromParentAndCleanup(true);

    // Phải xoá effect cũ trước khi xoá các sprite nó đang giữ con trỏ tới.
    if (CCNode *node = m_buttonMenu->getChildByID("rgb-effect"))
      node->removeFromParentAndCleanup(true);

    if (CCNode *node = m_buttonMenu->getChildByID("list-layer"))
      node->removeFromParentAndCleanup(true);

    if (CCNode *node = m_buttonMenu->getChildByID("background"))
      node->removeFromParentAndCleanup(true);

    rowMenus.clear();

    addList();
  }

  CCNode *makeRow(size_t index) {
    auto const &entry = filteredEntries[index];

    auto row = CCNode::create();
    row->setContentSize({LIST_WIDTH, CELL_HEIGHT});

    // Kiểu chữ giống MacroCell: tên chatFont (giới hạn bề rộng), dòng phụ mờ + nghiêng.
    std::string name =
        entry.levelName.empty() ? "Unknown Level" : entry.levelName;
    name = truncate(name, 60);

    auto nameLabel = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
    nameLabel->limitLabelWidth(240.f, 0.8f, 0.01f);
    nameLabel->setAnchorPoint({0.f, 0.5f});
    nameLabel->updateLabel();
    nameLabel->setPosition({10.f, 23.f});
    row->addChild(nameLabel);

    std::string info = fmt::format(
        "ID: {} | {} | {} | {:.1f}", entry.levelId,
        entry.difficulty.empty() ? "Unknown" : entry.difficulty,
        entry.uploader.empty() ? "Unknown" : entry.uploader, entry.rating);
    info = truncate(info, 70);

    auto infoLabel = CCLabelBMFont::create(info.c_str(), "chatFont.fnt");
    infoLabel->setPosition({10.f, 9.f});
    infoLabel->setScale(0.55f);
    infoLabel->setSkewX(2);
    infoLabel->setAnchorPoint({0.f, 0.5f});
    infoLabel->setOpacity(80);
    row->addChild(infoLabel);

    auto rowMenu = IndexRowMenu::create();
    rowMenu->setContentSize({LIST_WIDTH, CELL_HEIGHT});
    rowMenu->setPosition({0.f, 0.f});
    row->addChild(rowMenu);
    rowMenus.push_back(rowMenu);

    CCNode *image = nullptr;

    if (auto sprite =
            CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png")) {
      sprite->setScale(0.5f);
      image = sprite;
    } else {
      auto fallback = ButtonSprite::create("Download");
      fallback->setScale(0.38f);
      image = fallback;
    }

    auto downloadBtn = CCMenuItemSpriteExtra::create(
        image, this, menu_selector(MacroIndexPopup::onDownloadClicked));
    downloadBtn->setTag(static_cast<int>(index));
    downloadBtn->setPosition({LIST_WIDTH - 26.f, CELL_HEIGHT / 2.f});
    rowMenu->addChild(downloadBtn);

    return row;
  }

  void addList() {
    cocos2d::CCSize winSize =
        cocos2d::CCDirector::sharedDirector()->getWinSize();

    CCArray *cells = CCArray::create();

    size_t shown = std::min(MAX_ROWS, filteredEntries.size());
    for (size_t i = 0; i < shown; i++)
      cells->addObject(makeRow(i));

    countLabel->setString(
        fmt::format("{} Macros", filteredEntries.size()).c_str());

    // Message giữa list
    if (loading) {
      messageLabel->setString("Loading...");
      messageLabel->setVisible(true);
    } else if (!errorText.empty()) {
      messageLabel->setString(errorText.c_str());
      messageLabel->setVisible(true);
    } else if (cells->count() == 0) {
      messageLabel->setString("No Macros");
      messageLabel->setVisible(true);
    } else {
      messageLabel->setVisible(false);
    }

    ListView *listView = ListView::create(cells, CELL_HEIGHT, LIST_WIDTH,
                                          LIST_HEIGHT);
    CCNode *contentLayer = static_cast<CCNode *>(
        listView->m_tableView->getChildren()->objectAtIndex(0));

    cocos2d::ccColor3B color =
        Mod::get()->getSettingValue<cocos2d::ccColor3B>("background_color");

    CCArray *children = contentLayer->getChildren();
    int it = 0;

    cocos2d::ccColor3B color1 =
        ccc3(std::max(0, color.r - 70), std::max(0, color.g - 70),
             std::max(0, color.b - 70));
    cocos2d::ccColor3B color2 =
        ccc3(std::max(0, color.r - 55), std::max(0, color.g - 55),
             std::max(0, color.b - 55));

    for (auto child : CCArrayExt<CCObject *>(children)) {
      if (GenericListCell *cell = typeinfo_cast<GenericListCell *>(child)) {
        cocos2d::ccColor3B col = (it % 2 == 0) ? color1 : color2;
        it++;
        cell->m_backgroundLayer->setColor(col);
      }
    }

    GJCommentListLayer *listLayer = GJCommentListLayer::create(
        listView, "Custom Labels", ccc4(255, 255, 255, 0), LIST_WIDTH,
        LIST_HEIGHT, true);
    listLayer->setPosition((winSize / 2) - (listLayer->getContentSize() / 2) -
                           CCPoint((it >= 5) ? 6 : 0, 0) + ccp(0, 1));
    listLayer->setZOrder(1);
    listLayer->setID("list-layer");
    listView->setPositionY(-12);
    m_buttonMenu->addChild(listLayer);

    listLayer->setUserObject("dont-correct-borders",
                             cocos2d::CCBool::create(true));

    // Vùng nhìn thấy của list (world space) cho các menu của hàng.
    {
      CCPoint worldPos =
          m_buttonMenu->convertToWorldSpace(listLayer->getPosition());
      CCRect clip(worldPos.x, worldPos.y - 6.f, LIST_WIDTH, LIST_HEIGHT - 12.f);

      for (auto *rowMenu : rowMenus) {
        rowMenu->clipRect = clip;
        rowMenu->hasClip = true;
      }
    }

    CCSprite *topBorder = listLayer->getChildByType<CCSprite>(1);
    CCSprite *bottomBorder = listLayer->getChildByType<CCSprite>(0);
    CCSprite *rightBorder = listLayer->getChildByType<CCSprite>(3);
    CCSprite *leftBorder = listLayer->getChildByType<CCSprite>(2);

    if (color != ccc3(51, 68, 153)) {
      CCSprite *topSprite =
          CCSprite::create("GJ_commentTop2_001_White.png"_spr);
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
      scrollbar->setPosition(
          {(winSize.width / 2) +
               (listLayer->getScaledContentSize().width / 2) + 4,
           winSize.height / 2});
      scrollbar->setID("scrollbar");
      m_buttonMenu->addChild(scrollbar);
    }

    // RGB: công tắc nằm trong saved value "menu_rgb_ui" (trang 5 của menu).
    if (Mod::get()->getSavedValue<bool>("menu_rgb_ui")) {
      std::vector<cocos2d::CCNode *> rgbTargets = {
          topBorder, bottomBorder, leftBorder, rightBorder, listBackground};

      RGBEffect *fx = RGBEffect::create(rgbTargets, 60.f);
      fx->setID("rgb-effect");
      m_buttonMenu->addChild(fx);
    } else {
      topBorder->setColor(color);
      bottomBorder->setColor(color);
      leftBorder->setColor(color);
      rightBorder->setColor(color);
    }
  }

  // ========================================================
  // Search / sort
  // ========================================================

  void applyFilter() {
    filteredEntries.clear();

    for (auto const &entry : allEntries) {
      if (search.empty() ||
          entry.lowerName.find(search) != std::string::npos) {
        filteredEntries.push_back(entry);
      }
    }

    std::stable_sort(filteredEntries.begin(), filteredEntries.end(),
                     [this](MacroEntry const &a, MacroEntry const &b) {
                       return invertSort ? a.lowerName > b.lowerName
                                         : a.lowerName < b.lowerName;
                     });

    rebuildList();
  }

  void updateSort(CCObject *) {
    if (!sortToggle)
      return;

    invertSort = !sortToggle->isToggled();
    applyFilter();
  }

  void clearSearch(CCObject *) {
    searchOff->setVisible(false);
    searchInput->setString("");
    search = "";

    applyFilter();
  }

  // ========================================================
  // Fetch index
  // ========================================================

  void fetchIndex() {
    auto url = Mod::get()->getSettingValue<std::string>("macro_index_url");

    if (url.empty()) {
      allEntries.clear();
      loading = false;
      errorText = "Index URL is empty.";
      applyFilter();
      return;
    }

    loading = true;
    errorText.clear();
    rebuildList();

    indexTask.spawn(geode::utils::web::WebRequest()
                        .userAgent("White Bot Macro Index")
                        .get(url),
                    [this](geode::utils::web::WebResponse response) {
                      this->onIndexResult(std::move(response));
                    });
  }

  void failIndex(std::string const &text) {
    allEntries.clear();
    loading = false;
    errorText = text;
    applyFilter();
  }

  void onIndexResult(geode::utils::web::WebResponse response) {
    if (!response.ok()) {
      failIndex("Failed to fetch index.");
      return;
    }

    auto textResult = response.string();

    if (!textResult) {
      failIndex("Failed to read index.");
      return;
    }

    std::string content = textResult.unwrapOr("");

    if (content.empty()) {
      failIndex("Index is empty.");
      return;
    }

    try {
      auto json = nlohmann::json::parse(content);

      if (!json.is_array()) {
        failIndex("Invalid index format.");
        return;
      }

      allEntries.clear();

      for (auto const &item : json) {
        if (!item.is_object())
          continue;

        MacroEntry entry;

        entry.levelName = item.value("level_name", std::string());
        entry.lowerName = toLower(entry.levelName);
        entry.levelId = item.value("level_id", 0);
        entry.difficulty = item.value("difficulty", std::string());
        entry.uploader = item.value("uploader", std::string());
        entry.rating = item.value("rating", 0.f);
        entry.format = item.value("format", std::string());
        entry.downloadUrl = item.value("download_url", std::string());

        if (entry.downloadUrl.empty())
          continue;

        allEntries.push_back(std::move(entry));
      }

      loading = false;
      errorText.clear();
      applyFilter();
    } catch (std::exception const &) {
      failIndex("Invalid JSON index.");
    }
  }

  // ========================================================
  // Download macro
  // ========================================================

  void finishDownload(std::filesystem::path const &temporary, bool ok,
                      std::string const &text) {
    downloading = false;

    if (downloadNotification) {
      downloadNotification->hide();
      downloadNotification = nullptr;
    }

    if (!ok) {
      std::error_code ec;
      std::filesystem::remove(temporary, ec);
    }

    Notification::create(text, ok ? NotificationIcon::Success
                                  : NotificationIcon::Error)
        ->show();
  }

  void onDownloadClicked(CCObject *sender) {
    if (downloading) {
      Notification::create("A download is already in progress.",
                           NotificationIcon::Warning)
          ->show();
      return;
    }

    auto button = static_cast<CCMenuItemSpriteExtra *>(sender);
    int index = button->getTag();

    if (index < 0 || static_cast<size_t>(index) >= filteredEntries.size())
      return;

    MacroEntry entry = filteredEntries[index];

    if (entry.downloadUrl.empty()) {
      Notification::create("Invalid download URL.", NotificationIcon::Error)
          ->show();
      return;
    }

    auto folder = Mod::get()->getSettingValue<std::filesystem::path>(
        "macros_folder");

    try {
      std::filesystem::create_directories(folder);
    } catch (...) {
      Notification::create("Failed to create macros folder.",
                           NotificationIcon::Error)
          ->show();
      return;
    }

    std::string filename = sanitizeFilename(entry.levelName);
    std::string extension = getExtension(entry.format);

    std::filesystem::path output = folder / (filename + extension);

    int collision = 1;

    while (std::filesystem::exists(output)) {
      output = folder / fmt::format("{} ({}){}", filename, collision,
                                    extension);
      ++collision;
    }

    std::filesystem::path temporary = output;
    temporary += ".part";

    downloading = true;

    downloadNotification = Notification::create(
        fmt::format("Downloading {}...", entry.levelName),
        NotificationIcon::Loading, 0.f);
    downloadNotification->show();

    downloadTask.spawn(
        geode::utils::web::WebRequest()
            .userAgent("White Bot Macro Index")
            .get(entry.downloadUrl),
        [this, output, temporary](geode::utils::web::WebResponse response) {
          if (!response.ok()) {
            finishDownload(temporary, false, "Download failed.");
            return;
          }

          // Ghi ra .part trước rồi mới đổi tên để không bao giờ có macro dang dở.
          auto saved = response.into(temporary);

          if (!saved) {
            finishDownload(temporary, false, "Failed to write macro file.");
            return;
          }

          std::error_code ec;
          auto size = std::filesystem::file_size(temporary, ec);

          if (ec || size == 0) {
            finishDownload(temporary, false, "Downloaded file is empty.");
            return;
          }

          std::filesystem::rename(temporary, output, ec);

          if (ec) {
            finishDownload(temporary, false, "Failed to finalize macro file.");
            return;
          }

          finishDownload(temporary, true,
                         fmt::format("Downloaded: {}",
                                     output.filename().string()));
        });
  }

  // ========================================================
  // Buttons
  // ========================================================

  void onRefresh(CCObject *) { fetchIndex(); }

  void onApplyHost(CCObject *) {
    if (!hostInput)
      return;

    std::string url = hostInput->getString();

    if (url.empty()) {
      Notification::create("Index URL is empty.", NotificationIcon::Warning)
          ->show();
      return;
    }

    Mod::get()->setSettingValue<std::string>("macro_index_url", url);

    Notification::create("Index URL updated.", NotificationIcon::Success)
        ->show();

    fetchIndex();
  }

public:
  ~MacroIndexPopup() {
    // Tác vụ tải bị huỷ khi popup đóng: đừng để notification "Downloading" treo mãi.
    if (downloadNotification)
      downloadNotification->hide();
  }

  static MacroIndexPopup *create() {
    auto ret = new MacroIndexPopup();

    if (ret->init(385, 291, Utils::getTexture().c_str())) {
      if (!ret->setup()) {
        delete ret;
        return nullptr;
      }

      ret->autorelease();
      return ret;
    }

    delete ret;
    return nullptr;
  }

  static void open() {
    auto layer = MacroIndexPopup::create();

    if (!layer)
      return;

    layer->m_noElasticity = true;
    layer->show();
  }
};