"""tab5 账户：改成用户列表（点谁编辑谁）+ 一行四个按钮 + 资料两栏。"""
import io

P = 'main.cpp'
lines = io.open(P, encoding='utf-8').read().split('\n')

s5 = next(i for i, l in enumerate(lines) if l.strip() == '} else if (tab == 5) {')
s6 = next(i for i, l in enumerate(lines) if l.strip() == '} else if (tab == 6) {')
assert lines[s6 - 1].strip() == '}', repr(lines[s6 - 1])

NEW = r'''                // 账户: the local profile. Nothing here leaves the machine, and
                // none of it is drawn during play - see game/AccountData.
                // ImGui::InputText wants a mutable char buffer, so the three
                // fields are mirrored once (the account is already loaded from
                // disk by the time this first runs) and written back on change.
                static char nameBuf[64] = {};
                static char orgBuf[96] = {};
                static char noteBuf[160] = {};
                static bool bufReady = false;
                // Which profile the buffers were filled from. Switching user
                // has to re-read them, otherwise the next keystroke would write
                // the previous player's name into the new profile. The same goes
                // for 导入, which replaces the file of the profile that is already
                // active - hence the generation counter.
                static std::string bufProfile;
                static int bufGeneration = -1;
                if (!bufReady || bufProfile != activeProfileId || bufGeneration != profileDataGeneration) {
                    bufReady = true;
                    bufProfile = activeProfileId;
                    bufGeneration = profileDataGeneration;
                    std::snprintf(nameBuf, sizeof(nameBuf), "%s", account.name.c_str());
                    std::snprintf(orgBuf, sizeof(orgBuf), "%s", account.org.c_str());
                    std::snprintf(noteBuf, sizeof(noteBuf), "%s", account.note.c_str());
                }

                // --- 用户列表（2026-11-01 重做）---
                // 原来是一个下拉框选用户，下面再堆「新建 / 删除 / 资料 / 导入导出」，
                // 页面上一眼看不出"有几个用户、现在编辑的是谁"。现在一行一个用户：
                // 点哪一行就切到谁，下面的资料跟着换。
                contentLeft();
                ui::sectionLabel("用户", interior);
                {
                    int current = 0;
                    for (std::size_t i = 0; i < profiles.size(); ++i) {
                        if (profiles[i].id == activeProfileId) {
                            current = static_cast<int>(i);
                        }
                    }
                    const float rowH = 54.0f * s;
                    ImDrawList* listDl = ImGui::GetWindowDrawList();
                    int pick = -1;
                    for (std::size_t i = 0; i < profiles.size(); ++i) {
                        ImGui::PushID(0x5000 + static_cast<int>(i));
                        contentLeft();
                        const ImVec2 p0 = ImGui::GetCursorScreenPos();
                        const ImVec2 p1(p0.x + interior, p0.y + rowH);
                        const bool hit = ImGui::InvisibleButton("##profilerow", ImVec2(interior, rowH));
                        const bool hovered = ImGui::IsItemHovered();
                        const bool sel = static_cast<int>(i) == current;
                        const float hot = ui::anim(0x5052u000u + static_cast<unsigned>(i),
                            hovered && !sel ? 1.0f : 0.0f, 14.0f);
                        listDl->AddRectFilled(p0, p1,
                            sel ? IM_COL32(230, 246, 242, 255)
                                : ui::mix(IM_COL32(246, 246, 250, 255), IM_COL32(236, 238, 246, 255), hot),
                            10.0f * s);
                        if (sel) {
                            listDl->AddRectFilled(ImVec2(p0.x + 5.0f * s, p0.y + 12.0f * s),
                                ImVec2(p0.x + 9.0f * s, p1.y - 12.0f * s), IM_COL32(53, 208, 174, 255),
                                2.0f * s);
                        }
                        ImGui::PushFont(game::bodyFont(), 24.0f * s);
                        const ImVec2 ts = ImGui::CalcTextSize(profiles[i].name.c_str());
                        listDl->AddText(ImGui::GetFont(), 24.0f * s,
                            ImVec2(p0.x + 26.0f * s, p0.y + (rowH - ts.y) * 0.5f),
                            sel ? IM_COL32(38, 42, 58, 255) : IM_COL32(98, 98, 120, 255),
                            profiles[i].name.c_str());
                        if (sel) {
                            const char* tag = "当前";
                            const ImVec2 tsz = ImGui::CalcTextSize(tag);
                            listDl->AddText(ImGui::GetFont(), 18.0f * s,
                                ImVec2(p1.x - tsz.x - 24.0f * s, p0.y + (rowH - tsz.y) * 0.5f),
                                IM_COL32(116, 148, 138, 255), tag);
                        }
                        ImGui::PopFont();
                        if (hit && !sel) {
                            pick = static_cast<int>(i);
                        }
                        ImGui::PopID();
                    }
                    if (pick >= 0) {
                        activateProfile(profiles[static_cast<std::size_t>(pick)].id);
                    }
                }

                // 四个按钮一行：新建 / 删除 / 导出 / 导入。
                // 新建不再问名字 —— 直接建一个「用户 N」，名字在下面的昵称里改
                // （昵称本来就写回 profile 的名字，两处是同一个东西）。
                contentLeft();
                {
                    const float gap = 8.0f * s;
                    const float bw = (interior - gap * 3.0f) / 4.0f;
                    if (ui::capsuleButton("新建用户", ImVec2(bw, 44.0f * s), false)) {
                        std::string label = "用户 2";
                        for (int n = 2; n < 999; ++n) {
                            label = "用户 " + std::to_string(n);
                            bool taken = false;
                            for (const game::UserProfile& user : profiles) {
                                if (user.name == label) {
                                    taken = true;
                                    break;
                                }
                            }
                            if (!taken) {
                                break;
                            }
                        }
                        // A fresh profile starts from the defaults (so it does
                        // not inherit this user's note speed / offset), which
                        // is also what "save the current one first" protects.
                        game::saveUserData(userDataFile, userSettings, scores, account);
                        game::UserProfile user;
                        user.id = game::makeProfileId(label, profiles);
                        user.name = label;
                        profiles.push_back(user);
                        activeProfileId.clear(); // so activateProfile() does not bail out
                        activateProfile(user.id);
                        platform::logf(platform::LogSystem, "[profile] created '%s' as %s\n", label.c_str(), user.id.c_str());
                    }
                    ImGui::SameLine(0.0f, gap);
                    // Two-step instead of a modal: the first press only arms it,
                    // and the same button becomes the confirmation for ~3s.
                    static int confirmFrames = 0;
                    if (profiles.size() < 2) {
                        ImGui::BeginDisabled();
                    }
                    if (confirmFrames > 0) {
                        if (ui::capsuleButton("确认删除？", ImVec2(bw, 44.0f * s), true)) {
                            confirmFrames = 0;
                            const std::string victim = activeProfileId;
                            std::string victimName;
                            std::size_t target = profiles.size();
                            for (std::size_t i = 0; i < profiles.size(); ++i) {
                                if (profiles[i].id == victim) {
                                    victimName = profiles[i].name;
                                } else if (target == profiles.size()) {
                                    target = i;
                                }
                            }
                            if (target < profiles.size()) {
                                // Take the id first: erasing shifts the indexes.
                                const std::string targetId = profiles[target].id;
                                profiles.erase(std::remove_if(profiles.begin(), profiles.end(),
                                                   [&](const game::UserProfile& user) { return user.id == victim; }),
                                    profiles.end());
                                activeProfileId.clear(); // so activateProfile() does not bail out
                                activateProfile(targetId);
                                // The file is left on disk on purpose: removing a
                                // user from the list must never destroy their
                                // scores. It sits in profiles/ as <id>.json.
                                platform::logf(platform::LogSystem, "[profile] removed '%s' (%s) from the list; file kept\n",
                                    victimName.c_str(), victim.c_str());
                            }
                        }
                        --confirmFrames;
                    } else if (ui::capsuleButton("删除用户", ImVec2(bw, 44.0f * s), false)) {
                        confirmFrames = 180;
                    }
                    if (profiles.size() < 2) {
                        ImGui::EndDisabled();
                    }
                    ImGui::SameLine(0.0f, gap);
                    if (ui::capsuleButton("导出", ImVec2(bw, 44.0f * s), false)) {
                        // Nicknames are free text, so illegal file-name
                        // characters are replaced rather than dropped.
                        std::string who = account.name.empty() ? activeProfileId : account.name;
                        for (char& c : who) {
                            if (std::strchr("\\/:*?\"<>|", c) != nullptr) {
                                c = '_';
                            }
                        }
                        const std::string suggested = path_utf8::fromPath(path_utf8::toPath(userDataDir)
                            / ("CppSekai-" + who + "-" + dateStamp() + ".json"));
                        const std::string path = pickUserDataFile(true, suggested);
                        if (!path.empty()) {
                            // persistUserData() first: the live mirrors (note
                            // speed, offset, judgement...) are what make up
                            // userSettings, and they are only copied on save.
                            persistUserData();
                            game::saveUserData(path, userSettings, scores, account);
                            profileIoStatus = "已导出：" + fileLabel(path);
                            profileIoStatusFrames = 300;
                            platform::logf(platform::LogSystem, "[profile] exported '%s' to %s\n", activeProfileId.c_str(),
                                path.c_str());
                            std::fflush(stdout);
                        }
                    }
                    ImGui::SameLine(0.0f, gap);
                    if (ui::capsuleButton("导入", ImVec2(bw, 44.0f * s), false)) {
                        const std::string path = pickUserDataFile(false, std::string());
                        if (!path.empty()) {
                            // Screened *before* the confirmation card: a file
                            // that is not user data must not get a "we are about
                            // to replace your scores" prompt.
                            if (game::isUserDataFile(path)) {
                                game::UserSettings probeSettings;
                                std::map<std::string, game::ScoreRecord> probeScores;
                                game::AccountData probeAccount;
                                game::loadUserData(path, probeSettings, probeScores, probeAccount);
                                importPath = path;
                                importWho = probeAccount.name.empty() ? "(无昵称)" : probeAccount.name;
                                importScores = probeScores.size();
                                importAsk = true;
                                platform::logf(platform::LogSystem, "[profile] import candidate %s (user '%s', %zu score(s))\n",
                                    path.c_str(), importWho.c_str(), importScores);
                            } else {
                                profileIoStatus = "不是用户数据文件：" + fileLabel(path);
                                profileIoStatusFrames = 300;
                                platform::logf(platform::LogSystem, "[profile] import rejected %s (not user data)\n", path.c_str());
                            }
                            std::fflush(stdout);
                        }
                    }
                }
                if (profileIoStatusFrames > 0) {
                    --profileIoStatusFrames;
                    contentLeft();
                    ImGui::PushFont(game::bodyFont(), 19.0f * s);
                    ImGui::PushTextWrapPos(cardCenter.x - cardSize.x * 0.5f + padX + interior);
                    ImGui::TextWrapped("%s", profileIoStatus.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::PopFont();
                }

                // 选中那个用户的资料。改的就是上面列表里高亮的那一行。
                twoCols(
                    [&](float w) {
                        colSet(0);
                        ui::sectionLabel("资料", w);
                        colSet(0);
                        ImGui::Text("昵称");
                        colSet(0);
                        ImGui::SetNextItemWidth(w);
                        if (ImGui::InputText("##pname", nameBuf, sizeof(nameBuf))) {
                            account.name = nameBuf;
                            // The 用户 list above shows the *profile file* label
                            // (profiles/index.json); renaming yourself has to keep
                            // the two in step, otherwise the list keeps the old
                            // name. An empty nickname keeps whatever was there.
                            for (game::UserProfile& user : profiles) {
                                if (user.id != activeProfileId || account.name.empty()
                                    || user.name == account.name) {
                                    continue;
                                }
                                user.name = account.name;
                                game::saveProfiles(userDataDir, profiles, activeProfileId);
                                break;
                            }
                            if (party.active()) {
                                // The room shows this name too, and so does the
                                // window title (it is what tells two rooms' windows
                                // apart).
                                party.setName(account.name.empty() ? partyLabel : account.name);
                            }
                            syncWindowTitle();
                            persistUserData();
                        }
                        colSet(0);
                        ImGui::Text("学校 / 组织");
                        colSet(0);
                        ImGui::SetNextItemWidth(w);
                        if (ImGui::InputText("##porg", orgBuf, sizeof(orgBuf))) {
                            account.org = orgBuf;
                            persistUserData();
                        }
                        colSet(0);
                        const double need = game::expToNextRank(account.rank);
                        char lvLine[64];
                        std::snprintf(lvLine, sizeof(lvLine), "%d", account.rank);
                        char expLine[64];
                        std::snprintf(expLine, sizeof(expLine), "%d / %d",
                            static_cast<int>(account.exp), static_cast<int>(need));
                        ui::infoRows({{"等级", lvLine}, {"本级经验", expLine}}, w);
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("签名", w);
                        colSet(1);
                        ImGui::SetNextItemWidth(w);
                        ImGui::InputText("##pnote", noteBuf, sizeof(noteBuf));
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            account.note = noteBuf;
                            persistUserData();
                        }
                    });'''

lines[s5 + 1:s6 - 1] = NEW.split('\n')
io.open(P, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
print('tab5 rebuilt')
