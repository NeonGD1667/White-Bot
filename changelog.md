# DLL Bot - White Bot

## v3.1.0

### Added

- Added iOS support.
- Added Macro Index (about 90% complete, not fully tested yet).
- Added "Check Updates On Startup" setting.
- Added a changelog window (with an Install button) for every version in the Version Manager.

### Improved

- Upgraded Geode SDK from 5.9.0 to 5.11.2.
- Rendering on Windows and macOS is now about twice as fast.
- Redesigned the Version Manager: it now shows the channel, release date, and Latest / Current labels, with an info button for each version.
- Aligned the Settings buttons (Open Menu, Downgrade, update controls) with the rest of the settings.

### Fixed

- Fixed the Downgrade button not opening anything.
- Fixed FMOD_RESULT in iOS build.
- Fixed SDK target.

> Still supports Geometry Dash 2.2081 only. 2.2091 is not supported yet.

---

## v3.0.1 — Pre-release

### Added

- Added Macro Index (WIP again?).
- Added additional UI improvements.

### Fixed

- Fixed various bugs and stability issues.
- Fixed build issues with C++23 and Android ARMv7.

### Improved

- Improved overall stability.
- Improved macro handling.
- Improved UI and small details.

> DLL Bot v3.0.1 — Final pre-release before the White Bot rebrand. 🗿

---

## v3.0.0

### Added

- Added GDR2 macro format.
- Added SLC2 macro format.
- Added SLC3 macro format.
- Added Macro Index (WIP).
- Added RGB UI (Testing).
- Added format selection in Save Macro.
- Added SLC macro loading support.

### Fixed

- Fixed Android ARMv7 compilation issues.
- Fixed frame type handling when exporting macros.
- Fixed SLC atom size handling on 32-bit platforms.

### Improved

- Improved macro format compatibility.
- Improved the macro saving workflow.
- Improved UI for selecting macro formats.

> DLL Bot v3.0.0 — Macro formats are getting serious. 🗿

---

## v1.1.2

- **Add:** GDR2 (`.gdr2`) macro format — export and import support.
- **Add:** Format selector when saving macros (GDR / JSON / GDR2).
- **Fix:** Build error on GD 2.2081 (missing `m_dashFireFrame` in `PlayerData`).
- **Fix:** Platformer mode not saved/loaded correctly in GDR2 macros.

---

## v1.0.0

### Added

- Added HDR rendering (Libx265 / HEVC_NVENC).
- Added menu background blur (requires mod).
- Added custom keybinds for Frame Stepper.
- Added Frame Stepper Hold functionality with mobile on-screen button support.
- Added Telegram channel icon to the bot menu.
- Added `.gdr2` macro import support.
- Added built-in custom keybind support for opening the menu and toggling the macro.
- Added warning toast for legacy `.gdr` macros not recorded with DLL Bot.
- Added macro playback support during editor playtest.
- Added DLL Bot menu button to the editor pause layer.
- Added option to disable Auto Safe Mode.
- Added PathFinder (TEST).
- Added Ghost Playback (TEST).
- Added Auto Swift Click.
- Added Seamless Hold Continuity to prevent input drops when loading macros during holds.
- Added SpeedHack audio support.
- Added Speed Audio Sync.
- Completely rewrote the rendering system.
- Integrated Mobile FFmpeg API for native Android rendering.
- Added Confirm to Exit/Edit dialogs.
- Updated avatar/logo.
- Added CBF (Click Between Frames) and CBS support.
- Added Fast PBO rendering.
- Added Async Queue for rendering.
- Added Hardware Acceleration.
- Added Manual C++ VFlip.
- Added Macro Continue Botting.

### Improved

- Reworked the core botting system for improved stability.
- Ported the bot to Geometry Dash 2.2081.
- Rewrote the macro system from scratch.
- Updated Geode SDK targeting to v5.4.1.
- Improved cross-platform compilation and Geode v5 compatibility.

### Fixed

- Fixed various rendering, audio synchronization, macro playback, Frame Stepper, mobile, UI, and stability issues.
- Optimized the exact 1-frame stepper engine.
