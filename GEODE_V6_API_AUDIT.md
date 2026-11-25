# Geode v6 API audit — White Bot

Target: Geode **v6.0.0** (`fcbf84c`), bindings **c46bcdf**, GD **2.2091**.
All "before" snippets come from `NeonBrach` @ `9726d22`; "after" is this
branch. Headers were read from the pinned SDK checkout, not from alpha docs.

## 1. geode::TextInput

### What changed in v6

| v5 API | v6 API | Notes from `loader/include/Geode/ui/TextInput.hpp` |
| --- | --- | --- |
| `TextInputDelegate` + `setDelegate(this)` | `setCallback(Function<void(std::string const&)>)` | delegate/`textChanged(CCTextInputNode*)` no longer exist |
| `getInputNode()` (`CCTextInputNode*`) | — | wrapper removed; `TextInput` is a `CCNode` itself |
| `getInputNode()->setAllowedChars(s)` | `setFilter(std::string)` / `setCommonFilter(CommonFilter)` | filter is parsed as UTF-8 into UTF-32 |
| `getInputNode()->setMaxLabelLength(n)` | `setMaxCharCount(size_t)` | truncates existing text; **does not** emit a text-changed event |
| `getInputNode()->setString(s)` | `setString(std::string_view, bool triggerCallback = false)` | early-returns when the text is unchanged; no callback by default |
| manual IME detach + caret hiding | `unfocus()` / `focus()` | `TextInput` owns placeholder and caret |

Two consequences matter for behaviour preservation:

- `setMaxCharCount()` truncates but calls `syncLabel()` with event emission
  off, so it never re-enters our callbacks. The old `setMaxLabelLength()` was
  a label-only limit; migrating does not introduce a spurious save.
- `setString(value)` keeps `triggerCallback = false` and skips the event when
  the value is identical. Initial values therefore do not overwrite saved
  settings, exactly like the v5 code. Where the old code deliberately synced
  after `setString`, the call remains explicit (`textChanged()`).

### Per-file mapping

| File | Inputs | Before | After |
| --- | --- | --- | --- |
| `src/ui/autoclicker_settings_layer.hpp` | `holdInput`, `releaseInput`, `holdInput2`, `releaseInput2` | shared `textChanged(CCTextInputNode*)`, pointer comparison, `setAllowedChars("0123456789")`, `setMaxLabelLength(4)` | four `setCallback([this](std::string const&){ textChanged(); })`, `setFilter("0123456789")`, `setMaxCharCount(4)` |
| `src/ui/autosave_settings_layer.hpp` | `intervalInput` | `setDelegate(this)`, `setAllowedChars("0123456789.")`, max 4 | per-input callback, `setFilter("0123456789.")`, `setMaxCharCount(4)`; update path uses `unfocus()` |
| `src/ui/auto_swift_click_settings_layer.hpp` | `countInput` | delegate + allowed chars + max 2 | callback, filter, `setMaxCharCount(2)` |
| `src/ui/swift_click_settings_layer.hpp` | `countInput` (max 2), `keyInput` (max 3) | one delegate for both, pointer comparison | separate callback per input; filters and limits preserved |
| `src/ui/fake_taps_settings_layer.hpp` | `scaleInput` | delegate + `"0123456789."` | callback + filter; `textChanged(nullptr)` -> `textChanged()` |
| `src/ui/noclip_settings_layer.hpp` | `opacityInput`, `timeInput` (via `addNumberInput`) | shared delegate, pointer comparison against both inputs | `addNumberInput(..., key, maxValue)` attaches a callback that calls `saveNumber(key, maxValue)`; `saveNumber` resolves its own input from the key. Per-input dispatch replaces the pointer comparison; no "other input is null" bail-out is needed |
| `src/ui/pathfinder_settings_layer.hpp` | 6 numeric fields (`stepInput`, `shiftInput`, `actionsInput`, `warmupInput`, `startFrameInput`, `searchWindowInput`) | delegate + allowed chars + max length per field | `addInput` attaches one callback per field; filter `"0123456789"`; `setMaxCharCount(maxLength)` |
| `src/ui/trajectory_settings_layer.hpp` | `input` (trajectory length) | delegate + digits | callback + filter; `textChanged(nullptr)` -> `textChanged()` |
| `src/ui/macro_editor.cpp/.hpp` | `pageInput` (max 5), `frameInput` (max 8) | `textChanged(CCTextInputNode*)` compared pointers | `onPageInput(std::string const&)`, `onFrameInput(std::string const&)`; empty text and `page <= 0` guards preserved |
| `src/ui/macro_timeline_layer.cpp/.hpp` | `frameInput` (max 8) | delegate + pointer check + digits | `onFrameInput(std::string const&)`; still falls back to `currentFrame` on unparsable text |
| `src/ui/load_macro_layer.cpp/.hpp` | `searchInput` | `setDelegate(this)`, `textChanged(CCTextInputNode*)` reads `node->getString()` | `setCallback([this](std::string const& text){ onSearchInput(text); })`; clear button logic unchanged |
| `src/ui/render_settings_layer.cpp/.hpp` | `fadeInInput`, `fadeOutInput` (digits + dot), `extensionInput` (letters incl. `ñ`, digits, dot) | delegate on each geode::TextInput | callbacks call `saveRenderInputs()`; filters preserved byte-for-byte |

The vanilla Geometry Dash `CCTextInputNode` fields in
`render_settings_layer` (`args`, `audio args`, `video args`, `sec`) are still
`CCTextInputNode::create(...)`, so they keep their
`textChanged(CCTextInputNode*)` override. Only the geode::TextInput fields
moved to callbacks. Shared persistence was factored into
`saveRenderInputs()` and is called by both paths.

### Callback order, filtering, limits

- Every migrated callback is registered **after** the initial `setString()`
  in the same setup block; because `setString` does not fire by default, no
  save happens during construction.
- Multiple inputs that used to share one delegate now have independent
  lambdas. The lambda copies the identifying key (noclip) or calls a distinct
  handler (macro editor/timeline/load), so identical values in two fields
  cannot be confused.
- Filters are byte-for-byte identical, including the legacy
  `"abcdefghijklmnñopqrstuvwxyz0123456789."` extension filter.
- `unfocus()` replaces the old `detachWithIME()` / caret-hiding blocks in
  `updateInputs()`; the popup still closes the keyboard on OK.

## 2. Popup / Button

| v5 API | v6 API | Files |
| --- | --- | --- |
| `Popup::m_closeBtn->getNormalImage()` | `m_closeBtn->getDisplayNode()` (`geode::Button`) | `src/ui/load_macro_layer.cpp`, `src/ui/macro_index_popup.hpp`, `src/ui/record_layer.cpp` |
| `Popup` ctor with `m_bgSprite` etc. | unchanged `Popup(size, size)` + `init(width, height, bg, CCRect)` | all popups |
| `m_title`, `m_buttonMenu` | still available (`m_title` created lazily by `setTitle`) | all popups |

Scale values are unchanged: `0.6f` in load/index, `0.575f` in record.
Remaining `getNormalImage()` calls in the tree are on vanilla
`CCMenuItemSpriteExtra`, which still has that method.

## 3. GD 2.2091 binding changes

`PlayerObject` fields used by the practice snapshot:

| Field | v5 | 2.2091 |
| --- | --- | --- |
| `m_stateUnk` | `unsigned char` | `float`, alias of unused `m_unusedFloatA80` |
| `m_stateNoStickX` | `unsigned char` | **removed** |
| `m_stateNoStickY` | `unsigned char` | **removed** |
| `m_stateUnk2` | `unsigned char` | **removed** |

`src/macro.hpp` (`PlayerData`) and `src/practice_fixes/player.cpp`
(`saveData` / `applyData`) were updated accordingly. All other snapshot
fields are unchanged.

## 4. WebRequest timeout

`WebRequest::timeout` now takes `asp::Duration`.

| File | Before | After |
| --- | --- | --- |
| `src/download/version_manager.cpp:412` | `std::chrono::seconds(20)` | `asp::Duration::fromSecs(20)` |
| `src/download/version_manager.cpp:475` | `std::chrono::seconds(180)` | `asp::Duration::fromSecs(180)` |

## 5. MDTextArea audit

Two call sites, both unchanged and valid in v6:

- `src/ui/macro_info_layer.hpp:74` — `MDTextArea::create(std::string, CCSize)`
- `src/download/version_manager.cpp:917` — same overload

v6 declares `create(std::string, float)`, `create(std::string, CCSize)`,
and config-taking variants. No editing/scrolling/persistence API used by
White Bot changed; the default configuration is kept, so rendering and
scrolling behaviour is untouched.

## 6. Filesystem and error types audit

- `src/utils/fs_compat.hpp` already wraps Geode v6's `asp::fs` in the old
  `bool` / optional style. Helpers used by the code: `toStd`, `filename`,
  `extension` (re-adds the dot), `stem`, `parent`, `remove`, `rename`,
  `createDirs`.
- `asp::fs` surface used: `Path` (73), `exists` (35), `rename` (4),
  `readToString` (2), `fileSize` (2), `remove`, `PathView`, `iterDir`,
  `createDirAll`.
- `std::filesystem` remains only where a real std path or stream is required:
  `replay/gdr.*`, `replay/json.hpp`, the FFmpeg backend, and `fs_compat`.
  It was **not** blindly replaced.
- Error handling keeps `asp::Result` (`isErr`, `unwrapOr`, `error().code`)
  and `geode::utils::numFromString<T>` (`NumParseError`) at the call sites
  that already used them.
- All of this compiles on Android arm64/armv7 against asp `v1.6.4`.

## 7. Events, keybinds and hooks

- Keybind registration is Geode's built-in `KeybindSettingV3` API:
  five `listenForKeybindSettingPresses` calls in `$on_mod(Loaded)`
  (`keybind_open_menu`, `keybind_toggle_macro`, `keybind_step_forward`,
  `keybind_toggle_stepper`, `keybind_macro_swapper`). No removed
  `EventFilter`/`EventListener` remains.
- `geode.custom-keybinds` is not used (incompatible since v5).
- Settings keep their keybind JSON values in `mod.json`; nothing was
  re-registered or reordered.
- Hooks are declared with `class $modify(...)` and override virtuals with
  stable signatures. 54 hook classes in 23 files across gameplay/UI/hacks
  were compile-verified against the 2.2091 bindings on Android.
- `CCKeyboardDispatcher::dispatchKeyboardMSG` is guarded by
  `#if !defined(GEODE_IS_IOS)` (pre-existing: the generated iOS binding for
  that class did not compile with the older SDK).
- Pre-existing keybind gaps, unchanged by this migration:
  `handleHoldForward` is defined but not registered, and the legacy
  `g.keybinds` / `g.allKeybinds` arrays are unused because `updateKeybinds()`
  is still a TODO.

## 8. Remaining obsolete-API scan

`rg` over `src/`:

- `getInputNode`: 0
- `setDelegate` / `setAllowedChars` / `setMaxLabelLength` on geode::TextInput:
  0 (only explanatory comments mention `TextInputDelegate`)
- `getNormalImage()` on `Popup::m_closeBtn`: 0
- `RichTextArea`, `gd::string`, `Task.hpp`, `Signal.hpp`: 0
- `CCTextInputNode`: 44 references, all vanilla GD text fields or comments
