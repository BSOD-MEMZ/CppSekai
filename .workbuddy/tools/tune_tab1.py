"""tab1 画面：性能并入左栏（含显示帧率），右栏「窗口」收下原生菜单栏。"""
import io

P = 'main.cpp'
lines = io.open(P, encoding='utf-8').read().split('\n')

start = None
for i, l in enumerate(lines):
    if l.strip() == '} else if (tab == 1) {':
        start = i
        break
assert start is not None

# 第一个 twoCols 所在行
i0 = None
for i in range(start, len(lines)):
    if lines[i].strip() == 'twoCols(':
        i0 = i
        break
# tab2 起点
i2 = None
for i in range(start, len(lines)):
    if lines[i].strip() == '} else if (tab == 2) {':
        i2 = i
        break
# 替换区间：i0 .. i2-1（最后一个 twoCols 结束到 tab2 之间只有闭合的 '            }'）
end = i2 - 1
assert lines[end].strip() == '});', repr(lines[end])

NEW = r'''                twoCols(
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
                        // 性能：原来单独占一行两栏，"显示帧率"一个人待在右栏、
                        // 左边空掉半张卡（2026-11-01 合并）。现在它就在读数下面，
                        // 这个 checkbox 本来也是给这一组读数用的。
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
                        bool showFpsBox = showFps;
                        ui::checkBox("显示帧率", &showFpsBox, w);
                        if (showFpsBox != showFps) {
                            showFps = showFpsBox;
                            persistUserData();
                        }
                        colSet(0);
                        ImGui::Text("实测 %.1f fps · %d Hz 以上自动关垂直同步",
                            1.0 / std::max(1e-6, lastFrameDeltaSec), displayRefreshHz);
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
                        // 原生菜单栏（2026-11-01 从「系统集成」挪来）：它改的是窗口的
                        // 非客户区，跟分辨率 / 窗口模式是一类东西。
                        colSet(1);
                        bool menuBarBox = userSettings.nativeMenuBar;
                        ui::checkBox("原生菜单栏", &menuBarBox, w);
                        if (menuBarBox != userSettings.nativeMenuBar) {
                            userSettings.nativeMenuBar = menuBarBox;
                            // 挂/摘**不在这里做**：SetMenu 会重排非客户区（WM_NCCALCSIZE
                            // 一类的同步消息），而这正是"从 ImGui 帧中间动窗口"最不该干的
                            // 事 —— 跟玻璃实现同一个理由。帧外那段 refreshNativeMenuBar
                            // 会在这一帧结束后按设置自己挂/摘。
                            persistUserData();
                        }
                    });'''

lines[i0:end] = NEW.split('\n')
io.open(P, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
print('tab1 reflowed')
