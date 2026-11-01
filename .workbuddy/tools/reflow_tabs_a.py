"""设置卡片 tab1/tab2/tab3 重排成两栏（每栏自带标题条）。"""
import io

P = 'main.cpp'
lines = io.open(P, encoding='utf-8').read().split('\n')


def find(pred, start=0):
    for i in range(start, len(lines)):
        if pred(lines[i]):
            return i
    raise RuntimeError('anchor not found')


TAB3 = r'''                // 判定：左边判定窗口，右边长条 / Flick / 生命（2026-11-01 重排）。
                // 五条窗口滑杆原来横着两列摊开，把「判定窗口」这个组拆得七零八落；
                // 现在一个组一栏，从上往下读就是一台判定梯子。
                static float perfect = 70.0f;
                static float great = 120.0f;
                static float good = 170.0f;
                static float bad = 230.0f;
                static float miss = 230.0f;
                static float holdTail = 180.0f;
                static float holdStart = 140.0f;
                static int judgePreset = 0; // -1 = the sliders were dragged
                static int holdPreset = 0;
                static bool linkBadMiss = true;
                // The working copy is rebuilt on the first frame this page is
                // drawn in a card session, and never in the middle of a drag
                // (it is redrawn every frame then). Gating on "the card just
                // opened" instead - which is what this used to do - silently
                // missed the common case of a card opened on 演奏 and switched
                // to 判定 later, and it left 账户 > 导入 (which replaces the
                // profile under the card) pushing its pre-import numbers back
                // into the engine on the first slider nudge.
                static int judgePageLastFrame = -1000;
                const bool judgeFresh = ImGui::GetFrameCount() - judgePageLastFrame > 1;
                judgePageLastFrame = ImGui::GetFrameCount();
                if (judgeFresh) {
                    const game::JudgementWindows& cur = judgement.windows();
                    perfect = cur.perfectMs;
                    great = cur.greatMs;
                    good = cur.goodMs;
                    bad = cur.badMs;
                    miss = cur.missAfterMs;
                    holdTail = cur.holdTailGraceMs;
                    holdStart = cur.holdStartGraceMs;
                    linkBadMiss = userSettings.linkBadMiss;
                    judgePreset = -1;
                    for (int i = 0; i < 3; ++i) {
                        if (std::fabs(perfect - kJudgePresets[i][0]) < 1.0f
                            && std::fabs(great - kJudgePresets[i][1]) < 1.0f
                            && std::fabs(good - kJudgePresets[i][2]) < 1.0f
                            && std::fabs(bad - kJudgePresets[i][3]) < 1.0f
                            && std::fabs(miss - kJudgePresets[i][4]) < 1.0f) {
                            judgePreset = i;
                        }
                    }
                    holdPreset = -1;
                    for (int i = 0; i < 3; ++i) {
                        if (std::fabs(holdTail - kHoldGracePresets[i][0]) < 1.0f
                            && std::fabs(holdStart - kHoldGracePresets[i][1]) < 1.0f) {
                            holdPreset = i;
                        }
                    }
                }
                bool windowsChanged = false;
                twoCols(
                    [&](float w) {
                        colSet(0);
                        ui::sectionLabel("判定窗口 (ms)", w);
                        // 判定预设: a shorthand for the five sliders below. Picking one
                        // sets all five at once; dragging any slider walks the selection
                        // off to -1 (nothing highlighted), which is what the "custom"
                        // state looks like.
                        colSet(0);
                        if (ui::radioRow("judgepreset", kJudgePresetLabels, &judgePreset, w)) {
                            const int idx = std::clamp(judgePreset, 0, 2);
                            perfect = kJudgePresets[idx][0];
                            great = kJudgePresets[idx][1];
                            good = kJudgePresets[idx][2];
                            bad = kJudgePresets[idx][3];
                            miss = kJudgePresets[idx][4];
                            windowsChanged = true;
                        }
                        bool windowsDragged = false;
                        colSet(0);
                        windowsDragged |= ui::slider("perfect", &perfect, 10.0f, 100.0f, 1.0f, "Perfect %.0f", w);
                        colSet(0);
                        windowsDragged |= ui::slider("great", &great, 20.0f, 200.0f, 1.0f, "Great %.0f", w);
                        colSet(0);
                        windowsDragged |= ui::slider("goodw", &good, 30.0f, 260.0f, 1.0f, "Good %.0f", w);
                        good = std::max(good, great + 10.0f);
                        colSet(0);
                        windowsDragged |= ui::slider("badw", &bad, 40.0f, 400.0f, 5.0f, "Bad %.0f", w);
                        bad = std::max(bad, good + 10.0f);
                        colSet(0);
                        windowsDragged |= ui::slider("missw", &miss, 40.0f, 500.0f, 5.0f, "Miss %.0f", w,
                            /*enabled=*/!linkBadMiss);
                        miss = std::max(miss, good + 10.0f);
                        colSet(0);
                        bool linkBox = linkBadMiss;
                        ui::checkBox("Bad 与 Miss 同步", &linkBox, w);
                        if (linkBox != linkBadMiss) {
                            linkBadMiss = linkBox;
                            userSettings.linkBadMiss = linkBox;
                            if (linkBox) {
                                miss = bad; // linked: the two are one number
                            }
                            persistUserData();
                        }
                        if (linkBadMiss) {
                            miss = bad;
                        }
                        // Any slider (and only a slider) takes the row off its preset -
                        // the value is no longer the official one.
                        if (windowsDragged) {
                            windowsChanged = true;
                            judgePreset = -1;
                        }
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("长条容错 (ms)", w);
                        // Same pick-one shape as the judgement presets above. -1 = the
                        // current values match no preset, i.e. 松手/起按 were dragged by
                        // hand (then no circle is lit).
                        colSet(1);
                        if (ui::radioRow("holdpreset", kHoldPresetLabels, &holdPreset, w)) {
                            const int target = std::clamp(holdPreset, 0, 2);
                            holdTail = kHoldGracePresets[target][0];
                            holdStart = kHoldGracePresets[target][1];
                            windowsChanged = true;
                        }
                        bool tailChanged = false;
                        bool startChanged = false;
                        colSet(1);
                        tailChanged = ui::slider("holdtail", &holdTail, 20.0f, 300.0f, 10.0f, "松手容错 %.0f", w);
                        colSet(1);
                        startChanged = ui::slider("holdstart", &holdStart, 20.0f, 300.0f, 10.0f, "起按容错 %.0f", w);
                        if (tailChanged || startChanged) {
                            windowsChanged = true;
                            holdPreset = -1; // dragged off the presets
                        }
                        // Read straight off the engine every frame instead of in a static:
                        // 账户 > 导入 can replace these two under the card, and a stale
                        // copy would push its own value back on the next frame.
                        colSet(1);
                        ui::sectionLabel("Flick", w);
                        bool strictFlick = judgement.strictFlick();
                        bool flickAsTap = judgement.flickAsTap();
                        colSet(1);
                        ui::checkBox("严格 Flick 方向", &strictFlick, w);
                        if (strictFlick != judgement.strictFlick()) {
                            judgement.setStrictFlick(strictFlick);
                            userSettings.strictFlick = strictFlick;
                            persistUserData();
                        }
                        colSet(1);
                        ui::checkBox("Flick 视作 Tap", &flickAsTap, w);
                        if (flickAsTap != judgement.flickAsTap()) {
                            judgement.setFlickAsTap(flickAsTap);
                            userSettings.flickAsTap = flickAsTap;
                            persistUserData();
                        }
                        // Starting life. The engine seeds every session's stats from its
                        // own copy on reset(), so pushing it here (and once at boot) is
                        // enough.
                        colSet(1);
                        ui::sectionLabel("生命", w);
                        float life = judgement.initialLife();
                        colSet(1);
                        if (ui::slider("initlife", &life, 100.0f, game::kMaxInitialLife, 50.0f, "%.0f", w)) {
                            const float next = std::clamp(life, 100.0f, game::kMaxInitialLife);
                            if (next != userSettings.initialLife) {
                                userSettings.initialLife = next;
                                judgement.setInitialLife(next);
                                persistUserData();
                            }
                        }
                    });
                // Keep the ladder monotonic: a GREAT window narrower than
                // PERFECT (or a GOOD one narrower than GREAT) would silently
                // delete a judgement tier. Built upwards from PERFECT so the
                // result can never satisfy lo > hi (which std::clamp hates).
                perfect = std::clamp(perfect, 10.0f, 100.0f);
                great = std::max(great, perfect + 10.0f);
                good = std::max(good, great + 10.0f);
                bad = std::max(bad, good + 10.0f);
                if (linkBadMiss) {
                    miss = bad;
                } else {
                    miss = std::max(miss, good + 10.0f);
                }
                if (windowsChanged) {
                    game::JudgementWindows windows;
                    windows.perfectMs = perfect;
                    windows.greatMs = great;
                    windows.goodMs = good;
                    windows.badMs = bad;
                    windows.missAfterMs = miss;
                    windows.holdTailGraceMs = holdTail;
                    windows.holdStartGraceMs = holdStart;
                    judgement.setWindows(windows);
                    // Mirror into the profile immediately so closing the game
                    // never loses a window tweak.
                    userSettings.perfectMs = perfect;
                    userSettings.greatMs = great;
                    userSettings.goodMs = good;
                    userSettings.badMs = bad;
                    userSettings.missMs = miss;
                    userSettings.holdTailGraceMs = holdTail;
                    userSettings.holdStartGraceMs = holdStart;
                    persistUserData();
                }'''

TAB2 = r'''                // 外观：每块自带标题条（2026-11-01 重排，同演奏页）。
                // 画面页原来 16 项、要滚 340px；拆开之后两页都放得下。
                twoCols(
                    [&](float w) {
                        colSet(0);
                        ui::sectionLabel("界面", w);
                        colSet(0);
                        bool showProgressBox = showProgressBar;
                        ui::checkBox("显示播放进度条", &showProgressBox, w);
                        if (showProgressBox != showProgressBar) {
                            showProgressBar = showProgressBox;
                            persistUserData();
                        }
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("开屏", w);
                        colSet(1);
                        // Splash style: static image (0) vs classic progress bar (1).
                        // Only read at startup, so a change takes effect next launch.
                        bool classicSplashBox = userSettings.splashStyle != 0;
                        ui::checkBox("经典开屏画面", &classicSplashBox, w);
                        if (classicSplashBox != (userSettings.splashStyle != 0)) {
                            userSettings.splashStyle = classicSplashBox ? 1 : 0;
                            persistUserData();
                        }
                    });
                twoCols(
                    [&](float w) {
                        colSet(0);
                        ui::sectionLabel("背景", w);
                        colSet(0);
                        // Song-select background: the built-in gradient, the user's
                        // desktop wallpaper (blurred + dimmed), or nothing at all so
                        // the window itself is see-through (Aero glass on Win7).
                        ImGui::Text("选曲背景");
                        static int bgMode = userSettings.bgStyle;
                        colSet(0);
                        ImGui::SetNextItemWidth(w);
                        // The Aero entry only exists where Aero does - see aeroGlassAvailable.
                        const std::vector<std::string> bgItems = aeroGlass
                            ? std::vector<std::string>{"默认渐变", "桌面壁纸", "Aero 玻璃"}
                            : std::vector<std::string>{"默认渐变", "桌面壁纸"};
                        const int bgCount = aeroGlass ? 3 : 2;
                        const int bgSel = std::clamp(bgMode, 0, bgCount - 1);
                        bool bgPicked = ui::combo("##bgstyle", bgItems[static_cast<std::size_t>(bgSel)].c_str(),
                            bgItems, &bgMode, w);
                        bgPicked |= ui::padComboNudge(&bgMode, bgCount);
                        if (bgPicked) {
                            userSettings.bgStyle = bgMode;
                            // The frameless choice lives in the profile but is only in force
                            // while the background is the Aero one (see the declaration).
                            glassMode = userSettings.bgStyle == 2 ? std::clamp(userSettings.glassMode, 0, 2) : 0;
                            refreshSelectBackdrop();
                            // The play screen shares the setting, and switching it has
                            // to touch the window too: the frame is what makes the
                            // client area see-through in the first place.
                            renderer.setTransparentBackground(userSettings.bgStyle == 2);
#ifdef _WIN32
                            // Only the Aero background needs an extended frame.
                            requestGlassWindowMode(userSettings.bgStyle == 2);
#endif
                            persistUserData();
                        }
                        if (userSettings.bgStyle == 1) {
                            colSet(0);
                            ImGui::Text("背景模糊");
                            // Re-blurring the picture is a CPU pass over a decoded
                            // wallpaper, so it runs when the slider is let go, not
                            // on every frame of the drag.
                            static bool blurPending = false;
                            float bgBlurV = userSettings.bgBlur;
                            colSet(0);
                            if (ui::slider("背景模糊", &bgBlurV, 0.0f, 1.0f, 0.05f, "%.2f", w)) {
                                userSettings.bgBlur = bgBlurV;
                                blurPending = true;
                            }
                            if (blurPending && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                                blurPending = false;
                                refreshSelectBackdrop();
                                persistUserData();
                            }
                            colSet(0);
                            ImGui::Text("背景变暗");
                            float bgDimV = userSettings.bgDim;
                            colSet(0);
                            if (ui::slider("背景变暗", &bgDimV, 0.0f, 1.0f, 0.02f, "%.2f", w)) {
                                userSettings.bgDim = bgDimV;
                                // Cheap: only the overlay drawn on top changes.
                                game::setSelectBackdrop(backdropTex, backdropW, backdropH, bgDimV);
                                persistUserData();
                            }
                        }
                    },
                    [&](float w) {
                        if (!aeroGlass || userSettings.bgStyle != 2) {
                            return;
                        }
                        colSet(1);
                        ui::sectionLabel("窗口玻璃", w);
                        colSet(1);
                        // How far to go with the window chrome; see AGENTS.md「窗口外观」.
                        ImGui::Text("玻璃实现");
                        colSet(1);
                        // 0 and 2 only: the middle idea (ask DWM to skip the non-client
                        // area) falls back to the Basic frame on Windows 7, which is
                        // worse than what it removes. See AGENTS.md.
                        static int glassModeUi = userSettings.glassMode == 2 ? 1 : 0;
                        static const std::vector<std::string> kGlassItems = {
                            "extend frame", "无边框窗口"};
                        const int glassSel = std::clamp(glassModeUi, 0, 1);
                        bool glassPicked = ui::combo("##glassmode", kGlassItems[glassSel].c_str(),
                            kGlassItems, &glassModeUi, w);
                        glassPicked |= ui::padComboNudge(&glassModeUi, 2);
                        if (glassPicked) {
                            const int chosen = glassModeUi == 1 ? 2 : 0;
                            userSettings.glassMode = chosen;
                            glassMode = chosen; // the helper reads this one
#ifdef _WIN32
                            requestGlassWindowMode(true); // applied between frames, not here
#endif
                            persistUserData();
                        }
                    });
                twoCols(
                    [&](float w) {
                        colSet(0);
                        ui::sectionLabel("打击特效", w);
                        colSet(0);
                        // 弱化打击特效: pjsk's light effect profile. Only the basic
                        // layer of a hit burst survives - the judgement spark and a
                        // flick's flash - so the lane lights, the auras drawn under
                        // the notes and the particles a long note burns while held
                        // all go away. It is also the single biggest cut in
                        // per-frame overdraw, which is what a weak integrated GPU
                        // feels first. Takes effect on the next spawned particle.
                        bool simpleFxBox = simpleEffects;
                        ui::checkBox("弱化打击特效", &simpleFxBox, w);
                        if (simpleFxBox != simpleEffects) {
                            simpleEffects = simpleFxBox;
                            core_api::setSimpleEffect(simpleEffects);
                            persistUserData();
                        }
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("系统触摸", w);
                        colSet(1);
                        bool hideTouchBox = hideTouchFeedback;
                        ui::checkBox("隐藏系统触摸特效", &hideTouchBox, w);
                        if (hideTouchBox != hideTouchFeedback) {
                            hideTouchFeedback = hideTouchBox;
#ifdef _WIN32
                            applyTouchFeedback(window, hideTouchFeedback);
#endif
                            persistUserData();
                        }
                    });'''

TAB1 = r'''                // 画面：两块并排，每块自带标题条（2026-11-01 重排）。
                // 原来两条通栏灰条一路撑满 864px，控件也被拉成细长一条。
                static constexpr int kResCount = 10;
                static constexpr int kResW[kResCount] = {640, 800, 960, 1024, 1120, 1280, 1366, 1600, 1920, 2560};
                static constexpr int kResH[kResCount] = {360, 450, 540, 576, 630, 720, 768, 900, 1080, 1440};
                // Applies the chosen size: the window when it is not fullscreen
                // (there the desktop size wins until exit), the offscreen render
                // target always - in the fixed render mode that is what actually
                // decides how much the game draws.
                auto applySize = [&]() {
                    resW = std::clamp(resW, 320, 7680);
                    resH = std::clamp(resH, 240, 4320);
                    userSettings.windowWidth = resW;
                    userSettings.windowHeight = resH;
                    if (windowMode != 2) {
                        // The window is clamped to the desktop (same rule as at
                        // boot); resW/resH - the render size - keeps the chosen
                        // value, so choosing e.g. 2560x1440 for the fixed render
                        // mode still works on a smaller screen.
                        int winW = resW;
                        int winH = resH;
                        fitWindowToDesktop(winW, winH, 48, false);
                        SDL_SetWindowSize(window, winW, winH);
                        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                    }
                    applyRenderMode();
                    persistUserData();
                };
                int resIdx = [](int w, int h) {
                    for (int i = 0; i < kResCount; ++i) {
                        if (kResW[i] == w && kResH[i] == h) {
                            return i;
                        }
                    }
                    return kResCount; // 自定义…
                }(resW, resH);

                twoCols(
                    [&](float w) {
                        colSet(0);
                        ui::sectionLabel("界面与渲染", w);
                        colSet(0);
                        // UI zoom for the two screens that are laid out on a
                        // virtual canvas. The play screen is not affected on
                        // purpose.
                        ImGui::Text("界面缩放");
                        float uiScalePct = userSettings.uiScale * 100.0f;
                        colSet(0);
                        if (ui::slider("uiscale", &uiScalePct, 70.0f, 150.0f, 5.0f, "%.0f%%", w)) {
                            const float next = std::clamp(uiScalePct / 100.0f, 0.7f, 1.5f);
                            if (next != userSettings.uiScale) {
                                userSettings.uiScale = next;
                                persistUserData();
                            }
                        }
                        colSet(0);
                        ImGui::Text("渲染模式");
                        colSet(0);
                        // Not `static`: a profile switch replaces the whole
                        // settings struct and a stale index would show the wrong
                        // mode.
                        static const std::vector<std::string> kRenderModeItems = {
                            "跟随窗口分辨率", "固定分辨率"};
                        int renderModeSel = userSettings.renderScale;
                        const int renderSel = std::clamp(renderModeSel, 0, 1);
                        bool renderPicked = ui::combo("##renderscale", kRenderModeItems[renderSel].c_str(),
                            kRenderModeItems, &renderModeSel, w);
                        renderPicked |= ui::padComboNudge(&renderModeSel, 2);
                        if (renderPicked) {
                            userSettings.renderScale = std::clamp(renderModeSel, 0, 1);
                            applyRenderMode();
                            persistUserData();
                        }
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("窗口", w);
                        colSet(1);
                        ImGui::Text("分辨率");
                        colSet(1);
                        static const std::vector<std::string> kResItems = {
                            "640 x 360", "800 x 450", "960 x 540", "1024 x 576", "1120 x 630",
                            "1280 x 720", "1366 x 768", "1600 x 900", "1920 x 1080", "2560 x 1440",
                            "自定义…"};
                        bool resPicked = ui::combo("##resolution",
                            kResItems[std::clamp(resIdx, 0, kResCount)].c_str(), kResItems, &resIdx, w);
                        // Pad: left / right walk this combo's options (see padComboNudge).
                        resPicked |= ui::padComboNudge(&resIdx, kResCount + 1);
                        if (resPicked) {
                            if (resIdx < kResCount) {
                                resW = kResW[resIdx];
                                resH = kResH[resIdx];
                                applySize();
                            }
                        }
                        if (resIdx >= kResCount) {
                            // Committed on Enter / focus loss, not per keystroke:
                            // "1280" typed digit by digit would otherwise resize
                            // the window four times on the way there.
                            static int customW = resW;
                            static int customH = resH;
                            const float half = (w - 8.0f * s) * 0.5f;
                            colSet(1);
                            ImGui::SetNextItemWidth(half);
                            ImGui::InputInt("##resw", &customW, 16, 160);
                            const bool editedW = ImGui::IsItemDeactivatedAfterEdit();
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(half);
                            ImGui::InputInt("##resh", &customH, 16, 160);
                            const bool editedH = ImGui::IsItemDeactivatedAfterEdit();
                            if (editedW || editedH) {
                                resW = customW;
                                resH = customH;
                                applySize();
                            }
                        }
                        colSet(1);
                        ImGui::Text("窗口模式");
                        static int winMode = windowMode;
                        static const std::vector<std::string> kWindowModeItems = {
                            "borderless", "windowed", "fullscreen"};
                        colSet(1);
                        const int winSel = std::clamp(winMode, 0, 2);
                        bool winPicked = ui::combo("##window-mode", kWindowModeItems[winSel].c_str(),
                            kWindowModeItems, &winMode, w);
                        winPicked |= ui::padComboNudge(&winMode, 3);
                        if (winPicked) {
                            windowMode = winMode;
                            if (winMode == 2) {
                                SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                            } else {
                                SDL_SetWindowFullscreen(window, 0);
                                SDL_SetWindowBordered(window, winMode == 1 ? SDL_TRUE : SDL_FALSE);
                                // Leaving fullscreen and re-applying the frame drops
                                // WS_THICKFRAME (see the boot sequence), so put the
                                // resize frame back explicitly.
                                SDL_SetWindowResizable(window, SDL_TRUE);
                            }
                        }
                    });
                twoCols(
                    [&](float w) {
                        colSet(0);
                        ui::sectionLabel("性能", w);
                        colSet(0);
                        ImGui::Text("帧率上限");
                        static float fpsLimitF = static_cast<float>(fpsLimit);
                        colSet(0);
                        if (ui::slider("fps", &fpsLimitF, 0.0f, 240.0f, 5.0f, "%.0f fps", w)) {
                            fpsLimitLive = static_cast<int>(fpsLimitF);
                        }
                        colSet(0);
                        ImGui::Text("实测: %.1f fps", 1.0 / std::max(1e-6, lastFrameDeltaSec));
                        colSet(0);
                        ImGui::Text("超过 %d Hz 自动关垂直同步", displayRefreshHz);
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("显示", w);
                        colSet(1);
                        // 显示帧率 was down in 画面与特效; it is a performance
                        // readout, so it moved up with the fps rows (2026-10-11).
                        bool showFpsBox = showFps;
                        ui::checkBox("显示帧率", &showFpsBox, w);
                        if (showFpsBox != showFps) {
                            showFps = showFpsBox;
                            persistUserData();
                        }
                    });'''

jobs = [
    (3, find(lambda l: l.strip() == '} else if (tab == 4) {'), TAB3),
    (2, find(lambda l: l.strip() == '} else if (tab == 3) {'), TAB2),
    (1, find(lambda l: l.strip() == '} else if (tab == 2) {'), TAB1),
]

for n, e, text in jobs:
    s = find(lambda l, n=n: l.strip() == '} else if (tab == %d) {' % n)
    lines[s + 1:e] = text.split('\n')

io.open(P, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
print('reflowed tab1/2/3')
