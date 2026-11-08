"""设置卡片 tab4/tab5/tab6 重排成两栏（每栏自带标题条）。

从后往前替换，行号靠锚点动态定位，不写死。
"""
import io

P = 'main.cpp'
src = io.open(P, encoding='utf-8').read()
lines = src.split('\n')


def find(pred, start=0):
    for i in range(start, len(lines)):
        if pred(lines[i]):
            return i
    raise RuntimeError('anchor not found')


def block(n, end_marker):
    """返回 [内容起点, 内容终点) —— 即 '} else if (tab == n) {' 之后到闭合 '}' 之前。"""
    s = find(lambda l: l.strip() == '} else if (tab == %d) {' % n)
    e = find(end_marker, s + 1)
    return s + 1, e


TAB4 = r'''                // 系统：四块，两栏（2026-11-01 重排，同演奏页）。每块自带标题条，
                // 通栏灰条在宽屏上会一路撑满整张卡片。
                twoCols(
                    [&](float w) {
                        colSet(0);
                        ui::sectionLabel("窗口行为", w);
                        colSet(0);
                        bool autoPauseBox = userSettings.autoPauseOnBlur;
                        ui::checkBox("失焦时自动暂停", &autoPauseBox, w);
                        if (autoPauseBox != userSettings.autoPauseOnBlur) {
                            userSettings.autoPauseOnBlur = autoPauseBox;
                            persistUserData();
                        }
                        colSet(0);
                        bool muteMinBox = userSettings.muteWhenMinimized;
                        ui::checkBox("最小化窗口静音", &muteMinBox, w);
                        if (muteMinBox != userSettings.muteWhenMinimized) {
                            userSettings.muteWhenMinimized = muteMinBox;
                            // 真正切换由帧内那段轮询做（它每帧看一次 SDL_WINDOW_MINIMIZED），
                            // 所以这里只落盘：勾选时窗口必然不是最小化的，下一次最小化
                            // 就会按新设置静音；取消勾选时窗口是还原的，轮询也会把静音摘掉。
                            persistUserData();
                        }
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("系统集成", w);
                        colSet(1);
                        bool smtcBox = userSettings.reportSmtc;
                        ui::checkBox("启用 SMTC 汇报", &smtcBox, w);
                        if (smtcBox != userSettings.reportSmtc) {
                            userSettings.reportSmtc = smtcBox;
                            // Off: stop the media session right away instead of
                            // leaving the flyout with a stale song. On: re-announce,
                            // otherwise nothing shows up until the next song starts.
                            systemMedia.setReporting(smtcBox);
                            if (smtcBox) {
                                announceTrack();
                            }
                            persistUserData();
                        }
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
#ifdef _WIN32
                        // .sus 文件关联：注册表是唯一真相，所以不存进档案 —— 启动查一次，
                        // 之后跟着这个开关走（在别处改过关联时，重启就看到真实状态）。
                        colSet(1);
                        static bool susAssocBox = platform::shell::susAssociated();
                        bool susBox = susAssocBox;
                        ui::checkBox("关联 .sus 谱面文件", &susBox, w);
                        if (susBox != susAssocBox) {
                            if (susBox) {
                                // 写失败就把开关弹回去，别让界面显示一个没生效的状态。
                                susAssocBox = platform::shell::associateSus(exePathW);
                            } else {
                                platform::shell::disassociateSus();
                                susAssocBox = platform::shell::susAssociated();
                            }
                        }
#endif
                        colSet(1);
                        // 开始游戏时切到英文输入法（2026-10-11 从 演奏 挪来）：只把当前
                        // 输入法切到英文（半角）模式、不换键盘布局，中文输入法就不会把 12
                        // 个 lane 键吃进候选框。本质是跟 Windows 输入法打交道，所以落在
                        // 这一组。
                        bool imeBox = userSettings.switchImeOnStart;
                        ui::checkBox("开始游戏时切到英文输入法", &imeBox, w);
                        if (imeBox != userSettings.switchImeOnStart) {
                            userSettings.switchImeOnStart = imeBox;
                            persistUserData();
                        }
                    });
                twoCols(
                    [&](float w) {
                        colSet(0);
                        ui::sectionLabel("多实例", w);
                        colSet(0);
                        ImGui::Text("多开");
                        int mode = userSettings.instanceMode;
                        colSet(0);
                        static const std::vector<std::string> kInstanceItems = {
                            "只允许一个实例", "允许多开，新实例登录另一个用户"};
                        const int instSel = std::clamp(mode, 0, 1);
                        bool modePicked = ui::combo("##instancemode", kInstanceItems[instSel].c_str(),
                            kInstanceItems, &mode, w);
                        modePicked |= ui::padComboNudge(&mode, 2);
                        if (modePicked) {
                            const int picked = std::clamp(mode, 0, 1);
                            // 多开是实验性功能：第一次开启要先确认（已经确认过就直接写）。
                            if (picked == 1 && userSettings.instanceMode == 0
                                && !userSettings.multiInstanceAccepted) {
                                multiInstanceAsk = true;
                                multiInstanceAskFromParty = false;
                            } else {
                                userSettings.instanceMode = picked;
                                // 多人游玩 only means something with several windows, so
                                // switching to single-instance turns it off instead of
                                // leaving a switch that can never do anything.
                                if (userSettings.instanceMode == 0 && userSettings.multiplayer) {
                                    userSettings.multiplayer = false;
                                    platform::logf(platform::LogSystem, "[settings] multiplayer off (single instance)\n");
                                    std::fflush(stdout);
                                }
                                persistUserData();
                            }
                        }
                        colSet(0);
                        // Needs 允许多开: greyed out (and inert) when the instance
                        // policy forbids a second window.
                        const bool multiOpen = userSettings.instanceMode == 1;
                        bool partyBox = multiOpen ? userSettings.multiplayer : false;
                        ui::checkBox("多人演出", &partyBox, w, multiOpen);
                        // Only a click can change it: with the policy on single
                        // instance the greyed box shows "off" and storing that would
                        // silently wipe the setting just for opening this page.
                        if (multiOpen && partyBox != userSettings.multiplayer) {
                            // 勾上多人游玩同样要先确认多开是实验性功能（已经确认过就直接走）。
                            if (partyBox && !userSettings.multiInstanceAccepted) {
                                multiInstanceAsk = true;
                                multiInstanceAskFromParty = true;
                            } else {
                                userSettings.multiplayer = partyBox;
                                // 多人游玩 needs several windows, so it also switches
                                // the instance policy to multi-open; each copy logs in
                                // as its own user (they keep separate records).
                                if (partyBox) {
                                    userSettings.instanceMode = 1;
                                }
                                platform::logf(platform::LogSystem, "[settings] multiplayer %s\n", partyBox ? "on" : "off");
                                std::fflush(stdout);
                                persistUserData();
                                // 刚打开：直接问一句要不要现在就重开（房间是启动时
                                // 加入的，不重开的话这个窗口一直是单人）。
                                if (partyBox) {
                                    restartAsk = true;
                                }
                            }
                        }
                        // 只在真的开了多人时留一行「重开生效」；勾选框自己已经说明前两条了
                        // （2026-10-11 精简：原来三条状态各占一行）。
                        if (userSettings.instanceMode != 0 && userSettings.multiplayer) {
                            colSet(0);
                            ImGui::PushTextWrapPos(cardLeft + padX + w);
                            ImGui::TextWrapped("多人游玩已开启，重开所有窗口生效。");
                            ImGui::PopTextWrapPos();
                        }
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("诊断", w);
                        colSet(1);
                        // 日志输出：一行摘要 + 独立卡片（8 类共 287 个输出点，卡片里勾）。
                        int onCount = 0;
                        for (int i = 0; i < platform::LogCatCount; ++i) {
                            if ((userSettings.logMask & (1u << i)) != 0u) {
                                ++onCount;
                            }
                        }
                        char logLabel[96];
                        std::snprintf(logLabel, sizeof(logLabel), "日志输出　%d/%d 类", onCount,
                            static_cast<int>(platform::LogCatCount));
                        if (ui::capsuleButton(logLabel, ImVec2(w, 40.0f * s), false)) {
                            logCardOpen = true;
                        }
                    });
                // 结束实例：一张卡里开几个窗口之后，关掉它们不该靠一个个点右上角
                // （特别是全屏的那个还未必看得见）。两个按钮都在这里，只有
                // 结束所有实例 需要二次确认 —— 它会连带关掉别的窗口。
                contentLeft();
                {
                    const float btnW = (interior - 8.0f * s) * 0.5f;
                    if (ui::capsuleButton("结束当前实例", ImVec2(btnW, 40.0f * s), false)) {
                        platform::logf(platform::LogSystem, "[instance] quit requested from the settings card\n");
                        std::fflush(stdout);
                        running = false; // the ordinary shutdown path: profile saved on the way out
                    }
                    ImGui::SameLine();
                    static int killAllFrames = 0;
                    if (killAllFrames > 0) {
                        if (ui::capsuleButton("确认结束？", ImVec2(btnW, 40.0f * s), true)) {
                            killAllFrames = 0;
#ifdef _WIN32
                            const int asked = postCloseToAllInstances();
#else
                            const int asked = 0;
#endif
                            platform::logf(platform::LogSystem, "[instance] closed %d window(s) on request\n", asked);
                            std::fflush(stdout);
                            running = false; // ...this window included
                        }
                        --killAllFrames;
                    } else if (ui::capsuleButton("结束所有实例", ImVec2(btnW, 40.0f * s), false)) {
                        killAllFrames = 180; // the second press has to land within ~3s
                    }
                }'''

TAB5 = r'''                // 账户: the local profile. Nothing here leaves the machine, and
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

                twoCols(
                    [&](float w) {
                        // --- 多用户 ---
                        colSet(0);
                        ui::sectionLabel("用户", w);
                        colSet(0);
                        int current = 0;
                        for (std::size_t i = 0; i < profiles.size(); ++i) {
                            if (profiles[i].id == activeProfileId) {
                                current = static_cast<int>(i);
                            }
                        }
                        std::vector<std::string> labels;
                        labels.reserve(profiles.size());
                        for (const game::UserProfile& user : profiles) {
                            labels.push_back(user.name);
                        }
                        int chosen = current;
                        const int labelCount = static_cast<int>(labels.size());
                        bool profilePicked = !labels.empty()
                            && ui::combo("##profile",
                                labels[static_cast<std::size_t>(std::clamp(chosen, 0, labelCount - 1))].c_str(),
                                labels, &chosen, w);
                        if (!labels.empty()) {
                            profilePicked |= ui::padComboNudge(&chosen, labelCount);
                        }
                        if (profilePicked) {
                            if (chosen != current) {
                                activateProfile(profiles[static_cast<std::size_t>(chosen)].id);
                            }
                        }
                        colSet(0);
                        static char newName[48] = "新用户";
                        const float buttonW = w * 0.34f;
                        const float fieldW = w - buttonW - 8.0f * s;
                        ImGui::SetNextItemWidth(fieldW);
                        ImGui::InputText("##newprofile", newName, sizeof(newName));
                        ImGui::SameLine();
                        if (ui::capsuleButton("新建", ImVec2(buttonW, 40.0f * s), true)) {
                            std::string label(newName);
                            if (label.empty()) {
                                label = "新用户";
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
                            // New account: clear the mirrored text buffers by
                            // renaming through the profile id change.
                            platform::logf(platform::LogSystem, "[profile] created '%s' as %s\n", label.c_str(), user.id.c_str());
                        }
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("管理", w);
                        colSet(1);
                        // Two-step instead of a modal: the first press only arms it,
                        // and the same button becomes the confirmation for ~3s.
                        static int confirmFrames = 0;
                        const float buttonW = w * 0.72f;
                        if (profiles.size() < 2) {
                            ImGui::BeginDisabled();
                        }
                        if (confirmFrames > 0) {
                            if (ui::capsuleButton("确认删除？", ImVec2(buttonW, 40.0f * s), true)) {
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
                        } else if (ui::capsuleButton("删除此用户", ImVec2(buttonW, 40.0f * s), false)) {
                            confirmFrames = 180;
                        }
                        if (profiles.size() < 2) {
                            ImGui::EndDisabled();
                        }
                        colSet(1);
                        // 等级 / 经验：原来是一整块 infoRows，现在跟着「管理」走，
                        // 免得右边那一栏空着（下面还有导入导出）。
                        const double need = game::expToNextRank(account.rank);
                        char lvLine[64];
                        std::snprintf(lvLine, sizeof(lvLine), "%d", account.rank);
                        char expLine[64];
                        std::snprintf(expLine, sizeof(expLine), "%d / %d",
                            static_cast<int>(account.exp), static_cast<int>(need));
                        ui::infoRows({{"等级", lvLine}, {"本级经验", expLine}}, w);
                    });
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
                            // The 用户 combobox above lists the *profile files*
                            // (profiles/index.json), which had their own label - so
                            // renaming yourself left it showing "默认用户" and the
                            // two looked like unrelated things. Keep them the same
                            // label; an empty nickname keeps whatever was there
                            // before.
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
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("签名", w);
                        colSet(1);
                        ImGui::InputText("##pnote", noteBuf, sizeof(noteBuf));
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            account.note = noteBuf;
                            persistUserData();
                        }
                    });

                // 导入 / 导出. One profile file carries everything this account
                // owns (settings + scores + 资料), so "backup" and "move to
                // another machine" are the same operation - and a reinstall can
                // be undone. Both go through the native picker.
                twoCols(
                    [&](float w) {
                        colSet(0);
                        ui::sectionLabel("备份", w);
                        colSet(0);
                        if (ui::capsuleButton("导出用户数据", ImVec2(w, 40.0f * s), false)) {
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
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("恢复", w);
                        colSet(1);
                        if (ui::capsuleButton("导入用户数据", ImVec2(w, 40.0f * s), false)) {
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
                    });
                if (profileIoStatusFrames > 0) {
                    --profileIoStatusFrames;
                    contentLeft();
                    ImGui::PushFont(game::bodyFont(), 19.0f * s);
                    ImGui::PushTextWrapPos(cardCenter.x - cardSize.x * 0.5f + padX + interior);
                    ImGui::TextWrapped("%s", profileIoStatus.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::PopFont();
                }'''

TAB6 = r'''                // 关于: what this is, who made it, what it is built on, what it is
                // not, and the two links (把作者 / 上游 / 素材 / 仓库 / 个人站都放这里,
                // 免得玩家去翻 README)。
                //
                // 2026-11-01 重排成两栏：左「关于」是读的部分（谁做的、什么时候），
                // 右「链接 / 声明」是点的部分。原来单列一条竖着排，宽卡片右边整片空着。
                const auto aboutLine = [&](float width, float x0, const char* text) {
                    ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
                    ImGui::TextUnformatted(text);
                    (void)width;
                };
                const auto aboutGap = [&]() {
                    contentLeft();
                    ImGui::Dummy(ImVec2(1.0f, 9.0f * s));
                };
                twoCols(
                    [&](float w) {
                        colSet(0);
                        ui::sectionLabel("关于", w);
                        const float x0 = cardLeft + padX;
                        aboutLine(w, x0, "CppSekai");
                        ImGui::PushFont(game::bodyFont(), 19.0f * s);
                        aboutLine(w, x0, "版本 1.0.0.7");
                        aboutLine(w, x0, "许可 AGPL-3.0-only");
                        aboutGap();
                        aboutLine(w, x0, "策划 / 开发");
                        aboutLine(w, x0, "xxt8582753（BSOD-MEMZ）");
                        aboutLine(w, x0, "测试");
                        aboutLine(w, x0, "Shitakara4banme");
                        aboutGap();
                        aboutLine(w, x0, "株洲市南方中学");
                        aboutLine(w, x0, "2026年9月");
                        ImGui::PopFont();
                    },
                    [&](float w) {
                        colSet(1);
                        ui::sectionLabel("链接", w);
                        // 两个整栏宽的胶囊，竖着排：列里横排两个各 200 宽会把
                        // "访问 xxtsoft" 顶到边缘。
                        if (ui::capsuleButton("GitHub", ImVec2(w, 44.0f * s), true)) {
                            openUrl(L"https://github.com/BSOD-MEMZ/CppSekai");
                        }
                        colSet(1);
                        if (ui::capsuleButton("访问 xxtsoft", ImVec2(w, 44.0f * s), false)) {
                            openUrl(L"https://xxtsoft.top");
                        }
                        colSet(1);
                        ui::sectionLabel("声明", w);
                        colSet(1);
                        // Wrapped at the column width: TextWrapped alone uses the
                        // child's own width, which is the *card* - the text would
                        // run out of the column instead of onto the next line.
                        ImGui::PushFont(game::bodyFont(), 19.0f * s);
                        ImGui::PushTextWrapPos(cardLeft + padX + w);
                        ImGui::TextWrapped("本项目免费、开源、非营利，与 SEGA、Colorful Palette 及《初音未来："
                                           "缤纷舞台》官方没有任何关系，仅供本地学习与练习使用。");
                        ImGui::PopTextWrapPos();
                        ImGui::PopFont();
                        colSet(1);
                        // 许可与免责声明：把首次启动那张卡再抬一次。
                        if (ui::capsuleButton("许可与免责声明", ImVec2(w, 44.0f * s), false)) {
                            eulaAlive = true;
                            eulaDismissedThisRun = false;
                            eulaPadChoice = -1;
                        }
                        colSet(1);
                        const bool replayHit = ui::capsuleButton("重播新手教学", ImVec2(w, 44.0f * s), true);
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
                    });'''

# 从后往前替换（tab6 -> tab4），这样前面的行号不受影响
jobs = []
jobs.append((6, find(lambda l: l.strip() == '// End on an item: the checkbox helper leaves the cursor at the row'),
             TAB6))
jobs.append((5, find(lambda l: l.strip() == '} else if (tab == 6) {'), TAB5))
jobs.append((4, find(lambda l: l.strip() == '} else if (tab == 5) {'), TAB4))

for n, end_i, text in jobs:
    s = find(lambda l, n=n: l.strip() == '} else if (tab == %d) {' % n)
    # tab6 的结束标记前面还有一行 '            }'
    e = end_i
    if n == 6:
        assert lines[e - 1].strip() == '}', lines[e - 1]
        e -= 1
    lines[s + 1:e] = text.split('\n')

io.open(P, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
print('reflowed tab4/5/6')
