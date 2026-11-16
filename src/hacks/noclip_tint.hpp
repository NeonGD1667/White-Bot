#pragma once

// Noclip tint: a colored overlay that flashes while noclip is saving the player from dying.
// Settings (saved values): "macro_noclip_tint" (bool), "macro_noclip_color" (ccColor3B, red),
// "macro_noclip_opacity" (0-100, default 90), "macro_noclip_time" (seconds 0-5, default 0).
// The behavior (toggle, color, opacity, fade time) follows the Noclip tint of Eclipse Menu.

class PlayLayer;

namespace NoclipTint {

// Call every time noclip prevents a death (from the PlayLayer::destroyPlayer hook).
void trigger(PlayLayer *playLayer);

// Re-reads the settings (call after changing any of them).
void reloadSettings();

} // namespace NoclipTint
