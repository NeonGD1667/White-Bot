#include "../includes.hpp"
#include "clickbot.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>

#include <string>

$execute {
    auto& g = Global::get();

    if (!g.mod->setSavedValue("clickbot_defaults5", true)) {
        g.mod->setSavedValue("clickbot_holding_only", true);
        g.mod->setSavedValue("clickbot_playing_only", false);
    }

    if (!g.mod->setSavedValue("clickbot_defaults4", true)) {
        // Geode v6 uses asp::fs::Path instead of std::filesystem::path.
        const asp::fs::Path dir = g.mod->getResourcesDir();
        ClickSetting settings;

        for (const auto& name : buttonNames) {
            // Store the UTF-8 path as a string in saved JSON data.
            settings.path = (dir / fmt::format("default_{}.mp3", name)).string();

            const matjson::Value data =
                matjson::Serialize<ClickSetting>::to_json(settings);

            g.mod->setSavedValue(name, data);
        }

        g.mod->setSavedValue("clickbot_volume", 100);
        g.mod->setSavedValue("clickbot_pitch", 1.f);
    }

    g.clickbotEnabled =
        g.mod->getSavedValue<bool>("clickbot_enabled");
    g.clickbotOnlyPlaying =
        g.mod->getSavedValue<bool>("clickbot_playing_only");
    g.clickbotOnlyHolding =
        g.mod->getSavedValue<bool>("clickbot_holding_only");

    Clickbot::updateSounds();
}

class $modify(GJBaseGameLayer) {
    void handleButton(bool hold, int button, bool player2) {
        // Always forward the original input first.
        GJBaseGameLayer::handleButton(hold, button, player2);

        auto& g = Global::get();

        if (!g.clickbotEnabled) return;
        if (button < 1 || button > 3) return;
        if (!hold && g.clickbotOnlyHolding) return;

        auto* playLayer = PlayLayer::get();
        if (!playLayer) return;

        if (g.clickbotOnlyPlaying && g.state != state::playing) {
            return;
        }

        std::string buttonName;
        switch (button) {
            case 1:
                buttonName = "click";
                break;
            case 2:
                buttonName = "left";
                break;
            case 3:
                buttonName = "right";
                break;
            default:
                return;
        }

        const std::string soundID =
            (hold ? "hold_" : "release_") + buttonName;

        Clickbot::playSound(soundID);
    }
};

FMOD::Sound* Clickbot::getSound(std::string id) {
    auto& c = get();

    if (id == "hold_click") return c.holdClick;
    if (id == "release_click") return c.releaseClick;
    if (id == "hold_left") return c.holdLeft;
    if (id == "release_left") return c.releaseLeft;
    if (id == "hold_right") return c.holdRight;
    if (id == "release_right") return c.releaseRight;

    return nullptr;
}

void Clickbot::setSound(std::string id, FMOD::Sound* sound) {
    auto& c = get();

    if (id == "hold_click") c.holdClick = sound;
    else if (id == "release_click") c.releaseClick = sound;
    else if (id == "hold_left") c.holdLeft = sound;
    else if (id == "release_left") c.releaseLeft = sound;
    else if (id == "hold_right") c.holdRight = sound;
    else if (id == "release_right") c.releaseRight = sound;
}

void Clickbot::playSound(std::string id) {
    auto& c = get();

    if (!c.system) {
        updateSounds();
        if (!c.system) return;
    }

    auto& g = Global::get();

    const matjson::Value data =
        g.mod->getSavedValue<matjson::Value>(id);
    const ClickSetting settings =
        matjson::Serialize<ClickSetting>::from_json(data);

    if (settings.disabled) return;

    FMOD::Sound* sound = getSound(id);
    if (!sound) return;

    const int masterVolume =
        static_cast<int>(g.mod->getSavedValue<int64_t>("clickbot_volume"));

    if (settings.volume <= 0 || masterVolume <= 0) return;

    FMOD_RESULT result = c.system->playSound(
        sound,
        nullptr,
        false,
        &c.channel
    );

    if (result != FMOD_OK || !c.channel) {
        log::debug("Click sound errored. ID: 2");
        return;
    }

    const float soundVolume =
        (settings.volume / 100.f) * (masterVolume / 100.f);

    result = c.channel->setVolume(soundVolume);
    if (result != FMOD_OK) {
        log::debug("Click sound errored. ID: 3");
        return;
    }

    result = c.channel->setPitch(g.currentPitch);
    if (result != FMOD_OK) {
        log::debug("Click sound errored. ID: 4");
        return;
    }

    FMOD::DSP* pitchShifter = c.pitchShifter;
    if (!pitchShifter) {
        updateSounds();
        return;
    }

    const float pitch =
        settings.pitch * g.mod->getSavedValue<float>("clickbot_pitch");

    result = pitchShifter->setParameterFloat(
        FMOD_DSP_PITCHSHIFT_PITCH,
        pitch
    );

    if (result != FMOD_OK) {
        log::debug("Click sound errored. ID: 6");
        return;
    }

    result = c.channel->addDSP(0, pitchShifter);
    if (result != FMOD_OK) {
        log::debug("Click sound errored. ID: 7");
        return;
    }
}

void Clickbot::updateSounds() {
    auto& c = get();

    if (!c.system) {
        auto* fmod = FMODAudioEngine::sharedEngine();
        if (!fmod) return;

        c.system = fmod->m_system;
    }

    if (!c.system) return;

    auto& g = Global::get();

    for (const auto& name : buttonNames) {
        const matjson::Value data =
            g.mod->getSavedValue<matjson::Value>(name);
        const ClickSetting settings =
            matjson::Serialize<ClickSetting>::from_json(data);

        if (settings.path.empty()) continue;

        // ClickSetting::path is stored as UTF-8 std::string.
        const asp::fs::Path path(settings.path);
        if (!path.exists()) continue;

        // Do not create a second FMOD sound over an existing one.
        // Sound replacement/release should be handled by the owner's
        // resource-lifecycle code, where active channels can be stopped safely.
        FMOD::Sound* sound = getSound(name);
        if (sound) continue;

        FMOD_RESULT result = c.system->createSound(
            settings.path.c_str(),
            FMOD_DEFAULT,
            nullptr,
            &sound
        );

        if (result != FMOD_OK) {
            log::debug("Click sound errored. ID: 1");
            continue;
        }

        setSound(name, sound);
    }

    if (!c.pitchShifter) {
        const FMOD_RESULT result = c.system->createDSPByType(
            FMOD_DSP_TYPE_PITCHSHIFT,
            &c.pitchShifter
        );

        if (result != FMOD_OK) {
            log::debug("Click sound errored. ID: 5");
            return;
        }
    }
}
