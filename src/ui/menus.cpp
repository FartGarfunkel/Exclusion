// ui/menus.cpp — the pause menu (STALKER style: the list on the left, a PDA card on the right), How to play, What's new
// and the "coming soon" panel used by entries whose workstream hasn't landed (skin editor, creator, LAN).
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "client/netplay.h"
#include "ui/char_preview.h"
#include "sim/zone.h"
#include <imgui.h>

namespace dq {
using namespace ui;

bool backupWorldFolder(const fs::path& folder, const std::string& name, fs::path& out, std::string& err);   // worlds.cpp
std::string agoText(i64 ms);                                                                                  // worlds.cpp

// ---- pause ---------------------------------------------------------------------------------------------
// S.T.A.L.K.E.R.'s pause: the game stays on screen, darkened, the menu a plain list of words down the left, and a PDA
// card on the right with the world, where you are, and who plays on this screen (split-screen partners can leave).
class PauseScreen : public Screen {
public:
  const char* id() const override { return "pause"; }
  std::string info;
  bool focusPending = true;
  CharPreview headView, headViews[3];
  float cardBottom = 0;
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    float z = Z(), W = (float)a.width, H = (float)a.height;
    double t = a.time;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(6, 7, 5, 120));
    float band = std::max(W * 0.5f, 480 * z);
    bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(band, H), IM_COL32(6, 7, 5, 225), IM_COL32(6, 7, 5, 0), IM_COL32(6, 7, 5, 0), IM_COL32(6, 7, 5, 225));
    zoneBackdrop(bg, ImVec2(0, 0), ImVec2(W, H), (float)t, 0.8f);
    float inset = 18 * z;
    cornerBrackets(bg, ImVec2(inset, inset), ImVec2(W - inset, H - inset), 26 * z, withAlpha(col::amber, 0.45f), 2 * z);
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##pause", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    padScrollHere();
    Level* lv = a.session ? a.session->level.get() : nullptr;
    Player* p = a.session ? a.session->player : nullptr;
    bool creative = p && p->creative();
    bool guest = netIsGuest(a);
    const bool small = H / z < 600;
    float x0 = std::max(56 * z, W * 0.075f), y = std::max(inset + 24 * z, H * (small ? 0.06f : 0.12f));
    float listW = std::min(380 * z, W - x0 * 2), bh = (small ? 32 : 38) * z, gap = (small ? 1 : 2) * z;
    // the heading: PAUSED, the world's name over it
    {
      std::string cap = upper(lv ? lv->info.name : std::string("Exclusion"));
      float capH = textSize(F_SMALL, "A").y;
      trefoil(dl, ImVec2(x0 + 8 * z, y + capH * 0.5f), 8 * z, col::amber);
      dl->AddCircle(ImVec2(x0 + 8 * z, y + capH * 0.5f), 10.5f * z, withAlpha(col::amber, 0.7f), 24, 1 * z);
      textSpaced(dl, F_SMALL, ImVec2(x0 + 26 * z, y), col::amber, ellipsize(F_SMALL, cap, listW).c_str(), 3 * z);
      y += capH + 8 * z;
      ImVec2 ts = textSpacedSize(F_TITLE, "PAUSED", 5 * z);
      textSpaced(dl, F_TITLE, ImVec2(x0 + 3 * z, y + 3 * z), IM_COL32(0, 0, 0, 200), "PAUSED", 5 * z);
      textSpaced(dl, F_TITLE, ImVec2(x0, y), col::zText, "PAUSED", 5 * z);
      grain(dl, ImVec2(x0, y), ImVec2(x0 + ts.x, y + ts.y), 1.f);
      y += ts.y + 4 * z;
      dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + 60 * z, y + 4 * z), col::rust);
      dl->AddRectFilledMultiColor(ImVec2(x0 + 60 * z, y + 1 * z), ImVec2(x0 + listW, y + 3 * z), col::amber, withAlpha(col::amber, 0), withAlpha(col::amber, 0), col::amber);
      y += (small ? 18 : 30) * z;
    }
    if (focusPending && a.input.padCount()) { ImGui::SetKeyboardFocusHere(); ImGui::SetNavCursorVisible(true); }
    if (a.input.padCount() > 0) focusPending = false;   // a pad picked up later still gets its highlight
    std::string hint;
    auto entry = [&](const char* label, const char* about) {
      ImGui::SetCursorScreenPos(ImVec2(x0 - 16 * z, y));
      bool c = mcButton(label, ImVec2(listW, bh));
      if (ImGui::IsItemHovered() || (ImGui::IsItemFocused() && navVisible())) hint = about;
      y += bh + gap;
      return c;
    };
    if (entry("Resume", "Back to the game.")) { a.popScreen(); ImGui::End(); return; }
    if (entry("Options", "Video, sound, controls and the game.")) a.pushScreen(makeOptionsScreen());
    if (entry("Profile", "Your name, skin and character, and who plays on this screen.")) a.pushScreen(makePlayersScreen());
    if (entry("How to play", "The keys, the controller, and staying alive.")) a.pushScreen(makeHowToScreen());
    // (netui.js) a guest has no world settings, backups, mode switch or LAN of its own
    if (!guest) {
      if (entry("World settings", "This world's name, mode, day and night, and rules.")) a.pushScreen(makeWorldSettingsScreen({}));
      if (entry(creative ? "Switch to survival##mode" : "Switch to creative##mode", creative ? "Back to surviving: damage, hunger, the lot." : "Every block in the inventory, flight, no damage.") && lv && p) {
        lv->info.mode = p->mode = creative ? "survival" : "creative";
        if (!p->creative()) p->flying = false;
        a.toast(p->creative() ? "Creative mode: every block in the inventory, double-tap jump to fly" : "Survival mode", "good");
      }
      if (entry("Back up world", "Save now and put a copy of this world in the backups folder.") && lv) {
        a.session->save();
        fs::path out; std::string err;
        if (backupWorldFolder(lv->folder, lv->info.name, out, err)) { info = "Backup saved: " + out.filename().string(); a.toast("Backup saved", "good"); }
        else { info = "Backup failed: " + err; a.toast(info, "warn"); }
      }
      LanStatus ls = lanStatus(a);
      std::string lanLabel = ls.open ? "LAN: " + (ls.players.empty() ? std::string("open") : std::to_string(ls.players.size()) + " joined") + "##lan" : "Open to LAN##lan";
      if (entry(lanLabel.c_str(), ls.open ? ("Open at " + ls.address + ". Others on your network can join from Multiplayer.").c_str() : "Let others on your home network play in this world with you.")) a.pushScreen(makeLanHostScreen());
    }
    y += 6 * z;
    std::string quit = guest ? "Leave " + (lv ? lv->info.name : std::string("the world")) + "##quit" : "Save and exit to menu##quit";
    if (entry(quit.c_str(), guest ? "Leave the host's world and go back to the title." : "The world is saved, then you're back at the title.")) uiDefer([](App& app) { app.leaveWorld(true); });
    if (!hint.empty()) {
      float hy = y + 10 * z;
      dl->AddRectFilled(ImVec2(x0, hy + 3 * z), ImVec2(x0 + 2 * z, hy + textSize(F_MID, hint.c_str(), listW - 10 * z).y - 1 * z), withAlpha(col::amber, 0.6f));
      text(dl, F_MID, ImVec2(x0 + 10 * z, hy), col::zDim, hint.c_str(), listW - 10 * z);
      y = hy + textSize(F_MID, hint.c_str(), listW - 10 * z).y;
    }
    // the PDA card: right of the list when there's room, under it when not
    float cardW = std::min(400 * z, W - x0 - listW - 90 * z);
    ImVec2 c0;
    if (cardW >= 280 * z) c0 = ImVec2(W - inset - 40 * z - cardW, std::max(inset + 24 * z, H * (small ? 0.06f : 0.12f)) + 20 * z);
    else { cardW = listW; c0 = ImVec2(x0, y + 28 * z); }
    int leaveIdx = drawCard(a, dl, c0, cardW, lv, p, guest);
    if (leaveIdx > 0 && a.session) a.session->removeLocal(leaveIdx);
    padHints(dl, ImVec2(W - inset - 14 * z, H - inset - 12 * z - 20 * z), {{0, "Enter", "Select"}, {1, "Esc", "Resume"}}, true);
    ImGui::SetCursorScreenPos(ImVec2(0, std::max(H - 2, cardBottom + 24 * z)));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::End();
  }
  // world · where · mode · saved, then the people on this screen (heads, devices, Leave for partners). Returns who leaves.
  int drawCard(App& a, ImDrawList* dl, ImVec2 c0, float cw, Level* lv, Player* p, bool guest) {
    float z = Z(), pad = 16 * z;
    int leaveIdx = -1;
    dl->ChannelsSplit(2); dl->ChannelsSetCurrent(1);
    float y = c0.y + 12 * z, iw = cw - pad * 2;
    auto row = [&](const char* k, const std::string& v) {
      textSpaced(dl, F_SMALL, ImVec2(c0.x + pad, y + 2 * z), col::zDim, k, 1.5f * z);
      std::string vv = ellipsize(F_BODY, v, iw - 110 * z);
      text(dl, F_BODY, ImVec2(c0.x + pad + 110 * z, y), col::zText, vv.c_str());
      y += textSize(F_BODY, "A").y + 6 * z;
    };
    textSpaced(dl, F_BODY, ImVec2(c0.x + pad, y), col::amber, "PDA \xC2\xB7 STATUS", 2 * z);
    y += textSize(F_BODY, "A").y + 6 * z;
    dl->AddRectFilledMultiColor(ImVec2(c0.x + pad, y), ImVec2(c0.x + cw - pad, y + 1 * z), col::zEdgeHi, withAlpha(col::zEdge, 0), withAlpha(col::zEdge, 0), col::zEdgeHi);
    y += 8 * z;
    if (lv && p) {
      row("WORLD", lv->info.name + (guest ? " (hosted)" : ""));
      row("AREA", zone::regionName(ifloor(p->pos.x), ifloor(p->pos.z)));
      row("MODE", p->creative() ? "Creative" : lv->info.rules.is_object() && lv->info.rules.value("hardcore", false) ? "Hardcore" : "Survival");
      if (lv->info.lastPlayed && !guest) row("SAVED", agoText(lv->info.lastPlayed));
    }
    if (!info.empty()) { y += 2 * z; y += textLines(dl, F_MID, ImVec2(c0.x + pad, y), col::amberHi, info.c_str(), iw, textSize(F_MID, "A").y + 2 * z); }
    y += 10 * z;
    textSpaced(dl, F_SMALL, ImVec2(c0.x + pad, y), col::zDim, "ON THIS SCREEN", 1.5f * z);
    y += textSize(F_SMALL, "A").y + 8 * z;
    // .pause-party (ui.js ~305): one row per local player, Leave for the partners, how to join
    float rowH = 30 * z;
    GameSession* gs = a.session.get();
    int rows = gs ? (int)gs->locals.size() : 1;
    static const u32 PCOL[4] = {0xe2b84c, 0x8fc7d8, 0xa8c46a, 0xd09ad8};
    for (int r = 0; r < rows; r++) {
      Player* rp = gs ? gs->locals[r].player : p;
      ImVec2 head(c0.x + pad, y);
      // your head as a little 3D cube seen from above and to one side (the real voxel head)
      if (rp) {
        auto spec = resolveCharacter(rp->skin);
        CharPreviewOpts ho; ho.headOnly = true; ho.yaw = PI - 0.62f; ho.elev = 0.48f; ho.dist = 30; ho.time = (float)a.time;
        CharPose hp; hp.still = true; ho.pose = &hp;
        float fb = ImGui::GetIO().DisplayFramebufferScale.x, hs = rowH * 1.25f;
        GLuint tex = (r == 0 ? headView : headViews[std::min(r, 3) - 1]).render(*spec, (int)(hs * fb), (int)(hs * fb), ho);
        if (tex) dl->AddImage((ImTextureID)(intptr_t)tex, head + ImVec2(-rowH * 0.12f, -rowH * 0.14f), head + ImVec2(-rowH * 0.12f, -rowH * 0.14f) + ImVec2(hs, hs), ImVec2(0, 1), ImVec2(1, 0));
      }
      dl->AddRectFilled(head + ImVec2(0, rowH - 2 * z), head + ImVec2(rowH, rowH), col::rgb(PCOL[r & 3]));
      text(dl, F_BODY, ImVec2(head.x + rowH + 10 * z, y + (rowH - textSize(F_BODY, "A").y) * 0.5f), col::rgb(PCOL[r & 3]), rp ? rp->name.c_str() : "");
      std::string dev;
      if (!gs || !gs->split()) dev = std::string("Keyboard & mouse") + (a.input.padCount() ? " + pad" : "");
      else { const InputSource& src = gs->locals[r].src; dev = src.kb ? "Keyboard & mouse" : ""; if (!src.pads.empty()) dev += std::string(src.kb ? " + " : "") + "Controller"; }
      ImVec2 ds = textSize(F_SMALL, dev.c_str());
      float right = c0.x + cw - pad;
      if (r > 0) {   // a partner can leave
        float lw = textSpacedSize(F_MID, "LEAVE", 1 * z).x + 24 * z;
        ImGui::SetCursorScreenPos(ImVec2(right - lw, y + (rowH - 30 * z) * 0.5f));
        if (button(("Leave##party" + std::to_string(r)).c_str(), BTN_NORMAL, true, lw)) leaveIdx = r;
        right -= lw + 10 * z;
      }
      text(dl, F_SMALL, ImVec2(right - ds.x, y + (rowH - ds.y) * 0.5f), col::zDim, dev.c_str());
      y += rowH + 8 * z;
    }
    const char* hint = rows >= 4 ? "Four players is the most that fit on one screen."
                     : a.input.padCount() > (rows > 1 ? rows - 1 : 0) ? "A free controller is connected \xE2\x80\x94 press Start on it to join."
                     : "Split screen: plug in another controller and press Start on it to join.";
    y += textLines(dl, F_MID, ImVec2(c0.x + pad, y), col::zDim, hint, iw, textSize(F_MID, "A").y + 2 * z) + 12 * z;
    ImVec2 c1(c0.x + cw, y);
    cardBottom = y;
    dl->ChannelsSetCurrent(0);
    dl->AddRectFilledMultiColor(c0, c1, col::rgb(0x1a1b14, 235), col::rgb(0x17180f, 235), col::rgb(0x111209, 235), col::rgb(0x121309, 235));
    grain(dl, c0, c1, 1);
    dl->AddRect(c0, c1, col::zEdge, 0, 0, 1 * z);
    cornerBrackets(dl, c0 - ImVec2(3, 3) * z, c1 + ImVec2(3, 3) * z, 12 * z, withAlpha(col::amber, 0.85f), 2 * z);
    dl->ChannelsMerge();
    return leaveIdx;
  }
};
std::unique_ptr<Screen> makePauseScreen() { return std::make_unique<PauseScreen>(); }

// ---- simple text panels -------------------------------------------------------------------------------
namespace {
struct Block { int kind; std::string a, b; };   // 0 h3, 1 paragraph, 2 key row (a = keys, b = what), 3 numbered
class TextPanel : public Screen {
public:
  TextPanel(const char* id, std::string t, std::string close, float w, std::vector<std::vector<Block>> cols) : sid(id), title(std::move(t)), closeLbl(std::move(close)), width(w), columns(std::move(cols)) {}
  const char* sid; std::string title, closeLbl; float width; std::vector<std::vector<Block>> columns;
  const char* id() const override { return sid; }
  bool pausesGame() const override { return true; }
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    Panel pn;
    pn.begin((std::string("##") + sid).c_str(), title.c_str(), width, closeLbl.c_str());
    pn.beginBody();
    float z = Z(), pad = 16 * z, gapc = 20 * z;
    int nc = (int)columns.size();
    if (pn.width < 560 * z) nc = 1;   // one column on narrow screens
    float colW = (pn.width - pad * 2 - gapc * (nc - 1)) / nc;
    ImGui::SetCursorPos(ImVec2(pad, pad));
    std::vector<Block> flat; std::vector<std::vector<Block>> cols = columns;
    if (nc == 1 && columns.size() > 1) { for (auto& c : columns) flat.insert(flat.end(), c.begin(), c.end()); cols = {flat}; }
    float top = ImGui::GetCursorPosY(), maxY = top;
    for (int c = 0; c < (int)cols.size(); c++) {
      float x = pad + c * (colW + gapc);
      ImGui::SetCursorPos(ImVec2(x, top));
      ImGui::BeginGroup();
      int num = 0;
      for (auto& b : cols[c]) {
        if (b.kind == 0) { ImGui::Dummy(ImVec2(1, 4 * z)); sectionLabel(b.a.c_str(), colW); num = 0; }
        else if (b.kind == 1) { label(F_BODY, col::muted, b.a.c_str(), colW); ImGui::Dummy(ImVec2(1, 4 * z)); }
        else if (b.kind == 2) {
          float kw = 130 * z; ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
          float h1 = textSize(F_MID, b.a.c_str(), kw).y, h2 = textSize(F_MID, b.b.c_str(), colW - kw - 12 * z).y;
          text(dl, F_MID, p, col::amber, b.a.c_str(), kw); text(dl, F_MID, ImVec2(p.x + kw + 12 * z, p.y), col::muted, b.b.c_str(), colW - kw - 12 * z);
          ImGui::Dummy(ImVec2(colW, std::max(h1, h2) + 2 * z));
        } else {
          std::string n = std::to_string(++num) + ".";
          ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
          text(dl, F_BODY, p, col::muted, n.c_str());
          float hh = textSize(F_BODY, b.a.c_str(), colW - 22 * z).y;
          text(dl, F_BODY, ImVec2(p.x + 22 * z, p.y), col::muted, b.a.c_str(), colW - 22 * z);
          ImGui::Dummy(ImVec2(colW, hh + 4 * z));
        }
      }
      ImGui::EndGroup();
      maxY = std::max(maxY, ImGui::GetItemRectMax().y - ImGui::GetWindowPos().y + ImGui::GetScrollY());
    }
    ImGui::SetCursorPos(ImVec2(0, maxY)); ImGui::Dummy(ImVec2(1, pad));
    pn.endBody();
    pn.end();
    if (pn.closed) a.popScreen();
  }
};
}

std::unique_ptr<Screen> makeHowToScreen() {
  std::vector<Block> c1 = {
    {0, "Keyboard & mouse", ""},
    {2, "W A S D", "Move (Ctrl sprint \xC2\xB7 Shift crouch \xC2\xB7 Space jump)"}, {2, "Left click", "Fire \xC2\xB7 knife \xC2\xB7 throw a bolt (detector in hand)"},
    {2, "Right click", "Aim down the sights \xC2\xB7 use (talk to a trader, open a crate, climb a hatch)"},
    {2, "R", "Reload \xC2\xB7 clear a jam"}, {2, "V \xC2\xB7 middle click", "Underbarrel launcher"},
    {2, "1\xE2\x80\x93" "4", "Primary \xC2\xB7 secondary \xC2\xB7 sidearm \xC2\xB7 knife"}, {2, "5\xE2\x80\x93" "9", "Quick slots (meds, food, grenades, bolts, the detector)"},
    {2, "H", "Quick bandage or medkit"}, {2, "L", "Head torch"}, {2, "E", "Inventory"}, {2, "Tab \xC2\xB7 M", "PDA: map, tasks, news, contacts, radio"},
    {2, "U", "Weapon bench: fit and strip attachments"}, {2, "Q", "Drop"}, {2, "F5", "First or third person"}, {2, "Esc", "Pause"},
    {0, "Controller", ""},
    {2, "Sticks", "Move \xC2\xB7 look (click the left stick to sprint)"}, {2, "RT \xC2\xB7 LT", "Fire \xC2\xB7 aim / use"},
    {2, "A \xC2\xB7 B", "Jump \xC2\xB7 crouch"}, {2, "X", "Reload"}, {2, "Y", "Inventory"}, {2, "LB \xC2\xB7 RB", "Weapons and quick slots"},
    {2, "D-pad up \xC2\xB7 down", "Head torch \xC2\xB7 quick heal"}, {2, "RS click", "Underbarrel launcher"}, {2, "Back", "PDA"},
    {2, "Start", "Pause \xC2\xB7 on a new controller, join split screen"},
  };
  std::vector<Block> c2 = {
    {0, "Staying alive", ""},
    {1, "You start with next to nothing: a worn pistol, a few rounds, a bandage and some bread. Search ruins, crates and lockers, and the dead, for the rest.", ""},
    {1, "Eat, drink and sleep, or you weaken. Wounds bleed until you bandage them. Healing is slow, and only when you are fed and watered.", ""},
    {1, "Radiation builds up from hot ground, some regions and artifacts. Anti-rad drugs and vodka bring it down. Watch the counter.", ""},
    {1, "Everything weighs something. Carry too much and you slow down; far too much and you cannot run.", ""},
    {0, "Anomalies and artifacts", ""},
    {1, "Anomalies burn, shock, crush and dissolve. Throw a bolt ahead of you: it sets them off. Artifacts lie inside the fields.", ""},
    {1, "Hold a detector up to find them: the Echo beeps, the Bear points the way, the Veles shows them on its screen. Artifacts on your belt help you, and irradiate you.", ""},
    {0, "Emissions", ""},
    {1, "When the sirens wail and the sky turns red, get under a solid roof or into your bunker. Anyone caught outside when the wave hits dies.", ""},
    {0, "The people of the Zone", ""},
    {1, "Loners, Bandits, the Military, Duty, Freedom, Mercenaries, the Monolith and the Ecologists. Some will trade with you, some will shoot on sight. Your PDA's contacts show who is where.", ""},
    {1, "Traders buy what you find and sell what you need. Artifacts fetch the best prices.", ""},
    {0, "Guns", ""},
    {1, "Fit sights, suppressors, grips and magazines at the weapon bench, and strip them again. Worn guns jam: clear them with reload, and repair them with a kit.", ""},
    {0, "Your bunker", ""},
    {1, "The iron hatch in Old Hamlet leads down to your own room. Store your finds there, dig it out and furnish it. It is the one place no emission can reach.", ""},
    {0, "Your worlds are safe", ""},
    {1, "The game saves as you play, and when you leave from the pause menu.", ""},
  };
  return std::make_unique<TextPanel>("howto", "How to play", "Close", 860, std::vector<std::vector<Block>>{c1, c2});
}

std::unique_ptr<Screen> makeNewsScreen() {
  // the same notes the launcher shows: data/news.json [{version, date, title, items[]}] when it is there
  std::vector<Block> b;
  if (auto j = readJson(dataDir() / "news.json"); j && j->is_array()) {
    for (auto& e : *j) {
      b.push_back({0, e.value("version", "") + (e.contains("date") ? "  " + e.value("date", "") : ""), ""});
      if (e.contains("title")) b.push_back({1, e.value("title", ""), ""});
      if (e.contains("items")) for (auto& it : e["items"]) if (it.is_string()) b.push_back({2, "\xC2\xB7", it.get<std::string>()});
    }
  }
  if (b.empty()) b = {{0, std::string("Exclusion ") + DQ_VERSION, ""}, {1, "The patch notes for this version are in the launcher's News tab.", ""}};
  return std::make_unique<TextPanel>("news", "What's new", "Close", 860, std::vector<std::vector<Block>>{b});
}

std::unique_ptr<Screen> makeComingSoonScreen(const std::string& title, const std::string& txt) {
  return std::make_unique<TextPanel>("comingsoon", title, "Back", 460, std::vector<std::vector<Block>>{{{0, "Coming soon", ""}, {1, txt, ""}}});
}

// ---- players & skins: ui/players_screen.cpp


} // namespace dq
