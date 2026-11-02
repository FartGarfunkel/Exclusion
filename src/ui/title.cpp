// ui/title.cpp — the title screen, S.T.A.L.K.E.R. style: a slow look round a block of flats in the Zone (the
// panorama from game/game.js, now on a Zone site) under a dark grade with drifting dust, the EXCLUSION name in
// stencilled capitals, the menu as a plain list of words on the left (CONTINUE / NEW GAME / LOAD GAME / ...), a
// line about the picked entry, and the version line (What's new) at the foot.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "render/world_renderer.h"
#include "render/sky.h"
#include "render/gl.h"
#include "common/noise.h"
#include "gen/zone_plan.h"
#include <imgui.h>
#include <set>

namespace dq {

static const char* const SPLASHES[] = {
  "Fresh air is overrated.", "The dosimeter is clicking again.", "Mind the anomalies.", "Nobody is coming.", "Boots, bandages, bread.",
  "Count your rounds.", "Bolts first, bullets later.", "Don't drink the water.", "Emission in five minutes.", "Dig in before dark.",
  "Rust never sleeps.", "Silence is worse than gunfire.", "Clean your rifle.", "Ammo is currency.", "Watch the treeline.",
  "The fence was the easy part.", "Wait out the storm.", "Carry less, live longer.", "Vodka is not medicine.", "Leave one lamp burning.",
  "Split screen: share the misery.", "The artifact was worth it. Probably.", "Cold soup, warm bunker.", "Every shadow has a rifle.",
};

// Slowly orbiting view of a demo world behind the title menu (game.js startPanorama).
struct Panorama {
  std::unique_ptr<Level> lv;
  WorldRenderer wr;
  std::unique_ptr<SkyRenderer> sky;
  FrameCtx frame;
  fs::path folder;
  float yaw = 0.6f, pitch = -0.28f; double y = 0, cx = 8, cz = 8, radius = 0;
  bool ok = false;
  bool start(App& a) {
    folder = userSub("cache") / "title-panorama";
    std::error_code ec; fs::remove_all(folder, ec); fs::create_directories(folder, ec);
    LevelInfo info; info.name = "title"; info.seedText = "emberwild-title"; info.seed = seedFromString("emberwild-title");
    info.time = 0.18 * DAY_LEN;
    lv = std::make_unique<Level>(info, folder, a.pool.get(), &a.events);
    frame.settings = &a.settings;
    if (!wr.init(a.settings, a.tileTex)) return false;
    sky = std::make_unique<SkyRenderer>(); sky->init();
    wr.setWorld(&lv->world());
    // the Zone: circle a standing block of flats (the first panel block that isn't rubble); else the old look round 8,8
    const zone::Plan& plan = zone::zonePlan((u32)info.seed);
    for (const zone::Placed& s : plan.s) if (s.type == zone::S_PANEL && s.ruin < 0.5f) {
      cx = s.x0 + s.W * 0.5; cz = s.z0 + s.D * 0.5; radius = 40; pitch = -0.04f;
      y = dimBase(DIM_OVERWORLD) + s.y + 7;
      break;
    }
    if (radius == 0) {
      int top = 0;
      if (DimGenerator* g = lv->gen->forDim(DIM_OVERWORLD))
        for (int dz = -40; dz <= 40; dz += 8) for (int dx = -40; dx <= 40; dx += 8) { ColumnSample s; g->sample(8 + dx, 8 + dz, s); top = std::max(top, std::max((int)s.height, 64)); }
      y = dimBase(DIM_OVERWORLD) + top + 10; radius = 6;
    }
    ok = true;
    return true;
  }
  void stop() {
    if (!lv) return;
    wr.setWorld(nullptr);
    lv.reset();
    std::error_code ec; fs::remove_all(folder, ec);
    ok = false;
  }
  void render(App& a) {
    if (!ok) return;
    yaw += (float)a.dt * 0.025f;
    int R = std::min(a.settings.renderDist, 4);
    World& w = lv->world();
    Camera& cam = frame.cam;
    cam.pos = glm::dvec3(cx + std::sin(yaw) * radius, y, cz + std::cos(yaw) * radius);
    cam.yaw = yaw + PI; cam.pitch = pitch; cam.fov = 70;
    std::vector<StreamAnchor> anchors = {{vec3(cam.pos), R, false}};
    w.stream(anchors, 3.0);
    int dw, dh; SDL_GL_GetDrawableSize(a.window, &dw, &dh);
    cam.aspect = dh > 0 ? (float)dw / dh : 1.f; cam.farZ = 1024;
    cam.update();
    frame.time = a.time; frame.width = dw; frame.height = dh;
    sky->computeFrame(*lv, frame, R);
    cam.update();
    wr.update(cam, *a.pool, 3.0);
    glViewport(0, 0, dw, dh);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    sky->draw(frame);
    wr.drawOpaque(frame); wr.drawCutout(frame); wr.drawTranslucent(frame);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  }
};

bool latestWorld(fs::path& folder, std::string& name, i64& lastPlayed);   // ui/worlds.cpp: the world played last
std::unique_ptr<Screen> makeWorldsScreenFor(bool newGame);               // ui/worlds.cpp: NEW GAME starts on the form
std::string agoText(i64 ms);                                              // ui/worlds.cpp

// EXCLUSION in stencilled capitals: spaced Jersey letters, a stencil bridge cut through them, a rust-and-amber rule under
static void drawLogo(ImDrawList* dl, ImVec2 p, float maxW, float z, double t) {
  using namespace ui;
  const char* word = "EXCLUSION";
  float sp = 7 * z;
  ImVec2 ts = textSpacedSize(F_HUGE, word, sp);
  float k = std::min(1.6f, maxW / std::max(1.f, ts.x));
  ts = textSpacedSize(F_HUGE, word, sp * k, k);
  // the caption above: the sign and where you are
  float capH = textSize(F_SMALL, "A").y;
  trefoil(dl, ImVec2(p.x + 8 * z, p.y + capH * 0.5f), 8 * z, col::amber);
  dl->AddCircle(ImVec2(p.x + 8 * z, p.y + capH * 0.5f), 10.5f * z, withAlpha(col::amber, 0.7f), 24, 1 * z);
  textSpaced(dl, F_SMALL, ImVec2(p.x + 26 * z, p.y), col::amber, "CHERNOBYL ZONE OF ALIENATION", 3 * z);
  float y = p.y + capH + 6 * z;
  // the letters (a hard shadow, then the face), flickering very slightly like an old screen
  float fl = 0.92f + 0.08f * (float)std::sin(t * 13.0) * (float)std::sin(t * 3.1);
  textSpaced(dl, F_HUGE, ImVec2(p.x + 3 * z, y + 4 * z), IM_COL32(0, 0, 0, 210), word, sp * k, k);
  textSpaced(dl, F_HUGE, ImVec2(p.x, y), withAlpha(col::zText, fl), word, sp * k, k);
  // stencil bridges: two thin dark cuts across the letters, and the grain over them
  float cut = std::round(y + ts.y * 0.47f);
  dl->AddRectFilled(ImVec2(p.x - 2 * z, cut), ImVec2(p.x + ts.x + 4 * z, cut + 2.5f * z * k), IM_COL32(10, 11, 8, 235));
  dl->AddRectFilled(ImVec2(p.x - 2 * z, std::round(y + ts.y * 0.76f)), ImVec2(p.x + ts.x + 4 * z, std::round(y + ts.y * 0.76f) + 1 * z), IM_COL32(10, 11, 8, 150));
  grain(dl, ImVec2(p.x, y), ImVec2(p.x + ts.x, y + ts.y), 1.2f);
  // the rule
  float ry = y + ts.y + 4 * z;
  dl->AddRectFilled(ImVec2(p.x, ry), ImVec2(p.x + 70 * z, ry + 4 * z), col::rust);
  dl->AddRectFilledMultiColor(ImVec2(p.x + 70 * z, ry + 1 * z), ImVec2(p.x + ts.x, ry + 3 * z), col::amber, withAlpha(col::amber, 0), withAlpha(col::amber, 0), col::amber);
}

class TitleScreen : public Screen {
public:
  const char* id() const override { return "title"; }
  bool showsWorld() const override { return false; }
  bool pausesGame() const override { return false; }
  std::string splash;
  Panorama pano;
  bool focusPending = true, askQuit = false, wasTop = false;
  double newsAt = -1;
  fs::path lastFolder; std::string lastName; i64 lastPlayed = 0;
  int hotIdx = -1;
  void onOpen(App& a) override {
    splash = SPLASHES[(size_t)(unixMillis() / 7) % (sizeof SPLASHES / sizeof SPLASHES[0])];
    if (!a.session) pano.start(a);
    newsCheck(a);
  }
  void onClose(App& a) override { pano.stop(); }
  bool onBack(App&) override { return askQuit; }   // the quit box answers B itself
  std::string versionText() const {
    std::string v = DQ_VERSION; size_t p = v.find("-alpha"); if (p != std::string::npos) v.erase(p, 6);
    return "EXCLUSION ALPHA v" + v + "  \xC2\xB7  WHAT'S NEW";
  }
  // The first time a new version runs, the title screen opens its notes once (players new to the game just get the "New" tag)
  void newsCheck(App& a) {
    std::string seen = a.settings.extra.value("seenVersion", "");
    if (seen == DQ_VERSION || a.args.smokeFrames > 0 || !a.args.screen.empty()) return;
    bool played = !seen.empty() || a.settings.extra.value("played", false);
    if (played) newsAt = a.time + 0.4;
  }
  void markSeen(App& a) { a.settings.extra["seenVersion"] = DQ_VERSION; a.settings.save(); }
  void update(App& a, double) override {
    bool top = a.topScreen() == this;
    if (top && !wasTop) { lastFolder.clear(); lastName.clear(); latestWorld(lastFolder, lastName, lastPlayed); }   // back on the title: the newest world again
    wasTop = top;
    if (newsAt > 0 && a.time >= newsAt && top) { newsAt = -1; markSeen(a); a.pushScreen(makeNewsScreen()); }
    if (top && !askQuit && a.input.pressed(Act::MenuStart)) uiDefer([](App& app) { app.pushScreen(makeWorldsScreen()); });
  }
  void draw(App& a) override {
    using namespace ui;
    if (a.topScreen() == this || !a.session) pano.render(a);
    float z = Z(), W = (float)a.width, H = (float)a.height;
    double t = a.time;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    // the grade: the Zone darkened and cooled, much darker down the left where the menu stands
    if (!pano.ok) bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), col::zBg);
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(10, 12, 8, 120));
    float band = std::max(W * 0.55f, 520 * z);
    bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(band, H), IM_COL32(6, 7, 5, 225), IM_COL32(6, 7, 5, 0), IM_COL32(6, 7, 5, 0), IM_COL32(6, 7, 5, 225));
    trefoil(bg, ImVec2(W * 0.74f, H * 0.46f), std::min(W, H) * 0.26f, IM_COL32(226, 184, 76, 9), (float)t * 0.02f);
    bg->AddCircle(ImVec2(W * 0.74f, H * 0.46f), std::min(W, H) * 0.30f, IM_COL32(226, 184, 76, 12), 96, 2 * z);
    zoneBackdrop(bg, ImVec2(0, 0), ImVec2(W, H), (float)t);
    // the PDA frame round the screen, with a dosimeter reading that won't sit still
    float inset = 18 * z;
    cornerBrackets(bg, ImVec2(inset, inset), ImVec2(W - inset, H - inset), 26 * z, withAlpha(col::amber, 0.45f), 2 * z);
    {
      float rad = 0.28f + 0.05f * (float)std::sin(t * 0.9) + 0.03f * (float)std::sin(t * 4.3 + 1);
      char buf[64]; std::snprintf(buf, sizeof buf, "DOSIMETER  %.2f \xC2\xB5Sv/h", rad);
      ImVec2 ds = textSpacedSize(F_SMALL, buf, 1.5f * z);
      ImVec2 dp(W - inset - 14 * z - ds.x, inset + 12 * z);
      bool blink = std::fmod(t, 1.6) < 0.8;
      bg->AddCircleFilled(ImVec2(dp.x - 9 * z, dp.y + ds.y * 0.5f), 3 * z, blink ? col::amber : withAlpha(col::amber, 0.25f), 10);
      textSpaced(bg, F_SMALL, dp, col::zDim, buf, 1.5f * z);
    }
    if (a.topScreen() != this) return;
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##title", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool small = H / z < 560;
    float x0 = std::max(56 * z, W * 0.075f);
    float logoW = std::min(std::min(W - x0 * 2, 600 * z), W * 0.62f);
    float y0 = std::max(inset + 22 * z, H * (small ? 0.07f : 0.12f));
    drawLogo(dl, ImVec2(x0, y0), logoW, z, t);
    float logoH = textSize(F_SMALL, "A").y + 6 * z + textSpacedSize(F_HUGE, "EXCLUSION", 0, std::min(1.6f, logoW / std::max(1.f, textSpacedSize(F_HUGE, "EXCLUSION", 7 * z).x))).y + 10 * z;
    // the splash: a line off the radio
    std::string sl = "\xE2\x80\x94 " + splash;
    text(dl, F_MID, ImVec2(x0, y0 + logoH + 6 * z), col::zDim, sl.c_str());
    // the menu
    float btnW = std::min(380 * z, W - x0 * 2), btnH = (small ? 32 : 40) * z, gap = (small ? 1 : 3) * z;
    float by = y0 + logoH + (small ? 30 : 56) * z;
    if (focusPending && a.input.padCount() > 0) { ImGui::SetKeyboardFocusHere(); ImGui::SetNavCursorVisible(true); }
    if (a.input.padCount() > 0) focusPending = false;   // a pad picked up later still gets its highlight
    ImGui::BeginDisabled(askQuit);
    int hot = -1, idx = 0;
    std::string hint;
    auto entry = [&](const char* label, const std::string& about) {
      ImGui::SetCursorScreenPos(ImVec2(x0 - 16 * z, by));
      bool c = mcButton(label, ImVec2(btnW, btnH));
      if (ImGui::IsItemHovered() || (ImGui::IsItemFocused() && navVisible())) { hot = idx; hint = about; }
      by += btnH + gap; idx++;
      return c;
    };
    bool canContinue = !lastFolder.empty();
    if (canContinue && entry("Continue", "Back to " + lastName + (lastPlayed ? " \xC2\xB7 last saved " + agoText(lastPlayed) : std::string()))) {
      fs::path f = lastFolder; a.settings.extra["played"] = true; uiDefer([f](App& app) { app.openWorld(f); });
    }
    if (entry("New game", "Make a new world and walk into the Zone.")) a.pushScreen(makeWorldsScreenFor(true));
    if (entry("Load game", "Your worlds: play, change their settings, back them up. They save on their own as you play.")) a.pushScreen(makeWorldsScreenFor(false));
    if (entry("Multiplayer", "Join a world on your network or at an address. Split screen: plug in a controller and press Start.")) a.pushScreen(makeMultiplayerScreen());
    if (entry("Profile", "Your name, skin and character, and who plays on this screen.")) a.pushScreen(makePlayersScreen());
    if (entry("How to play", "The keys, the controller, and staying alive.")) a.pushScreen(makeHowToScreen());
    if (entry("Options", "Video, sound, controls and the game.")) a.pushScreen(makeOptionsScreen());
    by += 8 * z;
    if (entry("Quit", "Leave the Zone for now.")) askQuit = true;
    ImGui::EndDisabled();
    if (hot >= 0) hotIdx = hot;
    // the line about the picked entry
    if (!hint.empty()) {
      float hy = by + 10 * z;
      dl->AddRectFilled(ImVec2(x0, hy + 3 * z), ImVec2(x0 + 2 * z, hy + textSize(F_MID, "A", btnW).y - 1 * z), withAlpha(col::amber, 0.6f));
      text(dl, F_MID, ImVec2(x0 + 10 * z, hy), col::zDim, hint.c_str(), btnW - 10 * z);
    }
    // version (bottom left) and the controls (bottom right)
    std::string ver = versionText();
    bool isNew = a.settings.extra.value("seenVersion", "") != DQ_VERSION;
    ImVec2 vs = textSpacedSize(F_SMALL, ver.c_str(), 1.5f * z);
    ImVec2 vp(inset + 14 * z, H - inset - 12 * z - vs.y);
    ImGui::SetCursorScreenPos(vp);
    ImGui::BeginDisabled(askQuit);
    bool vClick = ImGui::InvisibleButton("##version", vs, ImGuiButtonFlags_EnableNav);
    ImGui::EndDisabled();
    bool vHot = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && navVisible());
    textSpaced(dl, F_SMALL, vp, vHot ? col::amberHi : col::zDim, ver.c_str(), 1.5f * z);
    if (vHot) dl->AddLine(ImVec2(vp.x, vp.y + vs.y + 1 * z), ImVec2(vp.x + vs.x, vp.y + vs.y + 1 * z), col::amber, 1 * z);
    if (isNew) {
      ImVec2 tp(vp.x + vs.x + 10 * z, vp.y - 2 * z); ImVec2 ts = textSpacedSize(F_SMALL, "NEW", 1.5f * z);
      dl->AddRectFilled(tp, tp + ImVec2(ts.x + 10 * z, vs.y + 4 * z), col::rust);
      textSpaced(dl, F_SMALL, tp + ImVec2(5 * z, 2 * z), col::rgb(0xffe9c8), "NEW", 1.5f * z);
    }
    if (vClick) { ui::playClick(); markSeen(a); a.pushScreen(makeNewsScreen()); }
    padHints(dl, ImVec2(W - inset - 14 * z, H - inset - 12 * z - 20 * z), {{0, "Enter", "Select"}, {-1, "\xE2\x86\x91\xE2\x86\x93", "Move"}}, true);
    if (askQuit) {
      int r = confirmBox("##quitbox", "Quit", "Leave the Zone and close the game?", "Quit", "Stay");
      if (r == 1) a.quit = true;
      if (r >= 0) askQuit = false;
    }
    ImGui::End();
  }
};

std::unique_ptr<Screen> makeTitleScreen() { return std::make_unique<TitleScreen>(); }

} // namespace dq
