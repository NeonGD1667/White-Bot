#include "noclip_tint.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace {

struct TintSettings {
    bool enabled = false;
    ccColor3B color = {255, 0, 0};
    float opacity = 90.f; // percent
    float time = 0.f;     // seconds, 0 = follows the collision
};

TintSettings g_settings;
bool g_loaded = false;

void loadSettings() {
    auto* mod = Mod::get();

    g_settings.enabled =
        mod->getSavedValue<bool>("macro_noclip_tint", false);

    g_settings.color = mod->getSavedValue<ccColor3B>(
        "macro_noclip_color",
        ccColor3B{255, 0, 0}
    );

    g_settings.opacity = std::clamp(
        mod->getSavedValue<float>("macro_noclip_opacity", 90.f),
        0.f,
        100.f
    );

    g_settings.time = std::clamp(
        mod->getSavedValue<float>("macro_noclip_time", 0.f),
        0.f,
        5.f
    );

    g_loaded = true;
}

} // namespace

class $modify(NoclipTintPLHook, PlayLayer) {
public:
    struct Fields {
        CCLayerColor* tint = nullptr;
        bool triggered = false; // noclip saved the player during this frame
        float level = 0.f;      // 0..1, multiplied by the opacity setting
        float timer = 0.f;      // seconds since the last trigger (time > 0)
    };

    void ensureTint() {
        auto fields = m_fields.self();

        if (fields->tint)
            return;

        fields->tint = CCLayerColor::create(ccColor4B{
            g_settings.color.r,
            g_settings.color.g,
            g_settings.color.b,
            0
        });

        fields->tint->setZOrder(1000);
        fields->tint->setID("noclip-tint"_spr);

        if (m_uiLayer)
            m_uiLayer->addChild(fields->tint);
        else
            this->addChild(fields->tint);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto fields = m_fields.self();

        // Nothing to animate until noclip has saved the player once.
        if (!fields->tint)
            return;

        if (!g_loaded)
            loadSettings();

        if (!g_settings.enabled || m_hasCompletedLevel) {
            fields->level = 0.f;
            fields->timer = 0.f;
            fields->triggered = false;
            fields->tint->setOpacity(0);
            return;
        }

        if (g_settings.time <= 0.f) {
            // Follows the collision: fades in while it lasts, out right after.
            if (fields->triggered)
                fields->level = std::min(1.f, fields->level + dt * 15.f);
            else
                fields->level = std::max(0.f, fields->level - dt * 10.f);
        } else if (fields->triggered) {
            // Full strength, then a linear fade over "time" seconds.
            fields->timer = 0.f;
            fields->level = 1.f;
        } else if (fields->level > 0.f) {
            fields->timer += dt;
            fields->level = std::max(0.f, 1.f - fields->timer / g_settings.time);
        }

        fields->triggered = false;

        float maxAlpha = 255.f * g_settings.opacity / 100.f;

        fields->tint->setOpacity(
            static_cast<GLubyte>(fields->level * maxAlpha)
        );
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        auto fields = m_fields.self();

        fields->level = 0.f;
        fields->timer = 0.f;
        fields->triggered = false;

        if (fields->tint)
            fields->tint->setOpacity(0);
    }
};

void NoclipTint::trigger(PlayLayer* playLayer) {
    if (!playLayer)
        return;

    if (!g_loaded)
        loadSettings();

    if (!g_settings.enabled)
        return;

    auto hook = static_cast<NoclipTintPLHook*>(playLayer);

    hook->ensureTint();

    auto fields = hook->m_fields.self();

    fields->tint->setColor(g_settings.color);
    fields->triggered = true;
}

void NoclipTint::reloadSettings() {
    loadSettings();
}
