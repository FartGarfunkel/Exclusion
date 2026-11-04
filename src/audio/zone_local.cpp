// audio/zone_local.cpp — find Matt's local STALKER recordings and say which ones miniaudio can play.
// The binaries stay on disk. This file only remembers paths.
#include "audio/zone_local.h"
#include "common/fs.h"
#include "gen/zone_plan.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <mutex>

namespace dq::snd {
namespace {

const char* kOutroDir = "12.-alexey-omelchuk-call-of-pripyat-ost-outro";
const char* kVoiceDir = "Classic Stalker Voice Taunts and Idles Lines";

// faction order is gen/zone_plan.h: drifter, bandit, army, bastion, opensky, merc, choir, institute
struct Pack { const char* dir; const char* idle; const char* taunt; };
const Pack kPacks[8] = {
  {"Loners_Lines", "idle", "attack"},
  {"Bandits_Lines", "idle", "attack"},
  {"Militaries_Lines", "VoiceMONOLITHidle", "VoiceMONOLITH"},
  {"Duties_Lines", "idle", "attack"},
  {"Freedomers_Lines", "idle", "VoiceMONOLITH"},
  {"Mercs_Lines", "idle", "attack"},
  {"Monoliths_Lines", "idle", "attack"},
  {"Ecologists_Lines", "idle", "VoiceMONOLITH"},
};
static_assert(zone::F_DRIFTER == 0 && zone::F_INSTITUTE == 7, "stalker voice folders follow the faction enum");

std::string lowerExt(const fs::path& p) {
  std::string e = p.extension().string();
  for (char& c : e) c = (char)std::tolower((unsigned char)c);
  return e;
}
bool knownExt(const std::string& e) { return e == ".wav" || e == ".mp3" || e == ".flac"; }

bool inFolder(const fs::path& file, const char* folder) {
  for (fs::path p = file.parent_path(); !p.empty() && p != p.root_path(); p = p.parent_path())
    if (p.filename() == folder) return true;
  return false;
}

void walkUp(const fs::path& start, const std::function<void(const fs::path&)>& fn) {
  std::error_code ec;
  fs::path p = fs::absolute(start, ec);
  if (ec) p = start;
  for (int i = 0; i < 8 && !p.empty(); i++) {
    fn(p);
    fs::path parent = p.parent_path();
    if (parent == p) break;
    p = parent;
  }
}

fs::path findNamed(const char* name) {
  fs::path found;
  auto look = [&](const fs::path& dir) {
    if (!found.empty()) return;
    std::error_code ec;
    fs::path c = dir / name;
    if (fs::is_directory(c, ec)) found = c;
  };
  look(userDir());
  look(userDir() / "sounds");
  look(userDir() / "music");
  walkUp(fs::current_path(), look);
  walkUp(installDir(), look);
  return found;
}

// "Outro" in the file name, flac ahead of wav ahead of mp3. Nested album folders are fine.
fs::path pickOutro(const fs::path& root) {
  if (root.empty()) return {};
  fs::path best;
  int bestRank = 99;
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return {};
  for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const std::string ext = lowerExt(it->path());
    if (!knownExt(ext)) continue;
    std::string stem = it->path().stem().string();
    for (char& c : stem) c = (char)std::tolower((unsigned char)c);
    if (stem.find("outro") == std::string::npos) continue;
    int rank = ext == ".flac" ? 0 : ext == ".wav" ? 1 : 2;
    if (rank < bestRank) { bestRank = rank; best = it->path(); }
  }
  return best;
}

void collect(const fs::path& factionDir, const char* folder, std::vector<fs::path>& out, bool oggOk) {
  std::error_code ec;
  if (!fs::is_directory(factionDir, ec)) return;
  for (auto it = fs::recursive_directory_iterator(factionDir, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    if (!inFolder(it->path(), folder)) continue;
    const std::string ext = lowerExt(it->path());
    if (knownExt(ext) || (oggOk && ext == ".ogg")) out.push_back(it->path());
  }
  std::sort(out.begin(), out.end());
}

struct Lib {
  bool ready = false;
  fs::path outro, voiceRoot;
  std::string outroDetail, voiceDetail;
  bool voices = false;
  bool oggOk = false;
  std::vector<fs::path> idle[8], taunt[8];
  std::once_flag once;
};

Lib& lib() { static Lib L; return L; }

void build() {
  Lib& L = lib();
  fs::path outroDir = findNamed(kOutroDir);
  fs::path musicDir = userDir() / "music";
  L.outro = pickOutro(musicDir);
  if (L.outro.empty()) L.outro = pickOutro(outroDir);
  if (!L.outro.empty() && !recordingOpens(L.outro.string())) {
    L.outroDetail = "found " + L.outro.string() + " but miniaudio could not open it";
    L.outro.clear();
  } else if (!L.outro.empty()) {
    L.outroDetail = L.outro.string();
  } else if (!outroDir.empty()) {
    L.outroDetail = "album folder " + outroDir.string() + " has no wav/mp3/flac outro miniaudio can play";
  } else {
    L.outroDetail = "no local outro (looked for " + std::string(kOutroDir) + " beside the checkout, and userDir()/music)";
  }

  L.voiceRoot = findNamed(kVoiceDir);
  if (L.voiceRoot.empty()) {
    L.voiceDetail = "no local voice folder (" + std::string(kVoiceDir) + ")";
    L.voices = false;
    return;
  }
  // one ogg decides the set: the lines on disk are ogg, and this build's miniaudio decodes wav, mp3 and flac
  fs::path sample;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(L.voiceRoot, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    if (lowerExt(it->path()) == ".ogg") { sample = it->path(); break; }
  }
  if (!sample.empty()) L.oggOk = recordingOpens(sample.string());
  int n = 0;
  for (int f = 0; f < 8; f++) {
    fs::path dir = L.voiceRoot / kPacks[f].dir;
    collect(dir, kPacks[f].idle, L.idle[f], L.oggOk);
    collect(dir, kPacks[f].taunt, L.taunt[f], L.oggOk);
    n += (int)L.idle[f].size() + (int)L.taunt[f].size();
  }
  L.voices = n > 0;
  if (L.voices) {
    L.voiceDetail = std::to_string(n) + " playable lines in " + L.voiceRoot.string();
  } else if (!sample.empty() && !L.oggOk) {
    L.voiceDetail = "lines in " + L.voiceRoot.string() + " are ogg (" + sample.filename().string()
      + "). This build's miniaudio decodes wav, mp3 and flac only, so the taunts stay silent and zone_murmur keeps the bed";
  } else {
    L.voiceDetail = "voice folder " + L.voiceRoot.string() + " has no line miniaudio can play";
  }
  DQ_LOG("audio: local outro: %s", L.outroDetail.c_str());
  DQ_LOG("audio: local voices: %s", L.voiceDetail.c_str());
}

} // namespace

void LocalStalkerAudio::prepare() {
  std::call_once(lib().once, build);
}
const fs::path& LocalStalkerAudio::outro() { prepare(); return lib().outro; }
const fs::path& LocalStalkerAudio::voiceRoot() { prepare(); return lib().voiceRoot; }
bool LocalStalkerAudio::voicesPlayable() { prepare(); return lib().voices; }
const std::string& LocalStalkerAudio::voiceDetail() { prepare(); return lib().voiceDetail; }
const std::string& LocalStalkerAudio::outroDetail() { prepare(); return lib().outroDetail; }

fs::path LocalStalkerAudio::line(int faction, Moment moment, std::uint32_t salt) {
  prepare();
  if (faction < 0 || faction > 7) return {};
  const bool taunt = moment == Contact || moment == Warn;
  const std::vector<fs::path>& v = taunt ? lib().taunt[faction] : lib().idle[faction];
  if (v.empty()) return {};
  return v[salt % v.size()];
}

} // namespace dq::snd
