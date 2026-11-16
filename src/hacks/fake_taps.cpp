#include "fake_taps.hpp"

#include <algorithm>
#include <unordered_map>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/CCDirector.hpp>

using namespace geode::prelude;

namespace {

struct FakeTouch {
    CCPoint position;
    float opacity = 0.f;
    bool released = false;
};

std::unordered_map<int, FakeTouch> g_fakeTouches;

constexpr int FAKE_P1 = 0;
constexpr int FAKE_P2 = 1;

// Cached copy of the Faketaps settings (see FakeTapsSettingsLayer).
bool g_settingsLoaded = false;
float g_scale = 0.5f;
ccColor3B g_color = {255, 255, 255};

void loadSettings() {
    g_scale = std::clamp(
        Mod::get()->getSavedValue<float>("faketaps_scale", 0.5f),
        0.1f,
        5.f
    );

    g_color = Mod::get()->getSavedValue<ccColor3B>(
        "faketaps_color",
        ccColor3B{255, 255, 255}
    );

    g_settingsLoaded = true;
}

void renderFakeTouches() {
    if (!Mod::get()->getSavedValue<bool>("faketaps")) {
        // Drop leftovers so a touch that was held when the setting was turned off
        // does not reappear when it is turned back on.
        g_fakeTouches.clear();
        return;
    }

    if (g_fakeTouches.empty())
        return;

    auto dt =
        CCDirector::sharedDirector()->getDeltaTime();

    constexpr float fadeSpeed = 6.f;
    constexpr float maxOpacity = 0.5f;
    if (!g_settingsLoaded)
        loadSettings();

    int radius =
        std::max(1, static_cast<int>(g_scale * 16.f));

    int segments =
        std::max(8, static_cast<int>(g_scale * 32.f));

    ccGLBlendFunc(
        GL_SRC_ALPHA,
        GL_ONE_MINUS_SRC_ALPHA
    );

    std::erase_if(
        g_fakeTouches,
        [dt, maxOpacity](auto& item) {
            auto& touch = item.second;

            if (!touch.released) {
                touch.opacity =
                    std::min(
                        maxOpacity,
                        touch.opacity +
                            fadeSpeed * dt
                    );
            }
            else {
                touch.opacity -=
                    fadeSpeed * dt;
            }

            return touch.opacity <= 0.f;
        }
    );

    for (auto const& [id, touch] : g_fakeTouches) {
        if (touch.opacity <= 0.f)
            continue;

        ccDrawColor4B(
            g_color.r,
            g_color.g,
            g_color.b,
            static_cast<GLubyte>(
                touch.opacity * 255.f
            )
        );

        ccDrawFilledCircle(
            touch.position,
            radius,
            0,
            segments
        );
    }
}

} // namespace

void FakeTaps::press(
    int player,
    CCPoint const& position
) {
    auto it = g_fakeTouches.find(player);

    if (it != g_fakeTouches.end()) {
        // Pressed again while the circle is still fading: keep its opacity (no flicker).
        it->second.position = position;
        it->second.released = false;
        return;
    }

    g_fakeTouches[player] = {
        position,
        0.f,
        false
    };
}

void FakeTaps::reloadSettings() {
    loadSettings();
}

void FakeTaps::release(int player) {
    auto it = g_fakeTouches.find(player);

    if (it != g_fakeTouches.end()) {
        it->second.released = true;
    }
}

#if defined(GEODE_IS_WINDOWS)

class $modify(FakeTapsCCEGLView, CCEGLView) {
    void swapBuffers() {
        renderFakeTouches();
        CCEGLView::swapBuffers();
    }
};

#elif defined(GEODE_IS_ANDROID) || defined(GEODE_IS_MACOS) || defined(GEODE_IS_IOS)

class $modify(FakeTapsCCDirector, CCDirector) {
    void drawScene() {
        CCDirector::drawScene();
        renderFakeTouches();
    }
};

#endif
