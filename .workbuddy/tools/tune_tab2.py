"""tab2 外观：经典开屏并回「界面」，「打击特效」升到第一行右栏，「系统触摸」挪走。"""
import io

P = 'main.cpp'
lines = io.open(P, encoding='utf-8').read().split('\n')

s2 = next(i for i, l in enumerate(lines) if l.strip() == '} else if (tab == 2) {')
i0 = next(i for i in range(s2, len(lines)) if lines[i].strip() == 'twoCols(')
s3 = next(i for i in range(s2, len(lines)) if lines[i].strip() == '} else if (tab == 3) {')
assert lines[s3 - 1].strip() == '});', lines[s3 - 1]

NEW = r'''                twoCols(
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
                        colSet(0);
                        // Splash style: static image (0) vs classic progress bar (1).
                        // Only read at startup, so a change takes effect next launch.
                        // 2026-11-01：原来自己一个「开屏」组，整栏就只有这一个 checkbox，
                        // 标题占的宽度比内容还多 —— 并回「界面」。
                        bool classicSplashBox = userSettings.splashStyle != 0;
                        ui::checkBox("经典开屏画面", &classicSplashBox, w);
                        if (classicSplashBox != (userSettings.splashStyle != 0)) {
                            userSettings.splashStyle = classicSplashBox ? 1 : 0;
                            persistUserData();
                        }
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("打击特效", w);
                        colSet(1);
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
                    });'''

lines[i0:s3] = NEW.split('\n')
io.open(P, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
print('tab2 reflowed')
