#include "hacks/layout_mode.hpp"
#include "hacks/show_trajectory.hpp"
#include "includes.hpp"
#include "ui/clickbot_layer.hpp"
#include "ui/game_ui.hpp"
#include "ui/macro_editor.hpp"
#include "ui/record_layer.hpp"
#include "ui/render_settings_layer.hpp"

#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/CCTouchDispatcher.hpp>

#if !defined(GEODE_IS_IOS)
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#endif

// NOTE: geode.custom-keybinds is incompatible with Geode v5
// (EventFilter/EventListener removed).
// Using Geode's built-in KeybindSettingV3 API instead.

const std::vector<std::string> keybindIDs = {
    "open_menu",
    "toggle_recording",
    "toggle_playing",
    "toggle_speedhack",
    "toggle_frame_stepper",
    "step_frame",
    "toggle_render",
    "toggle_noclip",
    "show_trajectory"
};

// ============================================================
// Keyboard dispatcher hook
// Disabled on iOS because the generated Geode modify bindings
// for CCKeyboardDispatcher currently fail to compile there.
// ============================================================

#if !defined(GEODE_IS_IOS)

class $modify(CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(
        enumKeyCodes key,
        bool isKeyDown,
        bool isKeyRepeat,
        double p3
    ) {
        auto& g = Global::get();

        const int keyInt = static_cast<int>(key);

        // Track held buttons for configured keybinds.
        if (g.allKeybinds.contains(keyInt) && !isKeyRepeat) {
            for (size_t i = 0; i < 6; i++) {
                if (
                    std::find(
                        g.keybinds[i].begin(),
                        g.keybinds[i].end(),
                        keyInt
                    ) != g.keybinds[i].end()
                ) {
                    g.heldButtons[i] = isKeyDown;
                }
            }
        }

        // Swift Click handling.
        if (
            g.swiftClickEnabled &&
            !isKeyRepeat &&
            isKeyDown &&
            keyInt == g.swiftClickKey &&
            PlayLayer::get() &&
            (g.state == state::recording || g.state == state::none)
        ) {
            PlayLayer* pl = PlayLayer::get();

            if (pl && !pl->m_isPaused) {
                for (int i = 0; i < g.swiftClickCount; i++) {
                    pl->handleButton(true, 1, false);
                    pl->handleButton(false, 1, false);
                }
            }
        }

        // Hold H to step forward.
        if (key == enumKeyCodes::KEY_H && !isKeyRepeat) {
            g.holdingStepForward = isKeyDown;
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(
            key,
            isKeyDown,
            isKeyRepeat,
            p3
        );
    }
};

#endif

// ============================================================
// Built-in Geode keybind callbacks
// ============================================================

namespace {

bool shouldHandleDllBotKeybind(bool down, bool repeat) {
    return down && !repeat;
}

void handleOpenMenuKeybind(
    Keybind const&,
    bool down,
    bool repeat,
    double
) {
    if (!shouldHandleDllBotKeybind(down, repeat)) {
        return;
    }

    auto& g = Global::get();

    if (g.layer) {
        static_cast<RecordLayer*>(g.layer)->onClose(nullptr);
        return;
    }

    RecordLayer::openMenu();
}

void handleToggleMacroKeybind(
    Keybind const&,
    bool down,
    bool repeat,
    double
) {
    if (!shouldHandleDllBotKeybind(down, repeat)) {
        return;
    }

    Macro::togglePlaying();
}

void handleStepForward(
    Keybind const&,
    bool down,
    bool repeat,
    double
) {
    if (!shouldHandleDllBotKeybind(down, repeat)) {
        return;
    }

    Global::frameStep(1);
}

void handleHoldForward(
    Keybind const&,
    bool down,
    bool repeat,
    double
) {
    auto& g = Global::get();
    g.holdingStepForward = down;
}

void handleToggleStepper(
    Keybind const&,
    bool down,
    bool repeat,
    double
) {
    if (!shouldHandleDllBotKeybind(down, repeat)) {
        return;
    }

    Global::toggleFrameStepper();
}

// Macro Swapper: reopen the existing Load Macro UI.
// MacroCell::onLoad() already handles confirmation when needed.
void handleMacroSwapper(
    Keybind const&,
    bool down,
    bool repeat,
    double
) {
    if (!shouldHandleDllBotKeybind(down, repeat)) {
        return;
    }

    LoadMacroLayer::open(nullptr, nullptr, false);
}

} // namespace

// ============================================================
// Register keybind listeners
// ============================================================

$on_mod(Loaded) {
    geode::listenForKeybindSettingPresses(
        "keybind_open_menu",
        +[](Keybind const& keybind, bool down, bool repeat, double timestamp) {
            handleOpenMenuKeybind(keybind, down, repeat, timestamp);
        }
    );

    geode::listenForKeybindSettingPresses(
        "keybind_toggle_macro",
        +[](Keybind const& keybind, bool down, bool repeat, double timestamp) {
            handleToggleMacroKeybind(keybind, down, repeat, timestamp);
        }
    );

    geode::listenForKeybindSettingPresses(
        "keybind_step_forward",
        +[](Keybind const& keybind, bool down, bool repeat, double timestamp) {
            handleStepForward(keybind, down, repeat, timestamp);
        }
    );

    geode::listenForKeybindSettingPresses(
        "keybind_toggle_stepper",
        +[](Keybind const& keybind, bool down, bool repeat, double timestamp) {
            handleToggleStepper(keybind, down, repeat, timestamp);
        }
    );

    geode::listenForKeybindSettingPresses(
        "keybind_macro_swapper",
        +[](Keybind const& keybind, bool down, bool repeat, double timestamp) {
            handleMacroSwapper(keybind, down, repeat, timestamp);
        }
    );
}