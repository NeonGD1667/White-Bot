#include "fake_taps.hpp"

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

void renderFakeTouches() {
    if (!Mod::get()->getSavedValue<bool>("faketaps"))
        return;

    if (g_fakeTouches.empty())
        return;

    auto dt =
        CCDirector::sharedDirector()->getDeltaTime();

    constexpr float fadeSpeed = 6.f;
    constexpr float maxOpacity = 0.5f;
    constexpr float scale = 0.5f;

    int radius =
        static_cast<int>(scale * 16.f);

    int segments =
        static_cast<int>(scale * 32.f);

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
            255,
            255,
            255,
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
    g_fakeTouches[player] = {
        position,
        0.f,
        false
    };
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

#elif defined(GEODE_IS_ANDROID) || defined(GEODE_IS_MACOS)

class $modify(FakeTapsCCDirector, CCDirector) {
    void drawScene() {
        CCDirector::drawScene();
        renderFakeTouches();
    }
};

#endif
