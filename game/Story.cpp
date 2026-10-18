// CppSekai - story / tutorial runtime (see STORY-UI.md).
//
// A script is a flat list of steps. Branching is a step with options, each
// option naming the step to jump to; there are no expressions, because the only
// thing that ever branches here is "which device are you on" and "do you want\n// the multiplayer tour". Conditions are all about the live game (is the
// settings card open, is the downloader still up, did the live start), and
// live in Host - main owns the truth.
//
// Two failure modes get first-class handling, because a tutorial that talks
// over a closed window is worse than no tutorial:
//   * `onClose`   - the downloader window vanished while we were pointing at it
//   * `timeout` / `onTimeout` - the player wandered off for two minutes
#include "Story.hpp"

#include "Intro.hpp"
#include "Ui.hpp"
#include "platform/Renderer.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace story
{
namespace
{
    struct Option
    {
        std::string text;
        std::string target; // step id
        std::string image;  // cards: which picture to show
        std::map<std::string, std::string> set;
    };

    struct Step
    {
        std::string id;
        std::string type = "say"; // say | scene | image | cards | choice
        std::string who;
        std::string text;
        std::string image;
        std::string caption;
        std::string doThis;   // run once on entry
        std::string wait;     // gate before advancing
        std::string onClose;  // branch when the downloader disappears
        std::string onTimeout;
        std::string target;   // unconditional jump (after the step)
        std::string highlight; // ui anchor id
        std::string tip;       // label next to the spotlight ring
        std::string guide;     // points into chartdl ("search" / "list" / ...)
        bool passthrough = false;
        bool boxLeft = false; // compact box on the left (phone panel stays visible)
        float hold = 1.8f;
        float timeout = 0.0f;
        std::vector<Option> options;
        std::vector<std::string> subtitles;
        std::map<std::string, std::string> set;
        // runtime side
        storyui::TextLayout layout;
        std::size_t revealBytes = 0;
    };

    struct Script
    {
        std::string id;
        std::vector<Step> steps;
    };

    std::vector<Script> gScripts;
    std::map<std::string, storyui::Image> gImages;
    platform::Renderer* gRenderer = nullptr;
    std::string gAssetDir;
    Host gHost;

    Script* gCur = nullptr;
    std::size_t gIndex = 0;
    bool gActive = false;
    bool gSuspended = false;
    bool gFinished = false;
    std::map<std::string, std::string> gFlags;
    float gTime = 0.0f;     // whole script, for the ▼ bob / spotlight pulse
    float gStepTime = 0.0f; // current step
    float gReveal = 0.0f;   // characters revealed
    int gPadFocus = 0;      // which option the pad is on
    bool gPadUsed = false;  // the pad moved the focus -> draw the ring
    bool gPlaySeen = false; // the live started while a step was waiting for it

    float revealSpeed()
    {
        if (std::getenv("CPSEKAI_STORY_FAST") != nullptr)
        {
            return 240.0f;
        }
        return 30.0f;
    }

    bool hostTrue(const std::function<bool()>& fn)
    {
        return fn && fn();
    }

    int totalChars(const Step& step)
    {
        return static_cast<int>(step.layout.codepoints.size()) - 1;
    }

    storyui::Image& ensureImage(const std::string& name)
    {
        auto it = gImages.find(name);
        if (it != gImages.end())
        {
            return it->second;
        }
        storyui::Image img;
        if (gRenderer != nullptr && !name.empty())
        {
            // Forward slashes: Windows accepts them in fopen, and this avoids a
            // layer of C escaping in a string that already carries a path.
            img = storyui::loadImage(*gRenderer, gAssetDir + "/story/img/" + name);
        }
        return gImages.emplace(name, img).first->second;
    }

    void applyFlags(const std::map<std::string, std::string>& flags)
    {
        // Flags only matter inside one script run (which device the player
        // picked, whether they want the multiplayer tour). They are kept so a
        // later step could react to them; the current script branches with
        // goto instead, which is easier to read in the JSON.
        gFlags.insert(flags.begin(), flags.end());
    }

    void enterStep()
    {
        gStepTime = 0.0f;
        gReveal = 0.0f;
        gPadFocus = 0;
        if (gCur == nullptr || gIndex >= gCur->steps.size())
        {
            return;
        }
        Step& step = gCur->steps[gIndex];
        applyFlags(step.set);
        if (!step.doThis.empty() && gHost.command)
        {
            gHost.command(step.doThis);
        }
        if (!step.image.empty())
        {
            ensureImage(step.image);
        }
        if (ImFont* font = game::bodyFont(); font != nullptr)
        {
            // Wrap widths follow the editor's setup table: the full box wraps at
            // 1300 from x=245. The two narrowed layouts (settings card / phone
            // panel) use the space they actually have.
            const float width = step.passthrough ? 860.0f : (step.boxLeft ? 640.0f : 1300.0f);
            const float size = (step.passthrough || step.boxLeft) ? 38.0f : 44.0f;
            step.layout = storyui::layoutText(font, size, step.text, width);
        }
        step.revealBytes = 0;
        std::printf("[story] step %zu/%zu id=%s type=%s wait=%s\n", gIndex + 1, gCur->steps.size(),
            step.id.c_str(), step.type.c_str(), step.wait.c_str());
        std::fflush(stdout);
    }

    void finish()
    {
        gActive = false;
        gFinished = true;
        gCur = nullptr;
        std::printf("[story] finished\n");
        std::fflush(stdout);
    }

    void jump(const std::string& label)
    {
        if (gCur == nullptr)
        {
            finish();
            return;
        }
        for (std::size_t i = 0; i < gCur->steps.size(); ++i)
        {
            if (gCur->steps[i].id == label)
            {
                gIndex = i;
                enterStep();
                return;
            }
        }
        std::printf("[story] missing label '%s' - stopping\n", label.c_str());
        std::fflush(stdout);
        finish();
    }

    void next()
    {
        if (gCur == nullptr)
        {
            finish();
            return;
        }
        const Step& step = gCur->steps[gIndex];
        if (!step.target.empty())
        {
            jump(step.target);
            return;
        }
        if (gIndex + 1 >= gCur->steps.size())
        {
            finish();
            return;
        }
        ++gIndex;
        enterStep();
    }

    void revealAll(Step& step)
    {
        step.revealBytes = step.text.size();
        gReveal = static_cast<float>(totalChars(step));
    }

    bool waitSatisfied(const Step& step)
    {
        const std::string& w = step.wait;
        if (w.empty())
        {
            // A scene card is timed by its own `hold`: it has no click prompt
            // and no wait key in the script, so without this it sat there
            // forever (the card even faded out, leaving a dimmed screen with
            // nothing on it - exactly what "the tutorial hangs" looked like).
            if (step.type == "scene" && step.hold > 0.0f)
            {
                return gStepTime >= step.hold + 0.35f; // + the fade-out
            }
            return false;
        }
        // The command that a step fires (open the settings card, start the
        // downloader) has not happened yet on the frame the step is entered.
        // Half a second is enough for main to act and the window to appear, and
        // it also keeps "settings closed" from reading true before the card has
        // ever opened.
        if (gStepTime < 0.45f)
        {
            return false;
        }
        if (w == "seconds")
        {
            return gStepTime >= step.hold;
        }
        if (w == "settingsClosed")
        {
            return !hostTrue(gHost.settingsOpen);
        }
        if (w == "settingsOpen")
        {
            return hostTrue(gHost.settingsOpen);
        }
        if (w == "chartdlOpen")
        {
            return hostTrue(gHost.chartdlOpen);
        }
        if (w == "chartdlSearch")
        {
            return hostTrue(gHost.chartdlSearch);
        }
        if (w == "chartdlSong")
        {
            return hostTrue(gHost.chartdlSong);
        }
        if (w == "chartdlDone")
        {
            return hostTrue(gHost.chartdlDone);
        }
        if (w == "chartdlEasy")
        {
            return hostTrue(gHost.chartdlEasy);
        }
        if (w == "chartdlClosed")
        {
            return !hostTrue(gHost.chartdlOpen);
        }
        if (w == "playStarted")
        {
            // Latched: the layer is parked while the live runs, so by the time
            // update() runs again the game is already back on the select screen
            // and inPlay reads false. Without the latch the tutorial would wait
            // forever for a play that already happened.
            return gPlaySeen || hostTrue(gHost.inPlay);
        }
        return false;
    }

    // A chartdl step whose window is gone cannot continue. Give it a moment
    // (the process takes a second to come up) and then branch, so closing the
    // downloader mid-step lands on a "want me to start it again?" question
    // instead of a line of text pointing at nothing.
    bool downloaderVanished(const Step& step)
    {
        if (step.onClose.empty() || gStepTime < 3.0f)
        {
            return false;
        }
        if (step.wait.rfind("chartdl", 0) != 0)
        {
            return false;
        }
        return !hostTrue(gHost.chartdlOpen);
    }
} // namespace

void setHost(Host host)
{
    gHost = std::move(host);
}

bool load(platform::Renderer& renderer, const std::string& assetDir, std::string& err)
{
    gRenderer = &renderer;
    gAssetDir = assetDir;
    gScripts.clear();
    gImages.clear();

    std::printf("[story] asset dir: %s\n", assetDir.c_str());
    storyui::loadTextures(renderer, assetDir, err);

    const std::string path = assetDir + "\\story\\scripts\\tutorial.json";
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
        err = "cannot open " + path;
        std::printf("[story] %s\n", err.c_str());
        return false;
    }
    std::string text;
    char buf[4096];
    while (const std::size_t n = std::fread(buf, 1, sizeof(buf), file))
    {
        text.append(buf, n);
    }
    std::fclose(file);

    try
    {
        const nlohmann::json doc = nlohmann::json::parse(text);
        for (const auto& s : doc.at("scripts"))
        {
            Script script;
            script.id = s.value("id", std::string{});
            for (const auto& j : s.at("steps"))
            {
                Step step;
                step.id = j.value("id", std::string{});
                step.type = j.value("type", std::string("say"));
                step.who = j.value("who", std::string{});
                step.text = j.value("text", std::string{});
                step.image = j.value("image", std::string{});
                step.caption = j.value("caption", std::string{});
                step.doThis = j.value("do", std::string{});
                step.wait = j.value("wait", std::string{});
                step.onClose = j.value("onClose", std::string{});
                step.onTimeout = j.value("onTimeout", std::string{});
                step.target = j.value("goto", std::string{});
                step.highlight = j.value("highlight", std::string{});
                step.tip = j.value("tip", std::string{});
                step.guide = j.value("guide", std::string{});
                step.passthrough = j.value("passthrough", false);
                step.boxLeft = j.value("box", std::string{}) == "left";
                step.hold = j.value("hold", 1.8f);
                step.timeout = j.value("timeout", 0.0f);
                if (j.contains("set"))
                {
                    for (const auto& [k, v] : j.at("set").items())
                    {
                        step.set[k] = v.get<std::string>();
                    }
                }
                if (j.contains("options"))
                {
                    for (const auto& o : j.at("options"))
                    {
                        Option opt;
                        if (o.is_string())
                        {
                            opt.text = o.get<std::string>();
                        }
                        else
                        {
                            opt.text = o.value("text", std::string{});
                            opt.target = o.value("goto", std::string{});
                            opt.image = o.value("image", std::string{});
                            if (o.contains("set"))
                            {
                                for (const auto& [k, v] : o.at("set").items())
                                {
                                    opt.set[k] = v.get<std::string>();
                                }
                            }
                        }
                        step.options.push_back(std::move(opt));
                    }
                }
                if (j.contains("subtitles"))
                {
                    for (const auto& sub : j.at("subtitles"))
                    {
                        step.subtitles.push_back(sub.get<std::string>());
                    }
                }
                script.steps.push_back(std::move(step));
            }
            std::printf("[story] script '%s' with %zu steps\n", script.id.c_str(), script.steps.size());
            gScripts.push_back(std::move(script));
        }
    }
    catch (const std::exception& ex)
    {
        err = std::string("bad script json: ") + ex.what();
        std::printf("[story] %s\n", err.c_str());
        return false;
    }
    return true;
}

bool hasScript(const char* id)
{
    if (id == nullptr)
    {
        return false;
    }
    return std::any_of(gScripts.begin(), gScripts.end(), [&](const Script& s) { return s.id == id; });
}

bool start(const char* id)
{
    if (id == nullptr)
    {
        return false;
    }
    for (Script& script : gScripts)
    {
        if (script.id == id)
        {
            gCur = &script;
            gIndex = 0;
            gActive = true;
            gSuspended = false;
            gFinished = false;
            gTime = 0.0f;
            gPlaySeen = false;
            gFlags.clear();
            enterStep();
            return true;
        }
    }
    std::printf("[story] no script '%s'\n", id);
    return false;
}

bool debugJump(int stepIndex)
{
    if (!gActive || gCur == nullptr || stepIndex < 0
        || stepIndex >= static_cast<int>(gCur->steps.size()))
    {
        return false;
    }
    gIndex = static_cast<std::size_t>(stepIndex);
    enterStep();
    return true;
}

bool active()
{
    return gActive;
}

bool suspended()
{
    return gSuspended;
}

void setSuspended(bool on)
{
    if (on && !gSuspended && gActive && gCur != nullptr && gIndex < gCur->steps.size()
        && gCur->steps[gIndex].wait == "playStarted")
    {
        gPlaySeen = true;
    }
    gSuspended = on;
}

bool passthrough()
{
    if (!gActive || gCur == nullptr || gIndex >= gCur->steps.size())
    {
        return false;
    }
    return gCur->steps[gIndex].passthrough;
}

bool blocksInput()
{
    return gActive && !gSuspended && !passthrough();
}

const std::string& guideKey()
{
    static const std::string empty;
    if (!gActive || gSuspended || gCur == nullptr || gIndex >= gCur->steps.size())
    {
        return empty;
    }
    return gCur->steps[gIndex].guide;
}

const std::string& guideLabel()
{
    static const std::string empty;
    if (!gActive || gSuspended || gCur == nullptr || gIndex >= gCur->steps.size())
    {
        return empty;
    }
    return gCur->steps[gIndex].tip;
}

void padMove(int delta)
{
    if (!gActive || gSuspended || gCur == nullptr || gIndex >= gCur->steps.size())
    {
        return;
    }
    const Step& step = gCur->steps[gIndex];
    if (step.options.empty() || gReveal < static_cast<float>(totalChars(step)))
    {
        return;
    }
    const int count = static_cast<int>(step.options.size());
    gPadFocus = (gPadFocus + delta + count) % count;
    gPadUsed = true;
    ui::se(ui::SeSelect);
}

void padAccept()
{
    if (!gActive || gSuspended || gCur == nullptr || gIndex >= gCur->steps.size())
    {
        return;
    }
    Step& step = gCur->steps[gIndex];
    const bool ready = gReveal >= static_cast<float>(totalChars(step));
    if (!step.options.empty())
    {
        gPadUsed = true;
        if (!ready || gPadFocus < 0 || gPadFocus >= static_cast<int>(step.options.size()))
        {
            return;
        }
        ui::se(ui::SeClick);
        const Option& opt = step.options[static_cast<std::size_t>(gPadFocus)];
        applyFlags(opt.set);
        if (!opt.target.empty())
        {
            jump(opt.target);
        }
        else
        {
            next();
        }
        return;
    }
    if (step.wait.empty() && !step.passthrough)
    {
        if (!ready)
        {
            revealAll(step);
        }
        else
        {
            next();
        }
    }
}

bool consumeFinished()
{
    const bool was = gFinished;
    gFinished = false;
    return was;
}

void stop()
{
    if (gActive)
    {
        std::printf("[story] stopped by host\n");
        std::fflush(stdout);
    }
    gActive = false;
    gFinished = false;
    gCur = nullptr;
    gSuspended = false;
}

void update(float dt)
{
    if (!gActive || gSuspended || gCur == nullptr || gIndex >= gCur->steps.size())
    {
        return;
    }
    gTime += dt;
    gStepTime += dt;
    Step& step = gCur->steps[gIndex];

    const int chars = totalChars(step);
    if (chars > 0)
    {
        gReveal = std::min(gReveal + revealSpeed() * dt, static_cast<float>(chars));
        const int shown = std::min(static_cast<int>(gReveal), chars);
        step.revealBytes = step.layout.codepoints[static_cast<std::size_t>(shown)];
    }
    else
    {
        step.revealBytes = 0;
    }

    if (downloaderVanished(step))
    {
        jump(step.onClose);
        return;
    }
    if (step.timeout > 0.0f && gStepTime > step.timeout && !step.onTimeout.empty())
    {
        jump(step.onTimeout);
        return;
    }
    if (waitSatisfied(step))
    {
        next();
    }
}

void draw(float windowW, float windowH)
{
    (void)windowW;
    (void)windowH;
    if (!gActive || gSuspended || gCur == nullptr || gIndex >= gCur->steps.size())
    {
        return;
    }
    Step& step = gCur->steps[gIndex];
    // Skip key centre, in 1920x1080 virtual units (top-right, clear of the
    // song-select header row).
    constexpr float kSkipX = 1830.0f;
    constexpr float kSkipY = 126.0f;
    // ImGui's display size, not the render size: with renderScale on (or in a
    // screenshot run at a different resolution) windowW/windowH are the
    // offscreen buffer's pixels, and the virtual canvas would be scaled twice.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const storyui::Canvas c = storyui::makeCanvas(display.x, display.y);
    ImFont* font = game::bodyFont();
    const bool ready = totalChars(step) > 0 && gReveal >= static_cast<float>(totalChars(step));

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav;
    if (step.passthrough)
    {
        flags |= ImGuiWindowFlags_NoInputs;
    }
    else
    {
        // Ask for focus every frame. ImGui's hit test walks the window list from
        // the front, and any window the player clicks (the song list's own input
        // surface) is moved to the front - above this one. With
        // NoBringToFrontOnFocus the layer stayed *below* it, so the choice pills
        // and the skip button never registered a hover: the whole tutorial was
        // click-through while the dialogue still advanced (that path asks for
        // IsMouseClicked globally, not for a hovered item).
        ImGui::SetNextWindowFocus();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    const bool visible = ImGui::Begin("##story-layer", nullptr, flags);
    if (!visible)
    {
        // Collapsed or clipped away: nothing may be drawn into a window that is
        // not being submitted. End() is still mandatory.
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        return;
    }

    // The game paints its HUD / fades on the *foreground* draw list, which is
    // rendered after every window. The layer therefore draws there too and uses
    // this window only as an input surface (Begin/End + InvisibleButton); a
    // window's own draw list would sit underneath those overlays.
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // Spotlight owns the dim when it has a target: it punches a hole, so the
    // highlighted widget stays at full brightness while everything else drops.
    ImVec2 anchorLo;
    ImVec2 anchorHi;
    const bool hasAnchor = !step.highlight.empty() && ui::anchorRect(step.highlight.c_str(), &anchorLo, &anchorHi);
    if (hasAnchor)
    {
        storyui::spotlight(dl, anchorLo, anchorHi, gTime, step.tip, step.passthrough ? 95 : 120);
    }
    else
    {
        // Image steps dim harder: the picture is the subject, and whatever is
        // behind it (the select screen's phone panel) must not compete with it.
        // 0.62 over the select screen: the dialogue's 44px white-on-#5D5D79 text
        // is meant for a story background, and the song list behind it is far
        // brighter than anything pjsk ever puts under a dialogue.
        storyui::dim(dl, step.passthrough ? 0.42f : (step.type == "image" ? 0.66f : 0.62f));
    }

    if (step.type == "scene")
    {
        const float in = std::min(gStepTime / 0.35f, 1.0f);
        const float out = std::min(std::max((gStepTime - step.hold) / 0.35f, 0.0f), 1.0f);
        storyui::sceneTitle(c, font, step.text, in * (1.0f - out));
    }
    else if (step.type == "image")
    {
        const auto it = gImages.find(step.image);
        storyui::imageCard(c, font, it == gImages.end() ? storyui::Image{} : it->second, step.caption);
    }
    else if (step.type == "cards")
    {
        std::vector<storyui::Image> images;
        std::vector<std::string> titles;
        for (const Option& opt : step.options)
        {
            titles.push_back(opt.text);
            images.push_back(ensureImage(opt.image));
        }
        const int clicked = ready
            ? storyui::cards(c, font, images, titles, step.subtitles, gTime, gPadUsed ? gPadFocus : -1)
            : -1;
        if (clicked >= 0 && clicked < static_cast<int>(step.options.size()))
        {
            gPadUsed = false;
            ui::se(ui::SeClick);
            const Option& opt = step.options[static_cast<std::size_t>(clicked)];
            applyFlags(opt.set);
            if (!opt.target.empty())
            {
                jump(opt.target);
            }
            else
            {
                next();
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            return;
        }
    }

    if (step.type != "scene")
    {
        // Compact box while a settings card is open: the full-width frame would
        // cover the card's bottom rows.
        const storyui::Box box = step.passthrough ? storyui::Box::Right
            : (step.boxLeft ? storyui::Box::Left : storyui::Box::Full);
        storyui::dialogue(c, font, step.who, step.text, step.layout, step.revealBytes, gTime,
            ready && step.options.empty(), box);
    }

    if (step.type == "choice" && ready)
    {
        std::vector<std::string> labels;
        for (const Option& opt : step.options)
        {
            labels.push_back(opt.text);
        }
        const int clicked = storyui::choices(c, font, labels, gTime, gPadUsed ? gPadFocus : -1);
        if (clicked >= 0 && clicked < static_cast<int>(step.options.size()))
        {
            gPadUsed = false;
            ui::se(ui::SeClick);
            const Option& opt = step.options[static_cast<std::size_t>(clicked)];
            applyFlags(opt.set);
            if (!opt.target.empty())
            {
                jump(opt.target);
            }
            else
            {
                next();
            }
        }
    }

    // Skip: pjsk's round key in the top-right - the same white disc the opening
    // card draws before a live (game/Intro.cpp / storyui::skipKey), so the two
    // read as one control. Draw-only: the hit test is a disc, and a hit ends the
    // tutorial (main marks it seen when consumeFinished() fires).
    if (!step.passthrough)
    {
        const ImVec2 center = c.p(kSkipX, kSkipY);
        const float radius = c.s(40.0f) + c.s(6.0f);
        ImGui::SetCursorScreenPos(ImVec2(center.x - radius, center.y - radius));
        ImGui::InvisibleButton("##story-skip", ImVec2(radius * 2.0f, radius * 2.0f));
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float dx = mouse.x - center.x;
        const float dy = mouse.y - center.y;
        const bool over = dx * dx + dy * dy <= radius * radius;
        const bool hovered = ImGui::IsItemHovered() && over;
        const bool pressed = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        // Two independent animations: hover grows the disc, press dips it.
        const float hover = ui::anim(0x5717a001u, hovered, 22.0f);
        const float press = ui::anim(0x5717a002u, pressed, 30.0f);
        storyui::skipKey(dl, c, center, hover, press, storyui::skipIcon());
        if (hovered && ImGui::IsItemClicked())
        {
            ui::se(ui::SeClick);
            finish();
            // `step` points into gCur, which finish() just released: do not fall
            // through to the advance handler below. The window still has to be
            // closed properly - ImGui asserts "Missing End()" otherwise.
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            return;
        }
    }

    // Advance on click / Space / Enter, but only on steps that are waiting for
    // the player rather than for the game. Choices are handled above and never
    // fall through to here.
    if (!step.passthrough && step.options.empty() && step.wait.empty() && step.type != "scene")
    {
        bool advance = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        advance = advance || ImGui::IsKeyPressed(ImGuiKey_Space, false)
            || ImGui::IsKeyPressed(ImGuiKey_Enter, false)
            || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        if (advance)
        {
            if (!ready)
            {
                revealAll(step);
            }
            else
            {
                next();
            }
        }
    }
    else if (step.type == "scene" && gStepTime > step.hold)
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Space, false)
            || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        {
            next();
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace story
