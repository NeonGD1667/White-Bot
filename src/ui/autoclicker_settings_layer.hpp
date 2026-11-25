#include "../includes.hpp"

class AutoclickerLayer : public geode::Popup {

private:

	TextInput* holdInput = nullptr;
	TextInput* releaseInput = nullptr;

	CCLabelBMFont* holdLbl = nullptr;
	CCLabelBMFont* releaseLbl = nullptr;
	CCLabelBMFont* holdTitle = nullptr;
	CCLabelBMFont* releaseTitle = nullptr;

	TextInput* holdInput2 = nullptr;
	TextInput* releaseInput2 = nullptr;

	CCLabelBMFont* holdLbl2 = nullptr;
	CCLabelBMFont* releaseLbl2 = nullptr;
	CCLabelBMFont* holdTitle2 = nullptr;
	CCLabelBMFont* releaseTitle2 = nullptr;

	CCMenuItemToggler* toggleP1 = nullptr;
	CCMenuItemToggler* toggleP2 = nullptr;

	STATIC_CREATE(AutoclickerLayer, 250, 240)

	// Geode v6: TextInput no longer wraps CCTextInputNode. Every input
	// registers this as its setCallback lambda in setup().
	void textChanged() {
		auto& g = Global::get();
		g.holdFor = geode::utils::numFromString<int>(holdInput->getString()).unwrapOr(0);
		g.releaseFor = geode::utils::numFromString<int>(releaseInput->getString()).unwrapOr(0);
		g.holdFor2 = geode::utils::numFromString<int>(holdInput2->getString()).unwrapOr(0);
		g.releaseFor2 = geode::utils::numFromString<int>(releaseInput2->getString()).unwrapOr(0);

		g.mod->setSavedValue("autoclicker_hold_for", g.holdFor);
		g.mod->setSavedValue("autoclicker_release_for", g.releaseFor);
		g.mod->setSavedValue("autoclicker_hold_for2", g.holdFor2);
		g.mod->setSavedValue("autoclicker_release_for2", g.releaseFor2);

		float dt = 1.f / Global::getTPS();

		holdLbl->setString(fmt::format("{:.3f}s", dt * g.holdFor).c_str());
		releaseLbl->setString(fmt::format("{:.3f}s", dt * g.releaseFor).c_str());
		holdLbl2->setString(fmt::format("{:.3f}s", dt * g.holdFor2).c_str());
		releaseLbl2->setString(fmt::format("{:.3f}s", dt * g.releaseFor2).c_str());
	}
	
    bool setup() {
        setTitle("Autoclicker");
		m_title->setScale(0.625f);
		m_title->setPositionY(224);

		float x = m_size.width - 72;

		CCLabelBMFont* lbl = CCLabelBMFont::create("Player 1", "goldFont.fnt");
		lbl->setPosition({m_size.width / 2, 195});
		lbl->setScale(0.46f);
		m_mainLayer->addChild(lbl);

		holdTitle = CCLabelBMFont::create("Hold For", "bigFont.fnt");
		holdTitle->setPosition({72, 175});
		holdTitle->setScale(0.37f);
		m_mainLayer->addChild(holdTitle);
		
		releaseTitle = CCLabelBMFont::create("Release For", "bigFont.fnt");
		releaseTitle->setPosition({x, 175});
		releaseTitle->setScale(0.37f);
		m_mainLayer->addChild(releaseTitle);

		holdLbl = CCLabelBMFont::create("0.325s", "chatFont.fnt");
		holdLbl->setPosition({72, 129});
		holdLbl->setScale(0.4f);
		holdLbl->setOpacity(86);
		m_mainLayer->addChild(holdLbl);

		releaseLbl = CCLabelBMFont::create("0.6s", "chatFont.fnt");
		releaseLbl->setPosition({x, 129});
		releaseLbl->setScale(0.4f);
		releaseLbl->setOpacity(86);
		m_mainLayer->addChild(releaseLbl);

		holdInput = TextInput::create(50, "Frames", "chatFont.fnt");
		holdInput->setPosition({72, 150});
		holdInput->setString(std::to_string(Mod::get()->getSavedValue<int64_t>("autoclicker_hold_for")).c_str());
		holdInput->setCallback([this](std::string const&) { textChanged(); });
		holdInput->setFilter("0123456789");
		holdInput->setMaxCharCount(4);
		m_mainLayer->addChild(holdInput);

		releaseInput = TextInput::create(50, "Frames", "chatFont.fnt");
		releaseInput->setPosition({x, 150});
		releaseInput->setString(std::to_string(Mod::get()->getSavedValue<int64_t>("autoclicker_release_for")).c_str());
		releaseInput->setCallback([this](std::string const&) { textChanged(); });
		releaseInput->setFilter("0123456789");
		releaseInput->setMaxCharCount(4);
		m_mainLayer->addChild(releaseInput);

		// ------------------------

		lbl = CCLabelBMFont::create("Player 2", "goldFont.fnt");
		lbl->setPosition({m_size.width / 2, 115});
		lbl->setScale(0.46f);
		m_mainLayer->addChild(lbl);

		holdTitle2 = CCLabelBMFont::create("Hold For", "bigFont.fnt");
		holdTitle2->setPosition({72, 95});
		holdTitle2->setScale(0.37f);
		m_mainLayer->addChild(holdTitle2);
		
		releaseTitle2 = CCLabelBMFont::create("Release For", "bigFont.fnt");
		releaseTitle2->setPosition({x, 95});
		releaseTitle2->setScale(0.37f);
		m_mainLayer->addChild(releaseTitle2);

		holdLbl2 = CCLabelBMFont::create("0.325s", "chatFont.fnt");
		holdLbl2->setPosition({72, 49});
		holdLbl2->setScale(0.4f);
		holdLbl2->setOpacity(86);
		m_mainLayer->addChild(holdLbl2);

		releaseLbl2 = CCLabelBMFont::create("0.6s", "chatFont.fnt");
		releaseLbl2->setPosition({x, 49});
		releaseLbl2->setScale(0.4f);
		releaseLbl2->setOpacity(86);
		m_mainLayer->addChild(releaseLbl2);

		holdInput2 = TextInput::create(50, "Frames", "chatFont.fnt");
		holdInput2->setPosition({72, 70});
		holdInput2->setString(std::to_string(Mod::get()->getSavedValue<int64_t>("autoclicker_hold_for2")).c_str());
		holdInput2->setCallback([this](std::string const&) { textChanged(); });
		holdInput2->setFilter("0123456789");
		holdInput2->setMaxCharCount(4);
		m_mainLayer->addChild(holdInput2);

		releaseInput2 = TextInput::create(50, "Frames", "chatFont.fnt");
		releaseInput2->setPosition({x, 70});
		releaseInput2->setString(std::to_string(Mod::get()->getSavedValue<int64_t>("autoclicker_release_for2")).c_str());
		releaseInput2->setCallback([this](std::string const&) { textChanged(); });
		releaseInput2->setFilter("0123456789");
		releaseInput2->setMaxCharCount(4);
		m_mainLayer->addChild(releaseInput2);

		toggleP1 = CCMenuItemToggler::create(
			CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"),
			CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"),
			this,
			menu_selector(AutoclickerLayer::onToggle)
		);
		toggleP1->setPosition({162, 195});
		toggleP1->setScale(0.5f);
		toggleP1->toggle(Global::get().autoclickerP1);
		m_buttonMenu->addChild(toggleP1);

		toggleP2 = CCMenuItemToggler::create(
			CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"),
			CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"),
			this,
			menu_selector(AutoclickerLayer::onToggle)
		);
		toggleP2->setPosition({162, 115});
		toggleP2->setScale(0.5f);
		toggleP2->toggle(Global::get().autoclickerP2);
		m_buttonMenu->addChild(toggleP2);

		ButtonSprite* btnSpr = ButtonSprite::create("Ok");
		btnSpr->setScale(0.7f);
		CCMenuItemSpriteExtra* btn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(AutoclickerLayer::onClose));
		btn->setPosition({m_size.width / 2, 24});
		m_buttonMenu->addChild(btn);

		textChanged();
		updateInputs();

		return true;
    }

	void onToggle(CCObject*) {
		Loader::get()->queueInMainThread([this] {
			auto& g = Global::get();
			g.autoclickerP1 = toggleP1->isToggled();
			g.autoclickerP2 = toggleP2->isToggled();
			g.mod->setSavedValue("autoclicker_p1", g.autoclickerP1);
			g.mod->setSavedValue("autoclicker_p2", g.autoclickerP2);
			updateInputs();
		});
	}

	void updateInputs() {
		auto& g = Global::get();

		holdTitle->setOpacity(g.autoclickerP1 ? 255 : 100);
		holdLbl->setOpacity(g.autoclickerP1 ? 86 : 23);
		holdInput->getBGSprite()->setOpacity(g.autoclickerP1 ? 90 : 30);
		holdInput->setEnabled(g.autoclickerP1);
		// v6 TextInput owns its caret/placeholder; unfocus() closes the IME.
		if (!g.autoclickerP1) {
			holdInput->unfocus();
		}

		holdTitle2->setOpacity(g.autoclickerP2 ? 255 : 100);
		holdLbl2->setOpacity(g.autoclickerP2 ? 86 : 23);
		holdInput2->getBGSprite()->setOpacity(g.autoclickerP2 ? 90 : 30);
		holdInput2->setEnabled(g.autoclickerP2);
		// v6 TextInput owns its caret/placeholder; unfocus() closes the IME.
		if (!g.autoclickerP2) {
			holdInput2->unfocus();
		}

		releaseTitle->setOpacity(g.autoclickerP1 ? 255 : 100);
		releaseLbl->setOpacity(g.autoclickerP1 ? 86 : 23);
		releaseInput->getBGSprite()->setOpacity(g.autoclickerP1 ? 90 : 30);
		releaseInput->setEnabled(g.autoclickerP1);
		// v6 TextInput owns its caret/placeholder; unfocus() closes the IME.
		if (!g.autoclickerP1) {
			releaseInput->unfocus();
		}

		releaseTitle2->setOpacity(g.autoclickerP2 ? 255 : 100);
		releaseLbl2->setOpacity(g.autoclickerP2 ? 86 : 23);
		releaseInput2->getBGSprite()->setOpacity(g.autoclickerP2 ? 90 : 30);
		releaseInput2->setEnabled(g.autoclickerP2);
		// v6 TextInput owns its caret/placeholder; unfocus() closes the IME.
		if (!g.autoclickerP2) {
			releaseInput2->unfocus();
		}
	}

public:
	
	void open(CCObject*) {
		create()->show();
	}

};