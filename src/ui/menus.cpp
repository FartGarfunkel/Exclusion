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
    {2, "W A S D", "Move (double-tap W or hold Ctrl to sprint)"}, {2, "Space", "Jump \xC2\xB7 swim up"},
    {2, "Shift", "Sneak (won't walk off edges, chops one log at a time)"}, {2, "Left click", "Mine \xC2\xB7 attack"},
    {2, "Right click", "Place \xC2\xB7 use \xC2\xB7 eat \xC2\xB7 open"},
    {2, "Shift + right click", "Empty hand: pick up a chest, furnace or animal and carry it. Holding an item: set it down on the ground"},
    {2, "1\xE2\x80\x93" "9 \xC2\xB7 wheel", "Choose hotbar slot"}, {2, "R", "Placing mode: vertical slabs, upside-down stairs and trapdoors"},
    {2, "F", "Swap main hand and off hand"}, {2, "E", "Inventory and crafting"}, {2, "Q", "Drop item"},
    {2, "T \xC2\xB7 /", "Chat \xC2\xB7 commands (/help lists them)"}, {2, "F1 \xC2\xB7 F2", "Hide the HUD \xC2\xB7 take a screenshot"},
    {2, "F3 \xC2\xB7 F5", "Debug info \xC2\xB7 first or third person"}, {2, "Esc", "Pause"},
    {0, "Controller", ""},
    {2, "Sticks", "Move \xC2\xB7 look (click left stick to sprint)"}, {2, "RT \xC2\xB7 LT", "Mine \xC2\xB7 use"}, {2, "A \xC2\xB7 B", "Jump \xC2\xB7 sneak"},
    {2, "Y \xC2\xB7 X", "Inventory \xC2\xB7 drop"}, {2, "LB \xC2\xB7 RB", "Hotbar (in the inventory: recipe book)"}, {2, "D-pad down", "Swap hands"},
    {2, "Start", "Pause \xC2\xB7 on a new controller, join split-screen"},
    {0, "Touch", ""},
    {2, "Left thumb", "Move (push to the top edge to sprint)"}, {2, "Drag right side", "Look around"}, {2, "Tap", "Place \xC2\xB7 use"},
    {2, "Press and hold", "Mine (or hold the Mine button)"},
  };
  std::vector<Block> c2 = {
    {0, "Surviving", ""},
    {1, "There is no hunger. Eating any food heals you on the spot \xE2\x80\x94 apples, berries, and roast boar are your lifeline. You have ten hearts, as in the classic game. Heart Crystals (from bosses) add one for good; charms worn in the charm slot add more while you wear them, or guard against fire, water and falls.", ""},
    {1, "Under water, watch your air bubbles; when they run out you drown. Sprint while under water to swim. If you die, your things wait in a grave and you're given a compass that points to it.", ""},
    {1, "At night and in dark caves the monsters come out. Ghouls shamble at you, skeletons keep their distance and shoot, spiders climb walls and pounce, and fuselings creep close, hiss and burst \xE2\x80\x94 back away when you hear one. Slimes hop about deep underground. Spiders cling to cave roofs and drop on you from above. A good axe or sword can take a limb off: a ghoul without a leg crawls, a spider short of legs can't leap, a skeleton that loses its bow arm drops the bow (and may try to grab it back). Torches keep the dark away \xE2\x80\x94 hold one in your off hand, or hang a torch or lantern on your belt hook (it moves to your backpack when you wear one) and it lights up everything around you as you walk.", ""},
    {0, "Crafting", ""},
    {1, "Crafting works on the grid, the classic way: 2\xC3\x97" "2 in your inventory, 3\xC3\x97" "3 at a crafting table. Stuck? Open the recipe book beside the grid \xE2\x80\x94 it shows what you can make and can lay the ingredients out for you.", ""},
    {0, "Progression", ""},
    {3, "Punch trees. Chop the base of a tree and the whole thing falls.", ""}, {3, "Craft a crafting table, then wooden and stone tools.", ""},
    {3, "Smelt iron ore at a furnace. Coal or wood is fuel.", ""},
    {3, "Craft an Ember Effigy and summon the Cinder Golem. Watch the ground \xE2\x80\x94 every attack is marked before it lands.", ""},
    {3, "Its Cinder Core makes an Ember Igniter. Light an obsidian frame to enter the Ember Depths and mine cinderite.", ""},
    {3, "A frame of Glow Lanterns filled with water opens the Skylands. Aether crystals there make a Pocket Key \xE2\x80\x94 your own private dimension.", ""},
    {0, "Farms and animals", ""},
    {1, "Cows, pigs, sheep and chickens roam the grasslands. Feed two of a kind (wheat, carrots or seeds) and they'll have a baby. Milk cows with a bucket, shear sheep, and carry an animal home with an empty hand while sneaking \xE2\x80\x94 animals you set down stay put.", ""},
    {1, "Bone meal (from bones, or a full composter) ripens crops and saplings. Shovels make paths, hoes till soil, and scaffolding lets you climb and sneak back down.", ""},
    {0, "Workstations", ""},
    {1, "Anvils repair and join tools, the loom and smithing table dye and upgrade, the fletching table makes arrows, the stonecutter and sawmill cut blocks. A bow shoots arrows; a spyglass zooms; a compass and clock show their faces on screen when held.", ""},
    {0, "Redstone and rails", ""},
    {1, "Mine redstone ore (it glows when touched) and lay the dust on the ground to carry power from levers, buttons, pressure plates, tripwires and torches. Power fades one step per block of dust; a repeater sends it on at full strength. Redstone opens doors (iron ones need it), lights lamps, fires dispensers and droppers, and moves pistons \xE2\x80\x94 sticky ones pull blocks back, and slime or honey drags its neighbours along.", ""},
    {1, "Hoppers pass items between chests and furnaces. A comparator reads how full a container is. Observers pulse when the block they watch changes. Use a repeater to change its delay, a comparator to make it subtract, a daylight detector to invert it.", ""},
    {1, "Rails join up, climb slopes and turn corners as you lay them. Set a minecart on the track and right-click to ride; W gives a push, sneak to get out. Powered rails speed carts up (and stop them when unpowered), detector rails send a signal as a cart passes, activator rails tip riders out. Chest and hopper carts carry things; feed a furnace cart coal and it pushes itself.", ""},
    {0, "World settings and commands", ""},
    {1, "Each world has its own settings (World settings in the pause menu, or More options when you make one): its name, game mode, how long days and nights last, and game rules such as keep inventory, mob griefing and fire spread.", ""},
    {1, "Where commands are on (always in Creative), press T or / to type one: /time set day, /gamemode creative, /tp, /give, /fill and more. Builders get WorldEdit: //wand gives the wand (left click and right click mark two corners), then //set, //replace, //walls, //copy, //paste, //rotate, //stack, //sphere and //undo.", ""},
    {0, "Your worlds are safe", ""},
    {1, "The game saves every few seconds. Worlds keep working through updates, and you can make a backup from the pause menu at any time. Switch between Survival and Creative from the pause menu.", ""},
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
