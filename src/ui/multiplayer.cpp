// ui/multiplayer.cpp — net/netui.js: the Multiplayer screen (worlds announced on the home network + an address
// to type), the Joining… screen (the original's loading bar) and Open to LAN for the world being played.
// The network itself is client/netplay + net/.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "client/netplay.h"
#include "net/client.h"
#include "net/lan.h"
#include <imgui.h>
#include <atomic>
#include <mutex>
#include <thread>

namespace dq {
using namespace ui;

// the game talks over UDP (ENet): the game's ports 25580-25589 and the home-network announcements on 25581. (It used to say
// TCP here, which let nobody in: the firewall still dropped the game's UDP.)
static const char* FIREWALL_CMD = "sudo firewall-cmd --permanent --add-port=25580-25589/udp && sudo firewall-cmd --reload";

// ---- Joining… -------------------------------------------------------------------------------------------
class JoiningScreen : public Screen {
public:
  JoiningScreen(std::string addr, std::string label, std::string pw) : address(std::move(addr)), label(std::move(label)), password(std::move(pw)) {}
  std::string address, label, password, error;
  std::shared_ptr<net::NetClient> nc;
  const char* id() const override { return "loading"; }
  void onOpen(App& a) override { nc = netJoinBegin(a, address, password); }
  bool onBack(App& a) override { nc.reset(); return false; }   // B / Esc gives up
  void update(App& a, double) override {
    if (!nc) return;
    auto st = nc->poll();
    if (st == net::NetClient::State::Ready) {
      auto c = nc; std::string addr = address;
      nc.reset();
      uiDefer([c, addr](App& app) { app.leaveWorld(false); if (!netJoinFinish(app, c, addr)) app.setScreen(makeTitleScreen()); });
    } else if (st == net::NetClient::State::Failed || st == net::NetClient::State::Closed) {
      std::string e = nc->error().empty() ? "Could not join" : nc->error();
      nc.reset();
      std::string lower = toLower(e);
      if (lower.find("connect") != std::string::npos || lower.find("answer") != std::string::npos)
        e += " \xE2\x80\x94 check the address, that you're on the same network, and the host's firewall (see below).";
      a.toast(e, "warn");
      a.popScreen();
    }
  }
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    // the loading screen of the Zone: dark, dust drifting, the words in spaced capitals over a segmented amber bar
    float z = Z(), W = (float)a.width, H = (float)a.height;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(8, 9, 6, 235));
    trefoil(dl, ImVec2(W * 0.5f, H * 0.42f), std::min(W, H) * 0.22f, IM_COL32(226, 184, 76, 10), (float)a.time * 0.05f);
    zoneBackdrop(dl, ImVec2(0, 0), ImVec2(W, H), (float)a.time);
    float inset = 18 * z;
    cornerBrackets(dl, ImVec2(inset, inset), ImVec2(W - inset, H - inset), 26 * z, withAlpha(col::amber, 0.45f), 2 * z);
    bool joining = nc && nc->state() == net::NetClient::State::Joining;
    std::string t = upper(joining ? "Joining " + (label.empty() ? address : label) : "Connecting to " + (label.empty() ? address : label));
    float bw = std::min(520 * z, W * 0.86f);
    ImVec2 ts = textSpacedSize(F_H3, t.c_str(), 2.5f * z);
    float k = ts.x > bw ? std::max(0.6f, bw / ts.x) : 1.f; ts = textSpacedSize(F_H3, t.c_str(), 2.5f * z, k);
    float y = H * 0.5f - (ts.y + 14 * z + 16 * z) * 0.5f;
    textSpaced(dl, F_H3, ImVec2(W * 0.5f - ts.x * 0.5f, y), col::zText, t.c_str(), 2.5f * z, k);
    y += ts.y + 14 * z;
    ImVec2 b0(W * 0.5f - bw * 0.5f, y), b1 = b0 + ImVec2(bw, 14 * z);
    dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 160));
    float f = nc ? clampv(nc->progress(), 0.f, 1.f) : 1.f;
    const int N = 30; float cw = (bw - 4 * z) / N;
    for (int i = 0; i < N; i++) {
      float on = clampv(f * N - i, 0.f, 1.f);
      ImU32 c = on > 0 ? withAlpha(col::amber, 0.35f + 0.65f * on) : IM_COL32(255, 255, 255, 14);
      dl->AddRectFilled(ImVec2(b0.x + 2 * z + i * cw + 1 * z, b0.y + 3 * z), ImVec2(b0.x + 2 * z + (i + 1) * cw - 1 * z, b1.y - 3 * z), c);
    }
    dl->AddRect(b0, b1, col::zEdge, 0, 0, 1 * z);
    const char* sub = joining ? "RECEIVING THE WORLD" : "WAITING FOR THE HOST";
    float dots = std::fmod((float)a.time, 1.2f) / 0.4f;
    std::string s2 = std::string(sub) + (dots < 1 ? "." : dots < 2 ? ".." : "...");
    textSpaced(dl, F_SMALL, ImVec2(b0.x, b1.y + 8 * z), col::zDim, s2.c_str(), 2 * z);
    padHints(dl, ImVec2(W - inset - 14 * z, H - inset - 12 * z - 20 * z), {{1, "Esc", "Cancel"}}, true);
  }
};
std::unique_ptr<Screen> makeJoiningScreen(const std::string& address, const std::string& label, const std::string& password) {
  return std::make_unique<JoiningScreen>(address, label, password);
}

// ---- Multiplayer ------------------------------------------------------------------------------------------
class MultiplayerScreen : public Screen {
public:
  const char* id() const override { return "lanjoin"; }
  std::string addr, password, msg;
  bool onBack(App&) override { return backConsumedByText(); }
  void onClose(App&) override { lanScanStop(); }
  void join(App& a, const std::string& address, const std::string& label) {
    if (address.empty()) { msg = "Type the address the host's screen shows, like 192.168.1.23:25580"; return; }
    a.pushScreen(makeJoiningScreen(address, label, password));
  }
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    float z = Z();
    Panel pn;
    pn.begin("##lanjoin", "Multiplayer", 860, "Back");
    pn.beginBody();
    float w = pn.width - 36 * z - ImGui::GetStyle().ScrollbarSize;
    ImGui::SetCursorPos(ImVec2(18 * z, 14 * z));
    ImGui::BeginGroup();
    sectionLabel("Worlds on your network", w);
    auto found = lanScan();
    if (found.empty()) {
      // a sweeping scan line while it listens
      ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 p0 = ImGui::GetCursorScreenPos(); float h = 40 * z;
      dl->AddRectFilled(p0, p0 + ImVec2(w, h), IM_COL32(0, 0, 0, 70)); dl->AddRect(p0, p0 + ImVec2(w, h), col::zEdge, 0, 0, 1 * z);
      float sx = p0.x + std::fmod((float)a.time * 0.35f, 1.f) * w;
      dl->AddRectFilledMultiColor(ImVec2(std::max(p0.x, sx - 60 * z), p0.y + 1 * z), ImVec2(sx, p0.y + h - 1 * z), withAlpha(col::amber, 0), withAlpha(col::amber, 0.16f), withAlpha(col::amber, 0.16f), withAlpha(col::amber, 0));
      ImGui::SetCursorScreenPos(p0 + ImVec2(12 * z, (h - textSize(F_BODY, "A").y) * 0.5f));
      label(F_BODY, col::zDim, "Searching the network\xE2\x80\xA6 (on the host: pause \xE2\x86\x92 Open to LAN)", w - 24 * z);
      ImGui::SetCursorScreenPos(p0 + ImVec2(0, h + 6 * z));
    }
    for (auto& g : found) {
      ImGui::PushID((g.address + ":" + std::to_string(g.port)).c_str());
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 p0 = ImGui::GetCursorScreenPos(); float h = 54 * z;
      dl->AddRectFilledMultiColor(p0, p0 + ImVec2(w, h), col::rgb(0x191a13), col::rgb(0x15160f), col::rgb(0x111209), col::rgb(0x121309));
      dl->AddRect(p0, p0 + ImVec2(w, h), col::zEdge, 0, 0, 1 * z);
      dl->AddRectFilled(p0 + ImVec2(0, 6 * z), ImVec2(p0.x + 3 * z, p0.y + h - 6 * z), col::amber);
      text(dl, F_BODY, p0 + ImVec2(12, 8) * z, col::zText, g.world.c_str());
      std::string sub = "hosted by " + g.host + " \xC2\xB7 " + std::to_string(g.players) + " playing \xC2\xB7 " + g.address + (g.self ? " (this computer)" : "") + (g.password ? " \xC2\xB7 password" : "");
      text(dl, F_SMALL, p0 + ImVec2(12, 30) * z, col::zDim, sub.c_str(), 0, 12 / 12.5f);
      float bw = textSize(F_BODY, "Join").x + 40 * z;
      ImGui::SetCursorScreenPos(ImVec2(p0.x + w - bw - 8 * z, p0.y + (h - 44 * z) * 0.5f));
      if (button("Join", BTN_PRIMARY, false, bw)) join(a, g.address + ":" + std::to_string(g.port), g.world);
      ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + 6 * z));
      ImGui::Dummy(ImVec2(w, 0));
      ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(1, 8 * z));
    sectionLabel("Join by address", w);
    label(F_MID, col::muted, "Type the address the host's screen shows");
    float jw = textSize(F_BODY, "Join").x + 40 * z;
    bool enter = false;
    { inputText("##addr", &addr, "192.168.1.23:25580", 80, w - jw - 8 * z, ImGuiInputTextFlags_EnterReturnsTrue); enter = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter); }
    ImGui::SameLine(0, 8 * z);
    if (button("Join##addr", BTN_PRIMARY, false, jw) || enter) join(a, addr, "");
    label(F_MID, col::muted, "Password (only if the host set one)");
    inputText("##pw", &password, nullptr, 64, std::min(w, 260 * z), ImGuiInputTextFlags_Password);
    if (!msg.empty()) label(F_BODY, col::text, msg.c_str(), w);
    ImGui::Dummy(ImVec2(1, 4 * z));
    label(F_MID, col::muted, "Worlds opened to LAN on your home network show up here by themselves. If the list stays empty, type the address the host's screen shows \xE2\x80\x94 the firewall may be hiding the list (sudo firewall-cmd --add-port=25581/udp lets it through).", w);
    label(F_MID, col::muted, "Split screen still works too: plug in a controller and press Start.", w);
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, 16 * z));
    pn.endBody();
    pn.end();
    if (pn.closed) a.popScreen();
  }
};
std::unique_ptr<Screen> makeMultiplayerScreen() { return std::make_unique<MultiplayerScreen>(); }

// ---- Open to LAN ------------------------------------------------------------------------------------------
class LanHostScreen : public Screen {
public:
  const char* id() const override { return "lanhost"; }
  std::string msg; bool fwOpen = false, netOpen = false;
  // this computer's address on the internet, looked up on a worker when asked (net::internetAddress)
  struct Lookup { std::mutex mx; std::string addr; bool busy = false, done = false; };
  std::shared_ptr<Lookup> look = std::make_shared<Lookup>();
  void draw(App& a) override {
    if (a.topScreen() != this || !a.session) return;
    float z = Z();
    Panel pn;
    pn.begin("##lanhost", "Open to LAN", 860, "Back");
    pn.beginBody();
    float w = pn.width - 36 * z - ImGui::GetStyle().ScrollbarSize;
    ImGui::SetCursorPos(ImVec2(18 * z, 14 * z));
    ImGui::BeginGroup();
    const std::string& world = a.session->level->info.name;
    LanStatus st = lanStatus(a);
    if (!st.open) {
      label(F_BODY, col::text, ("Let others on your home network play in " + world + " with you. Your computer runs the world: creatures, time and saving. Everyone keeps their own things, and your world remembers them for next time.").c_str(), w);
      label(F_BODY, col::text, "They can join from Exclusion on another computer (Multiplayer on the title screen).", w);
      if (button("Open to LAN##start", BTN_PRIMARY)) { std::string e; msg = openToLan(a, LanOpenOptions(), &e) ? "" : e; }
      if (!msg.empty()) label(F_MID, col::muted, msg.c_str(), w);
    } else {
      label(F_BODY, col::text, (world + " is open to your home network.").c_str(), w);
      label(F_BODY, col::text, "On another computer with Exclusion: Multiplayer on the title screen \xE2\x80\x94 your world shows up there. Or type this address:", w);
      label(F_BIG, col::amber, ((st.address.empty() ? std::string("this computer") : st.address) + ":" + std::to_string(st.port)).c_str());
      ImGui::Dummy(ImVec2(1, 4 * z));
      sectionLabel("Playing with you", w);
      if (st.players.empty()) label(F_MID, col::rgb(0xd8d8d8), "Nobody has joined yet.");
      for (auto& n : st.players) {
        ImGui::PushID(n.c_str());
        ImGui::AlignTextToFramePadding(); label(F_BODY, col::text, n.c_str());
        ImGui::SameLine(w - 90 * z + 18 * z);
        if (button("Remove", BTN_NORMAL, true)) lanKick(a, n);
        ImGui::PopID();
      }
      ImGui::Dummy(ImVec2(1, 6 * z));
      ImGui::PushStyleColor(ImGuiCol_Text, col::gold); ImGui::PushStyleColor(ImGuiCol_Header, 0);
      ImGui::SetNextItemOpen(fwOpen); fwOpen = ImGui::CollapsingHeader("They can't connect?");
      ImGui::PopStyleColor(2);
      if (fwOpen) {
        label(F_MID, col::text, "openSUSE's firewall blocks people joining until you allow it once (on the internet as well as at home). Open a terminal and run:", w);
        label(F_MONO, col::text, FIREWALL_CMD, w);
        if (button("Copy", BTN_NORMAL, true)) { SDL_SetClipboardText(FIREWALL_CMD); msg = "Copied"; }
        label(F_MID, col::muted, "It asks for your password. Then try joining again. Other Linux systems, and Windows: allow UDP ports 25580\xE2\x80\x93" "25589. (UDP, not TCP: the game doesn't use TCP at all.)", w);
      }
      ImGui::PushStyleColor(ImGuiCol_Text, col::gold); ImGui::PushStyleColor(ImGuiCol_Header, 0);
      ImGui::SetNextItemOpen(netOpen); netOpen = ImGui::CollapsingHeader("Playing over the internet");
      ImGui::PopStyleColor(2);
      if (netOpen) {
        const std::string port = std::to_string(st.port);
        label(F_MID, col::text, ("1. On your router, forward UDP port " + port + " (UDP, not TCP) to this computer" + (st.address.empty() ? std::string() : " (" + st.address + ")") + ".").c_str(), w);
        label(F_MID, col::text, "2. Let it through this computer's firewall too (above): a port open on the router isn't enough.", w);
        label(F_MID, col::text, "3. They type your internet address, not the home one above:", w);
        std::string addr; bool busy, done;
        { std::lock_guard<std::mutex> lk(look->mx); addr = look->addr; busy = look->busy; done = look->done; }
        if (!addr.empty()) {
          const std::string full = addr + ":" + port;
          label(F_BIG, col::ember, full.c_str());
          if (button("Copy##net", BTN_NORMAL, true)) { SDL_SetClipboardText(full.c_str()); msg = "Copied"; }
        } else if (busy) label(F_MID, col::muted, "Looking it up\xE2\x80\xA6", w);
        else {
          if (button("Find my internet address", BTN_NORMAL, true)) {
            { std::lock_guard<std::mutex> lk(look->mx); look->busy = true; }
            std::thread([l = look] { std::string a = net::internetAddress(); std::lock_guard<std::mutex> lk(l->mx); l->addr = a; l->busy = false; l->done = true; }).detach();
          }
          if (done) label(F_MID, col::muted, "Couldn't find it out (no curl, or offline). Search \"what is my IP\" in a browser instead.", w);
        }
        label(F_MID, col::muted, "Still nothing? If your router's own internet (WAN) address isn't the one above, your provider shares one address among many homes (CGNAT) and nobody can reach you from outside: ask them for a public IPv4 address. And you both need the same DokuQuest version.", w);
      }
      ImGui::Dummy(ImVec2(1, 6 * z));
      if (button("Stop sharing this world")) { closeLan(a); a.toast("Your world is just yours again", ""); }
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, 16 * z));
    pn.endBody();
    pn.end();
    if (pn.closed) a.popScreen();
  }
};
std::unique_ptr<Screen> makeLanHostScreen() { return std::make_unique<LanHostScreen>(); }

} // namespace dq
