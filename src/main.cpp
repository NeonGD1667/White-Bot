#include "includes.hpp"

#include "hacks/layout_mode.hpp"
#include "pathfinder.hpp"
#include "practice_fixes/practice_fixes.hpp"
#include "ui/record_layer.hpp"
#include "hacks/fake_taps.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

$execute {

    geode::listenForSettingChanges<std::string>(
        "macro_accuracy",
        +[](std::string value) {
            auto& g = Global::get();

            g.frameFixes = false;
            g.inputFixes = false;

            if (value == "Frame Fixes")
                g.frameFixes = true;

            if (value == "Input Fixes")
                g.inputFixes = true;
        }
    );

    geode::listenForSettingChanges<int64_t>(
        "frame_fixes_limit",
        +[](int64_t value) {
            Global::get().frameFixesLimit = value;
        }
    );

    geode::listenForSettingChanges<bool>(
        "lock_delta",
        +[](bool value) {
            Global::get().lockDelta = value;
        }
    );

    geode::listenForSettingChanges<bool>(
        "auto_stop_playing",
        +[](bool value) {
            Global::get().stopPlaying = value;
        }
    );
};


class $modify(PlayLayer) {

    struct Fields {
        int delayedLevelRestart = -1;
    };

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& g = Global::get();

        if (
            m_fields->delayedLevelRestart != -1 &&
            m_fields->delayedLevelRestart >= Global::getCurrentFrame()
        ) {
            m_fields->delayedLevelRestart = -1;
            resetLevelFromStart();
        }
    }

    void onQuit() {
        if (
            Mod::get()->getSettingValue<bool>("disable_speedhack") &&
            Global::get().speedhackEnabled
        )
            Global::toggleSpeedhack();

        PlayLayer::onQuit();
    }

    void pauseGame(bool b1) {
        Global::updateKeybinds();

        if (!Global::get().renderer.tryPause())
            return;

        auto& g = Global::get();

        if (!m_player1 || !m_player2)
            return PlayLayer::pauseGame(b1);

        if (g.state != state::recording)
            return PlayLayer::pauseGame(b1);

        g.ignoreRecordAction = true;

        int frame = Global::getCurrentFrame() + 1;

        if (m_player1->m_holdingButtons[1]) {
            handleButton(false, 1, false);
            g.macro.inputs.push_back(input(frame, 1, false, false));
        }

        if (m_levelSettings->m_platformerMode) {

            if (m_player1->m_holdingButtons[2]) {
                handleButton(false, 2, false);
                g.macro.inputs.push_back(input(frame, 2, false, false));
            }

            if (m_player1->m_holdingButtons[3]) {
                handleButton(false, 3, false);
                g.macro.inputs.push_back(input(frame, 3, false, false));
            }
        }

        if (m_levelSettings->m_twoPlayerMode) {

            if (m_player2->m_holdingButtons[1]) {
                handleButton(false, 1, true);
                g.macro.inputs.push_back(input(frame, 1, true, false));
            }

            if (m_levelSettings->m_platformerMode) {

                if (m_player2->m_holdingButtons[2]) {
                    handleButton(false, 2, false);
                    g.macro.inputs.push_back(input(frame, 2, true, false));
                }

                if (m_player2->m_holdingButtons[3]) {
                    handleButton(false, 3, false);
                    g.macro.inputs.push_back(input(frame, 3, true, false));
                }
            }
        }

        g.ignoreRecordAction = false;

        PlayLayer::pauseGame(b1);
    }

    bool init(GJGameLevel* level, bool b1, bool b2) {
        auto& g = Global::get();

        g.firstAttempt = true;

        if (!PlayLayer::init(level, b1, b2))
            return false;

        Global::updateKeybinds();

        auto now = std::chrono::system_clock::now();

        g.currentSession =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()
            ).count();

        g.lastAutoSaveFrame = 0;

        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        auto& g = Global::get();

        int frame = Global::getCurrentFrame();

        if (!m_isPracticeMode)
            g.renderer.levelStartFrame = frame;

        if (
            g.restart &&
            m_levelSettings->m_platformerMode &&
            g.state != state::none
        )
            m_fields->delayedLevelRestart = frame + 2;

        Global::updateSeed(true);

        g.safeMode = false;

        if (g.layoutMode)
            g.safeMode = true;

        g.currentAction = 0;
        g.currentFrameFix = 0;
        g.restart = false;

        if (g.state == state::recording)
            Macro::updateInfo(this);

        if (
            (!m_isPracticeMode || frame <= 1 || g.checkpoints.empty()) &&
            g.state == state::recording &&
            !g.continueBotting
        ) {
            g.macro.inputs.clear();
            g.macro.frameFixes.clear();
            g.checkpoints.clear();

            g.macro.framerate = 240.f;

            if (g.layer)
                static_cast<RecordLayer*>(g.layer)->updateTPS();

            PlayerData p1Data =
                PlayerPracticeFixes::saveData(m_player1);

            PlayerData p2Data =
                PlayerPracticeFixes::saveData(m_player2);

            InputPracticeFixes::applyFixes(
                this,
                p1Data,
                p2Data,
                frame
            );

            Macro::resetVariables();

            m_player1->m_holdingRight = false;
            m_player1->m_holdingLeft = false;
            m_player2->m_holdingRight = false;
            m_player2->m_holdingLeft = false;

            m_player1->m_holdingButtons[2] = false;
            m_player1->m_holdingButtons[3] = false;
            m_player2->m_holdingButtons[2] = false;
            m_player2->m_holdingButtons[3] = false;
        }

        if (
            !m_levelSettings->m_platformerMode ||
            (
                !g.mod->getSavedValue<bool>(
                    "macro_always_practice_fixes"
                ) &&
                g.state != state::recording
            )
        )
            return;

        g.ignoreRecordAction = true;

        for (int i = 0; i < 4; i++) {

            bool player2 = !(sidesButtons[i] > 2);
            bool rightKey =
                sidesButtons[i] == 5 ||
                sidesButtons[i] == 2;

            if (g.heldButtons[sidesButtons[i]])
                handleButton(
                    true,
                    indexButton[sidesButtons[i]],
                    player2
                );
        }

        g.ignoreRecordAction = false;
    }
};


class $modify(BGLHook, GJBaseGameLayer) {

    struct Fields {
        bool macroInput = false;
    };

    void processCommands(
        float dt,
        bool isHalfTick,
        bool isLastTick
    ) {
        auto& g = Global::get();

        PlayLayer* pl = PlayLayer::get();

        auto* editorLayer =
            typeinfo_cast<LevelEditorLayer*>(this);

        bool editorPlaytest =
            !pl &&
            editorLayer &&
            editorLayer->m_playbackMode == PlaybackMode::Playing;

        if (!pl && !editorPlaytest) {
            return GJBaseGameLayer::processCommands(
                dt,
                isHalfTick,
                isLastTick
            );
        }

        if (editorPlaytest) {

            GJBaseGameLayer::processCommands(
                dt,
                isHalfTick,
                isLastTick
            );

            if (g.state != state::playing)
                return;

            int frame = Global::getCurrentFrame(true);

            g.previousFrame = frame;

            handlePlaying(frame);

            return;
        }

        Global::updateSeed();

        bool rendering =
            g.renderer.recording ||
            g.renderer.recordingAudio;

        if (g.state != state::none || rendering) {

            if (!g.firstAttempt) {
                g.renderer.dontRender = false;
                g.renderer.dontRecordAudio = false;
            }

            int frame = Global::getCurrentFrame();

            if (
                frame > 2 &&
                g.firstAttempt &&
                g.macro.DllBotMacro
            ) {
                g.firstAttempt = false;

                if (
                    (m_levelSettings->m_platformerMode || rendering) &&
                    !m_levelEndAnimationStarted
                )
                    return pl->resetLevelFromStart();
                else if (!m_levelEndAnimationStarted)
                    return pl->resetLevel();
            }

            if (
                g.previousFrame == frame &&
                frame != 0 &&
                g.macro.DllBotMacro
            )
                return GJBaseGameLayer::processCommands(
                    dt,
                    isHalfTick,
                    isLastTick
                );
        }

        GJBaseGameLayer::processCommands(
            dt,
            isHalfTick,
            isLastTick
        );

        if (
            g.state == state::none &&
            !PathFinder::isRunning()
        )
            return;

        int frame = Global::getCurrentFrame();

        g.previousFrame = frame;

        if (
            g.macro.DllBotMacro &&
            g.restart &&
            !m_levelEndAnimationStarted
        ) {
            if (
                m_levelSettings->m_platformerMode &&
                g.state != state::none
            )
                return pl->resetLevelFromStart();
            else
                return pl->resetLevel();
        }

        if (g.state == state::recording)
            handleRecording(frame);

        if (
            g.state == state::playing ||
            PathFinder::isRunning()
        )
            handlePlaying(Global::getCurrentFrame());
    }

    void handleRecording(int frame) {
        auto& g = Global::get();

        if (g.ignoreFrame != -1) {
            if (g.ignoreFrame < frame)
                g.ignoreFrame = -1;
        }

        bool twoPlayers =
            m_levelSettings->m_twoPlayerMode;

        if (g.delayedFrameInput[0] == frame) {
            g.delayedFrameInput[0] = -1;

            GJBaseGameLayer::handleButton(
                true,
                1,
                true
            );
        }

        if (g.delayedFrameInput[1] == frame) {
            g.delayedFrameInput[1] = -1;

            GJBaseGameLayer::handleButton(
                true,
                1,
                false
            );
        }

        if (
            frame > g.ignoreJumpButton &&
            g.ignoreJumpButton != -1
        )
            g.ignoreJumpButton = -1;

        for (int x = 0; x < 2; x++) {

            if (g.delayedFrameReleaseMain[x] == frame) {

                bool player2 = x == 0;

                g.delayedFrameReleaseMain[x] = -1;

                GJBaseGameLayer::handleButton(
                    false,
                    1,
                    twoPlayers ? player2 : false
                );
            }

            if (!m_levelSettings->m_platformerMode)
                continue;

            for (int y = 0; y < 2; y++) {

                if (g.delayedFrameRelease[x][y] == frame) {

                    int button = y == 0 ? 2 : 3;
                    bool player2 = x == 0;

                    g.delayedFrameRelease[x][y] = -1;

                    GJBaseGameLayer::handleButton(
                        false,
                        button,
                        player2
                    );
                }
            }
        }

        if (
            !g.frameFixes ||
            g.macro.inputs.empty()
        )
            return;

        if (!g.macro.frameFixes.empty()) {

            if (
                1.f / Global::getTPS() *
                (
                    frame -
                    g.macro.frameFixes.back().frame
                ) <
                1.f / g.frameFixesLimit
            )
                return;
        }

        g.macro.recordFrameFix(
            frame,
            m_player1,
            m_player2
        );
    }

    void handlePlaying(int frame) {
        auto& g = Global::get();

        PathFinder::onFrame(frame);

        if (m_levelEndAnimationStarted)
            return;

        if (m_player1->m_isDead) {
            m_player1->releaseAllButtons();
            m_player2->releaseAllButtons();
            return;
        }

        m_fields->macroInput = true;

        while (
            g.currentAction < g.macro.inputs.size() &&
            frame >= g.macro.inputs[g.currentAction].frame
        ) {
            auto input =
                g.macro.inputs[g.currentAction];

            if (frame != g.respawnFrame) {

                if (Macro::flipControls())
                    input.player2 = !input.player2;

                GJBaseGameLayer::handleButton(
                    input.down,
                    input.button,
                    input.player2
                );

                // Fake Taps:
                // chỉ tạo touch visual cho input do bot playback.
                auto* player =
                    input.player2
                        ? m_player2
                        : m_player1;

                if (player) {

                    auto position =
                        player->getParent()->convertToWorldSpace(
                            player->getPosition()
                        );

                    int fakePlayer =
                        input.player2 ? 1 : 0;

                    if (input.down)
                        FakeTaps::press(
                            fakePlayer,
                            position
                        );
                    else
                        FakeTaps::release(
                            fakePlayer
                        );
                }
            }

            g.currentAction++;
            g.safeMode = true;
        }

        g.respawnFrame = -1;
        m_fields->macroInput = false;

        // Neon Mod: Continue Botting
        if (
            g.continueFrame != -1 &&
            (int)frame >= g.continueFrame
        ) {
            int targetFrame = g.continueFrame;

            g.continueFrame = -1;

            auto& inputs = g.macro.inputs;

            inputs.erase(
                std::remove_if(
                    inputs.begin(),
                    inputs.end(),
                    [targetFrame](const input& inp) {
                        return (int)inp.frame > targetFrame;
                    }
                ),
                inputs.end()
            );

            auto& fixes = g.macro.frameFixes;

            fixes.erase(
                std::remove_if(
                    fixes.begin(),
                    fixes.end(),
                    [targetFrame](const gdr::FrameFix& fix) {
                        return fix.frame > targetFrame;
                    }
                ),
                fixes.end()
            );

            g.state = state::recording;
            g.continueBotting = false;
            g.continueBottingSpeedhack = false;

            Macro::updateTPS();

            Loader::get()->queueInMainThread([] {

                if (PlayLayer* pl = PlayLayer::get()) {

                    if (!pl->m_isPracticeMode)
                        pl->togglePracticeMode(true);

                    pl->markCheckpoint();

                    if (!pl->m_isPaused)
                        pl->pauseGame(false);
                }
            });

            log::info(
                "Continue Botting: switched to recording at frame {}",
                targetFrame
            );

            return;
        }

        if (g.currentAction == g.macro.inputs.size()) {

            if (g.stopPlaying) {
                Macro::togglePlaying();
                Macro::resetState(true);

                return;
            }
        }

        if (
            (!g.frameFixes && !g.inputFixes) ||
            !PlayLayer::get()
        )
            return;

        while (
            g.currentFrameFix < g.macro.frameFixes.size() &&
            frame >=
                g.macro.frameFixes[g.currentFrameFix].frame
        ) {
            auto& fix =
                g.macro.frameFixes[g.currentFrameFix];

            PlayerObject* p1 = m_player1;
            PlayerObject* p2 = m_player2;

            cocos2d::CCPoint pos1 =
                p1->getPosition();

            cocos2d::CCPoint pos2 =
                p2->getPosition();

            if (
                fix.p1.pos.x != 0.f &&
                fix.p1.pos.y != 0.f
            )
                p1->setPosition(fix.p1.pos);

            if (
                fix.p1.rotate &&
                fix.p1.rotation != 0.f
            )
                p1->setRotation(
                    fix.p1.rotation
                );

            if (m_gameState.m_isDualMode) {

                if (
                    fix.p2.pos.x != 0.f &&
                    fix.p2.pos.y != 0.f
                )
                    p2->setPosition(
                        fix.p2.pos
                    );

                if (
                    fix.p2.rotate &&
                    fix.p2.rotation != 0.f
                )
                    p2->setRotation(
                        fix.p2.rotation
                    );
            }

            g.currentFrameFix++;
        }
    }

    void handleButton(
        bool hold,
        int button,
        bool player2
    ) {
        auto& g = Global::get();

        if (
            g.p2mirror &&
            m_gameState.m_isDualMode &&
            !g.autoclicker
        ) {
            GJBaseGameLayer::handleButton(
                g.mod->getSavedValue<bool>(
                    "p2_input_mirror_inverted"
                )
                    ? !hold
                    : hold,
                button,
                !player2
            );
        }

        int frame =
            Global::getCurrentFrame();

        bool isRecording =
            g.state == state::recording;

        bool isPlaying =
            g.state == state::playing;

        bool isPathFinding =
            PathFinder::isRunning();

        bool isNone =
            g.state == state::none;

        // Initial check for ignore input during playback
        if (
            (isPlaying || isPathFinding) &&
            g.mod->getSavedValue<bool>(
                "macro_ignore_inputs"
            ) &&
            !m_fields->macroInput
        )
            return;

        if (
            isPathFinding &&
            !m_fields->macroInput
        )
            return;

        // Recording-specific ignore logic
        if (
            isRecording &&
            g.ignoreFrame != -1 &&
            hold
        )
            return;

        // Recording-specific delay logic
        if (isRecording) {

            bool isDelayedInput =
                g.delayedFrameInput[
                    (
                        m_levelSettings->m_twoPlayerMode
                            ? static_cast<int>(!player2)
                            : 0
                    )
                ] != -1;

            bool isDelayedRelease =
                g.delayedFrameReleaseMain[
                    (
                        m_levelSettings->m_twoPlayerMode
                            ? static_cast<int>(!player2)
                            : 0
                    )
                ] != -1;

            if (
                (
                    isDelayedInput ||
                    g.ignoreJumpButton == frame ||
                    isDelayedRelease
                ) &&
                button == 1
            ) {
                if (g.ignoreJumpButton >= frame)
                    g.delayedFrameInput[
                        (
                            m_levelSettings->m_twoPlayerMode
                                ? static_cast<int>(!player2)
                                : 0
                        )
                    ] =
                        g.ignoreJumpButton + 1;

                return;
            }
        }

        // Capture state before original call for recording
        if (
            isRecording &&
            g.inputFixes
        )
            g.macro.recordFrameFix(
                frame,
                m_player1,
                m_player2
            );

        // Original call
        GJBaseGameLayer::handleButton(
            hold,
            button,
            player2
        );

        // NakoMod: Auto Swift Click
        if (
            hold &&
            g.autoSwiftClickEnabled &&
            !g.autoSwiftClickProcessing
        ) {
            g.autoSwiftClickProcessing = true;

            int clicks =
                g.autoSwiftClickCount;

            if (clicks < 2)
                clicks = 2;

            for (int i = 1; i < clicks; i++) {

                GJBaseGameLayer::handleButton(
                    false,
                    button,
                    player2
                );

                GJBaseGameLayer::handleButton(
                    true,
                    button,
                    player2
                );

                if (
                    isRecording &&
                    !g.ignoreRecordAction &&
                    !g.creatingTrajectory &&
                    !m_player1->m_isDead
                ) {
                    g.macro.recordAction(
                        frame,
                        button,
                        player2,
                        false
                    );

                    g.macro.recordAction(
                        frame,
                        button,
                        player2,
                        true
                    );
                }
            }

            g.autoSwiftClickProcessing = false;
        }

        // Regular Recording
        if (
            isRecording &&
            !g.ignoreRecordAction &&
            !g.creatingTrajectory &&
            !m_player1->m_isDead
        ) {
            if (!m_levelSettings->m_twoPlayerMode)
                player2 = false;

            g.macro.recordAction(
                frame,
                button,
                player2,
                hold
            );

            if (
                g.p2mirror &&
                m_gameState.m_isDualMode
            ) {
                g.macro.recordAction(
                    frame,
                    button,
                    !player2,
                    g.mod->getSavedValue<bool>(
                        "p2_input_mirror_inverted"
                    )
                        ? !hold
                        : hold
                );
            }
        }
    }
};


class $modify(DllBotEditorUI, EditorUI) {

    void onPlaytest(CCObject* sender) {
        auto& g = Global::get();

        if (
            m_editorLayer &&
            m_editorLayer->m_playbackMode ==
                PlaybackMode::Not
        ) {
            g.currentAction = 0;
            g.currentFrameFix = 0;
            g.previousFrame = 0;
            g.respawnFrame = -1;

            Macro::resetVariables();

            if (g.state == state::playing)
                m_editorLayer->m_gameState.m_currentProgress = 0;
        }

        EditorUI::onPlaytest(sender);
    }
};


class $modify(PauseLayer) {

    void onPracticeMode(CCObject* sender) {
        PauseLayer::onPracticeMode(sender);

        if (Global::get().state != state::none)
            PlayLayer::get()->resetLevel();
    }

    void onNormalMode(CCObject* sender) {
        PauseLayer::onNormalMode(sender);

        auto& g = Global::get();

        g.checkpoints.clear();

        if (g.restart) {
            if (PlayLayer* pl = PlayLayer::get())
                pl->resetLevel();
        }
    }

    void onQuit(CCObject* sender) {

        if (
            Mod::get()->getSavedValue<bool>(
                "macro_confirm_on_exit"
            )
        ) {
            geode::createQuickPopup(
                "Confirm Exit",
                "Are you sure you want to <cr>exit</c> the level?",
                "Cancel",
                "Exit",
                [](auto, bool btn2) {

                    if (btn2) {
                        Macro::resetState();

                        auto& g = Global::get();

                        if (g.renderer.recording)
                            g.renderer.stop();

                        if (g.renderer.recordingAudio)
                            g.renderer.stopAudio();

                        if (PlayLayer* pl = PlayLayer::get())
                            pl->onQuit();
                    }
                }
            );

            return;
        }

        PauseLayer::onQuit(sender);

        Macro::resetState();

        Loader::get()->queueInMainThread([] {

            auto& g = Global::get();

            if (g.renderer.recording)
                g.renderer.stop();

            if (g.renderer.recordingAudio)
                g.renderer.stopAudio();
        });
    }

    void goEdit() {

        if (
            Mod::get()->getSavedValue<bool>(
                "macro_confirm_on_exit"
            )
        ) {
            geode::createQuickPopup(
                "Confirm Edit",
                "Are you sure you want to <cy>edit</c> the level?",
                "Cancel",
                "Edit",
                [](auto, bool btn2) {

                    if (btn2) {
                        Macro::resetState();

                        auto& g = Global::get();

                        if (g.renderer.recording)
                            g.renderer.stop();

                        if (g.renderer.recordingAudio)
                            g.renderer.stopAudio();

                        if (PlayLayer* pl = PlayLayer::get()) {

                            auto level = pl->m_level;

                            auto editorLayer =
                                LevelEditorLayer::create(
                                    level,
                                    false
                                );

                            auto scene =
                                CCScene::create();

                            scene->addChild(
                                editorLayer
                            );

                            CCDirector::sharedDirector()
                                ->replaceScene(
                                    CCTransitionFade::create(
                                        0.5f,
                                        scene
                                    )
                                );
                        }
                    }
                }
            );

            return;
        }

                 PauseLayer::goEdit();

        Macro::resetState();

        Loader::get()->queueInMainThread([] {

            auto& g = Global::get();

            if (g.renderer.recording)
                g.renderer.stop();

            if (g.renderer.recordingAudio)
                g.renderer.stopAudio();
        });
    }
};
