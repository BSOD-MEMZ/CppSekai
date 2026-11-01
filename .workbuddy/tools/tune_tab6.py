"""tab6 关于：去掉分组，信息铺开，CppSekai 大字号，四个按钮一行、全非 primary。"""
import io

P = 'main.cpp'
lines = io.open(P, encoding='utf-8').read().split('\n')

s6 = next(i for i, l in enumerate(lines) if l.strip() == '} else if (tab == 6) {')
end = next(i for i, l in enumerate(lines) if l.strip().startswith('// End on an item:'))
# end-1 是 tab6 的闭合 '            }'，再往上是 twoCols 的 '});'
assert lines[end - 1].strip() == '}', repr(lines[end - 1])
assert lines[end - 2].strip() == '});', repr(lines[end - 2])

NEW = r'''                // 关于: 一页流水信息，不分组（2026-11-01 用户要求）。
                // 短页上再加几条灰条只是多几块分隔；从上往下读完就行。
                // 字号上限是 42 —— 字体图集就是按 42px 光栅化的（见 Intro.cpp 的
                // AddFontFromFileTTF），超过它只会放大糊掉，所以标题取 40。
                contentLeft();
                ImGui::PushFont(game::titleFont(), 40.0f * s);
                ImGui::TextUnformatted("CppSekai");
                ImGui::PopFont();

                contentLeft();
                ImGui::PushFont(game::bodyFont(), 22.0f * s);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(122, 122, 148, 255));
                ImGui::TextUnformatted("版本 1.0.0.6 · 许可 AGPL-3.0-only");
                ImGui::PopStyleColor();
                ImGui::PopFont();

                contentLeft();
                ImGui::PushFont(game::bodyFont(), 24.0f * s);
                ImGui::TextUnformatted("策划 / 开发　xxt8582753（BSOD-MEMZ）");
                contentLeft();
                ImGui::TextUnformatted("测试　Shitakara4banme");
                contentLeft();
                ImGui::TextUnformatted("株洲市南方中学 · 2026 年 9 月");
                ImGui::PopFont();

                // 四个按钮一行，全都不是 primary —— 这一页没有"主操作"，
                // 之前 GitHub 那颗是 primary，看着像"该点这个"。
                contentLeft();
                ImGui::Dummy(ImVec2(1.0f, 6.0f * s));
                contentLeft();
                {
                    const float gap = 8.0f * s;
                    const float bw = (interior - gap * 3.0f) / 4.0f;
                    if (ui::capsuleButton("GitHub", ImVec2(bw, 44.0f * s), false)) {
                        openUrl(L"https://github.com/BSOD-MEMZ/CppSekai");
                    }
                    ImGui::SameLine(0.0f, gap);
                    if (ui::capsuleButton("访问 xxtsoft", ImVec2(bw, 44.0f * s), false)) {
                        openUrl(L"https://xxtsoft.top");
                    }
                    ImGui::SameLine(0.0f, gap);
                    if (ui::capsuleButton("许可与免责声明", ImVec2(bw, 44.0f * s), false)) {
                        // Raises the first-run card again - see the eula state up
                        // with the settings tab.
                        eulaAlive = true;
                        eulaDismissedThisRun = false;
                        eulaPadChoice = -1;
                    }
                    ImGui::SameLine(0.0f, gap);
                    const bool replayHit = ui::capsuleButton("重播新手教学", ImVec2(bw, 44.0f * s), false);
                    if (std::getenv("CPSEKAI_UI_TRACE") != nullptr) {
                        const ImVec2 lo = ImGui::GetItemRectMin();
                        const ImVec2 hi = ImGui::GetItemRectMax();
                        const ImVec2 mp = ImGui::GetIO().MousePos;
                        const ImVec2 ds = ImGui::GetIO().DisplaySize;
                        platform::logf(platform::LogRender, "[ui] replay pill %.0f,%.0f..%.0f,%.0f mouse=%.0f,%.0f display=%.0fx%.0f\n",
                            lo.x, lo.y, hi.x, hi.y, mp.x, mp.y, ds.x, ds.y);
                        std::fflush(stdout);
                    }
                    if (replayHit) {
                        restartTutorial();
                    }
                }

                contentLeft();
                ImGui::Dummy(ImVec2(1.0f, 10.0f * s));
                // Wrapped at the card interior: TextWrapped alone uses the child's
                // own width, which already is the interior here, but an explicit
                // wrap pos keeps it clear of the scrollbar.
                contentLeft();
                ImGui::PushFont(game::bodyFont(), 20.0f * s);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(122, 122, 148, 255));
                ImGui::PushTextWrapPos(cardLeft + padX + interior);
                ImGui::TextWrapped("本项目免费、开源、非营利，与 SEGA、Colorful Palette 及《初音未来："
                                   "缤纷舞台》官方没有任何关系，仅供本地学习与练习使用。"
                                   "全部曲名、角色、美术与音频的权利归各自权利人所有。");
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ImGui::PopFont();'''

lines[s6 + 1:end - 1] = NEW.split('\n')
io.open(P, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
print('tab6 rebuilt')
