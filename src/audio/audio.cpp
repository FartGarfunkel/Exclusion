// audio/audio.cpp — miniaudio output, the mixer and the sfx API (game/audio.js, sfxlib.js's sample(),
// sound3.js's placeNode). See engine.h for who runs where.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>
#include "audio/engine.h"
#include <deque>
#include "audio/music.h"
#include "audio/ambience.h"
#include "common/fs.h"
#include "audio/zone_local.h"
#include "world/world.h"
#include "core/registry.h"
#include <chrono>
#include <unordered_set>

namespace dq {

void mixLocalScore(float* out, int n, float volume);   // a local recording on the music bus (defined below)

using namespace snd;
static double steadyNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

AudioEngine::AudioEngine() : impl(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { shutdown(); }

// ---- the mixer (audio thread) ---------------------------------------------------------------------------------
void AudioEngine::Impl::startVoice(const AudioCmd& c) {
  if (!c.buf || c.buf->d.size() < 2) return;
  int slot = -1; float quiet = 1e9f;
  for (int i = 0; i < MAXV; i++) if (!voices[i].on) { slot = i; break; }
  if (slot < 0) {   // all busy: take over the quietest, least-remaining one
    for (int i = 0; i < MAXV; i++) {
      const Voice& v = voices[i];
      float rem = 1.f - (float)(v.pos / v.b->d.size()), score = (v.gL + v.gR) * v.fade * rem * (v.cat == CAT_AMB ? 0.5f : 1.f);
      if (score < quiet) { quiet = score; slot = i; }
    }
  }
  Voice& v = voices[slot];
  v = Voice();
  v.on = true; v.handle = c.handle; v.b = c.buf; v.pos = 0; v.rate = std::clamp(c.rate, 0.25f, 4.f);
  v.gL = c.gL; v.gR = c.gR; v.cat = c.cat; v.wait = (int)(c.delay * SR);
  if (c.lp > 0 && c.lp < 16000) { v.lpOn = true; v.lpL.setWA("lowpass", c.lp, 0.5); }
}

void AudioEngine::Impl::mix(float* out, int n) {
  AudioCmd c;
  while (q.pop(c)) {
    switch (c.t) {
      case AudioCmd::Play: startVoice(c); break;
      case AudioCmd::Stop: for (auto& v : voices) if (v.on && v.handle == c.handle) v.fadeStep = 1.f / (0.01f * SR); break;
      case AudioCmd::Wind: wind.target = c.a; wind.ft = c.b; break;
      case AudioCmd::Rain: rain.target = c.a; break;
      case AudioCmd::Drone: drone.target = c.a; break;
      case AudioCmd::Verb: roomT = c.a; under = c.b < 0; caveT = std::max(0.f, c.b); break;   // b < 0: under water
      default: if (music) music->command(c); break;
    }
  }
  // ambience's level is used the original's way (how often things sound, how loud the wind), not again here
  float master = vMaster.load(std::memory_order_relaxed), catG[3] = {master * vSfx.load(std::memory_order_relaxed), master * vMobs.load(std::memory_order_relaxed), master};
  static thread_local std::vector<float> dry, bus; dry.assign((size_t)n * 2, 0.f); bus.assign((size_t)n * 2, 0.f);
  // buffers
  for (auto& v : voices) {
    if (!v.on) continue;
    const float* d = v.b->d.data(); const size_t L = v.b->d.size();
    float gl = v.gL * catG[v.cat], gr = v.gR * catG[v.cat];
    for (int i = 0; i < n; i++) {
      if (v.wait > 0) { v.wait--; continue; }
      size_t k = (size_t)v.pos;
      if (k + 1 >= L) { v.on = false; break; }
      float fr = (float)(v.pos - k), s = d[k] + (d[k + 1] - d[k]) * fr;
      if (v.lpOn) s = v.lpL.run(s);
      if (v.fadeStep > 0) { v.fade -= v.fadeStep; if (v.fade <= 0) { v.on = false; break; } }
      s *= v.fade;
      bus[2 * i] += s * gl; bus[2 * i + 1] += s * gr;
      v.pos += v.rate;
    }
  }
  // the wind: a looping band of noise (ambience.js)
  if (wind.g > 1e-5f || wind.target > 1e-5f) {
    for (int i = 0; i < n; i++) {
      if ((i & 63) == 0) { wind.f += (wind.ft - wind.f) * 0.02f; wind.bp.setWA("bandpass", wind.f, 0.6); }
      wind.g += (wind.target - wind.g) * 0.002f;
      float s = wind.bp.run(wind.r.pm()) * wind.g * master;
      bus[2 * i] += s; bus[2 * i + 1] += s;
    }
  }
  // the rain: low-passed noise with a little high patter on top (weather.js rain loop; louder in the open)
  if (rain.g > 1e-5f || rain.target > 1e-5f) {
    if (!rain.init) { rain.lp.setWA("lowpass", 1400, 0.5); rain.hp.setWA("highpass", 5200, 0.6); rain.init = true; }
    for (int i = 0; i < n; i++) {
      rain.g += (rain.target - rain.g) * 0.0004f;
      float nz = rain.r.pm(), s = (rain.lp.run(nz) * 0.9f + rain.hp.run(rain.r.pm()) * 0.25f) * rain.g * master;
      bus[2 * i] += s; bus[2 * i + 1] += s * 0.97f;
    }
  }
  // the End: two low tones a breath apart (they beat slowly), a fifth and an octave faintly over them, swelling and
  // fading over nine seconds, and a hollow wind breathing through a narrow band
  if (drone.g > 1e-5f || drone.target > 1e-5f) {
    if (!drone.init) { drone.bp.setWA("bandpass", 190, 3.0); drone.init = true; }
    const double TAU2 = 6.283185307179586;
    for (int i = 0; i < n; i++) {
      drone.g += (drone.target - drone.g) * 0.00015f;
      const double t = drone.t; drone.t += 1.0 / SR;
      const float sw = 0.6f + 0.4f * (float)std::sin(TAU2 * t / 9.3), sw2 = 0.5f + 0.5f * (float)std::sin(TAU2 * t / 13.7 + 1);
      const float a = (float)(std::sin(TAU2 * 55 * t) + 0.8 * std::sin(TAU2 * 55.45 * t) + 0.35 * std::sin(TAU2 * 82.5 * t + 0.4) + 0.18 * std::sin(TAU2 * 110.3 * t));
      const float h = drone.bp.run(drone.r.pm()) * 1.6f * sw2;
      const float s = (a * 0.11f * sw + h * 0.5f) * drone.g * master;
      bus[2 * i] += s; bus[2 * i + 1] += s * 0.95f + a * 0.004f * drone.g * master;
    }
    if (drone.t > 1e6) drone.t -= 1e6;
  }
  // sound3.js: everything through one bus, with a small room and a cave on sends
  float rs = 1.f / (0.6f * SR);
  bool verb = roomG > 1e-4f || caveG > 1e-4f || roomT > 1e-4f || caveT > 1e-4f;
  const float uwK = 0.085f;   // a one-pole low-pass near 700 Hz: the world heard through water
  for (int i = 0; i < n; i++) {
    float L = bus[2 * i], R = bus[2 * i + 1];
    if (verb) {
      roomG += (roomT - roomG) * rs; caveG += (caveT - caveG) * rs;
      float a, b, x, y;
      room.process(L * roomG, R * roomG, a, b); cave.process(L * caveG, R * caveG, x, y);
      L += a + x; R += b + y;
    }
    uwG += ((under ? 1.f : 0.f) - uwG) * rs * 2;
    if (uwG > 1e-3f) { uwL += uwK * (L - uwL); uwR += uwK * (R - uwR); L += (uwL * 1.15f - L) * uwG; R += (uwR * 1.15f - R) * uwG; }
    dry[2 * i] = L; dry[2 * i + 1] = R;
  }
  // the music bus (its own volume, straight out like music.js)
  float musicVol = vMusic.load(std::memory_order_relaxed);
  if (music) music->render(dry.data(), n, frames.load(std::memory_order_relaxed) / (double)SR, musicVol);
  mixLocalScore(dry.data(), n, musicVol);
  for (int i = 0; i < 2 * n; i++) {   // a soft knee above 0.8 instead of hard clipping
    float x = dry[i], a = std::abs(x);
    out[i] = a < 0.8f ? x : std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f), x);
  }
  frames.fetch_add((u64)n, std::memory_order_relaxed);
}

static void dataCb(ma_device* dev, void* out, const void*, ma_uint32 frames) {
  static_cast<AudioEngine::Impl*>(dev->pUserData)->mix((float*)out, (int)frames);
}

// A local recording on the music bus (menu and ending). The decoder stays in this file, next to the
// miniaudio implementation, so the struct matches. One score at a time; the game thread only swaps the path.
struct LocalScore {
  std::mutex mx;
  std::string want;
  int gen = 0, seen = -1;
  ma_decoder dec{};
  bool open = false, stop = false;
  float gain = 0;
};
LocalScore& localScore() { static LocalScore s; return s; }

void closeLocalScore() {
  LocalScore& s = localScore();
  if (s.open) { ma_decoder_uninit(&s.dec); s.open = false; }
  s.stop = false; s.gain = 0;
}

void mixLocalScore(float* out, int n, float volume) {
  LocalScore& s = localScore();
  std::string want; int gen;
  { std::lock_guard<std::mutex> lk(s.mx); want = s.want; gen = s.gen; }
  if (gen != s.seen) {
    s.seen = gen;
    if (want.empty()) s.stop = true;
    else {
      if (s.open) { ma_decoder_uninit(&s.dec); s.open = false; }
      ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, SR);
      if (ma_decoder_init_file(want.c_str(), &cfg, &s.dec) == MA_SUCCESS) {
        s.open = true; s.stop = false; s.gain = 0;
        DQ_LOG("audio: local score started (%s)", want.c_str());
      }
      else { s.open = false; s.stop = false; DQ_WARN("audio: miniaudio could not open the local score (%s)", want.c_str()); }
    }
  }
  if (!s.open) return;
  const float target = s.stop ? 0.f : std::clamp(volume, 0.f, 1.f);
  const float step = 1.f / (0.35f * SR);
  std::vector<float> tmp((size_t)n * 2, 0.f);
  ma_uint64 got = 0;
  ma_decoder_read_pcm_frames(&s.dec, tmp.data(), (ma_uint64)n, &got);
  if (!s.stop && got < (ma_uint64)n) {
    ma_decoder_seek_to_pcm_frame(&s.dec, 0);
    ma_uint64 more = 0;
    ma_decoder_read_pcm_frames(&s.dec, tmp.data() + (size_t)got * 2, (ma_uint64)n - got, &more);
    got += more;
  }
  bool silent = true;
  for (int i = 0; i < n; i++) {
    if (s.gain < target) s.gain = std::min(target, s.gain + step);
    else if (s.gain > target) s.gain = std::max(target, s.gain - step);
    if (s.gain > 0.0008f) silent = false;
    if ((ma_uint64)i >= got) continue;
    out[2 * i] += tmp[(size_t)i * 2] * s.gain;
    out[2 * i + 1] += tmp[(size_t)i * 2 + 1] * s.gain;
  }
  if (s.stop && silent) closeLocalScore();
}

void AudioEngine::Impl::setLocalScore(const std::string& path) {
  LocalScore& s = localScore();
  std::lock_guard<std::mutex> lk(s.mx);
  if (s.want == path) return;
  s.want = path;
  s.gen++;
}

namespace snd {
bool recordingOpens(const std::string& path) {
  if (path.empty()) return false;
  ma_decoder dec;
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, 48000);
  if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) return false;
  ma_decoder_uninit(&dec);
  return true;
}
} // namespace snd

// ---- worker ------------------------------------------------------------------------------------------------------
void AudioEngine::Impl::post(std::function<void()> f) {
  { std::lock_guard<std::mutex> l(jmx); jobs.push_back(std::move(f)); }
  jcv.notify_one();
}
std::shared_ptr<AudioBuffer> AudioEngine::Impl::cached(const std::string& key) {
  std::lock_guard<std::mutex> l(cmx);
  auto it = cache.find(key);
  return it == cache.end() ? nullptr : it->second;
}
void AudioEngine::Impl::requestRender(const std::string& key, const SoundRef& r, int take, u32 seed) {
  {
    std::lock_guard<std::mutex> l(cmx);
    if (cache.count(key) || rendering.count(key)) return;
    rendering[key] = true;
  }
  post([this, key, r, take, seed] {
    auto b = std::make_shared<AudioBuffer>();
    float p = 1, v = 1;
    b->d = lib.render(r, take, seed, &p, &v);
    if (b->d.empty()) b->d.assign(2, 0.f);
    nRenders++;
    std::lock_guard<std::mutex> l(cmx);
    cacheBytes += b->d.size() * 4;
    cache[key] = b; rendering.erase(key);
  });
}

static std::string cacheKey(const SoundRef& r, int take) { return std::to_string((int)r.kind) + ":" + r.key + "#" + std::to_string(take); }

bool AudioEngine::init(const Settings& s) {
  auto* dev = new ma_device();
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32; cfg.playback.channels = 2; cfg.sampleRate = SR;
  cfg.periodSizeInFrames = 512;
  cfg.dataCallback = dataCb; cfg.pUserData = impl.get();
  if (ma_device_init(nullptr, &cfg, dev) != MA_SUCCESS) { delete dev; DQ_WARN("audio: no output device; continuing without sound"); return false; }
  impl->device = dev;
  if (!initOffline(s)) return false;
  impl->offline = false;
  if (ma_device_start(dev) != MA_SUCCESS) { DQ_WARN("audio: the output device would not start; continuing without sound"); shutdown(); return false; }
  return true;
}

bool AudioEngine::initOffline(const Settings& s) {
  Impl& m = *impl;
  m.offline = true; m.ok = true; m.now = 0;
  m.room.init(0.7f, 0.5f); m.cave.init(2.5f, 0.28f);
  m.music = std::make_unique<MusicSynth>();
  m.director = std::make_unique<MusicDirector>(m);
  m.ambience = std::make_unique<Ambience>(m);
  setVolumes(s);
  m.worker = std::thread([&m] {
    m.libReady = m.lib.load(dataDir());
    // the footsteps and the clicks are ready before anyone needs them (sfxlib.js renders everything once sound is on)
    if (m.libReady) for (const char* mat : {"stone", "grass", "dirt", "wood", "sand", "gravel", "log", "leaves"}) {
      SoundRef r = m.lib.material(mat, "step", false);
      for (int t = 0; t < r.takes; t++) m.requestRender(cacheKey(r, t), r, t, (u32)t);
    }
    if (m.libReady) { SoundRef r = m.lib.resolve("click"); for (int t = 0; t < r.takes; t++) m.requestRender(cacheKey(r, t), r, t, (u32)t); }
    for (;;) {
      std::function<void()> job;
      { std::unique_lock<std::mutex> l(m.jmx); m.jcv.wait(l, [&] { return m.quit || !m.jobs.empty(); }); if (m.quit) return; job = std::move(m.jobs.front()); m.jobs.pop_front(); }
      try { job(); } catch (const std::exception& e) { DQ_WARN("audio: %s", e.what()); }
    }
  });
  return true;
}

void AudioEngine::shutdown() {
  if (!impl) return;
  Impl& m = *impl;
  closeLocalScore();
  if (m.device) { ma_device_uninit((ma_device*)m.device); delete (ma_device*)m.device; m.device = nullptr; }
  if (m.worker.joinable()) { { std::lock_guard<std::mutex> l(m.jmx); m.quit = true; } m.jcv.notify_all(); m.worker.join(); }
  if (m.ok && !m.offline) DQ_LOG("audio: %d sounds played, %d takes rendered (%.1f MB cached)", m.nPlays, m.nRenders.load(), m.cacheBytes / 1048576.0);
  m.ok = false;
}

void AudioEngine::setListener(vec3 pos, vec3 forward, vec3) { impl->lpos = pos; impl->lfwd = forward; }
void AudioEngine::setVolumes(const Settings& s) {
  impl->vMaster = s.volume; impl->vMobs = s.mobVol; impl->vMusic = s.music; impl->vAmb = s.ambience; impl->musicFreq = s.musicFreq;
}
void AudioEngine::setCategoryVolume(const std::string& cat, float v) {
  if (cat == "master") impl->vMaster = v; else if (cat == "sfx") impl->vSfx = v; else if (cat == "mobs") impl->vMobs = v;
  else if (cat == "music") impl->vMusic = v; else if (cat == "ambience") impl->vAmb = v;
}
bool AudioEngine::ok() const { return impl && impl->ok; }
void AudioEngine::setScene(const AudioScene& s) { impl->scene = s; }

// ---- playing ---------------------------------------------------------------------------------------------------------
bool AudioEngine::Impl::gapOk(const std::string& key, float gap) {
  auto it = last.find(key);
  if (it != last.end() && now - it->second < gap) return false;
  last[key] = now; return true;
}

int AudioEngine::Impl::start(const SoundRef& r, AudioCmd c, float pitchMul) {
  if (!r || !ok || !libReady) return 0;
  if (vMaster.load() <= 0) return 0;
  int take = r.takes > 1 ? (int)(rnd() * r.takes) % r.takes : 0;
  // Sfx.sample: a take, a touch higher or lower each time, a touch louder or softer
  float pv = r.pv;
  c.t = AudioCmd::Play;
  c.handle = c.handle ? c.handle : nextHandle++;
  c.rate = pitchMul * (1 + (rnd() * 2 - 1) * pv);
  float g = r.gain * (r.jitter ? 0.9f + rnd() * 0.15f : 1.f);
  if (r.kind == SoundRef::Pack) {   // Minecraft's own per-entry pitch and volume
    if (auto v = lib.packs().event(r.key)) { auto& var = (*v)[take % v->size()]; c.rate *= var.pitch; g *= var.vol; }
  }
  c.gL *= g; c.gR *= g;
  c.cat = r.mob ? CAT_MOBS : c.cat;
  std::string key = cacheKey(r, take);
  nPlays++;
  if (auto b = cached(key)) { b->lastStart = now; c.buf = b.get(); send(c); return c.handle; }
  requestRender(key, r, take, (u32)take);
  pending.push_back({key, r, take, c, now});
  return c.handle;
}

// sound3.js placeNode: how far it travels, what's in the way, where it comes from
bool AudioEngine::Impl::place(vec3 p, float range, float vol, AudioCmd& c) {
  vec3 L = lpos;
  // a sound made in a solid block (the one being mined, the one just placed) comes from its face toward you,
  // not from inside the stone (else the block itself counted as a wall: mining was a faint muffled thud)
  if (scene.world) {
    BlockId id = scene.world->getBlock((int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z));
    vec3 dv = L - p; float n = glm::length(dv);
    if (id && reg().tSolid[id] && n > 1.f) p += dv / n * 0.9f;
  }
  float best = glm::distance(L, p);
  if (best > range + 2) return false;
  float d = best, wall = 0;
  if (ambience && scene.inWorld && scene.world) ambience->reach(L, p, d, wall);
  if (d > range) return false;
  float gain = std::clamp(1 - (d - 3) / (range - 3), 0.f, 1.f) * vol;
  if (wall > 0) gain *= std::pow(0.42f, std::min(wall, 6.f));
  if (gain < 0.015f) return false;
  c.lp = wall > 0 ? std::max(220.f, 1400.f / wall) : 0;
  // direction: right/left from the listener's facing; behind sounds a little duller (the HRTF panner's cue)
  float gl = 1, gr = 1;
  if (best > 0.6f) {
    vec3 f = lfwd; f.y = 0; if (glm::length(f) < 1e-4f) f = vec3(0, 0, -1); f = glm::normalize(f);
    vec3 right(-f.z, 0, f.x);
    // height is in the pan at full strength, so a source overhead centres instead of sitting on the horizon.
    // In front is a little louder; behind is duller and about 6 dB down. Under a couple of metres the
    // behind cue stays off — your own gun is not "behind you".
    vec3 dv = p - L; float px = glm::dot(dv, right), pf = glm::dot(dv, f);
    float horiz = std::sqrt(px * px + pf * pf);
    float n = std::sqrt(px * px + dv.y * dv.y + pf * pf);
    if (n < 1e-4f) n = 1;
    float pan = std::clamp(px / n, -1.f, 1.f) * 0.8f, th = (pan + 1) * 0.25f * PI;   // the far ear still hears some (as through the HRTF)
    gl = std::min(1.f, 1.41421f * std::cos(th)); gr = std::min(1.f, 1.41421f * std::sin(th));
    float elev = dv.y / n;
    if (best > 2.f && horiz > 0.4f) {
      float face = std::clamp(pf / horiz, -1.f, 1.f);
      float ahead = face > 0 ? face : 0.f, behind = face < 0 ? -face : 0.f;
      gain *= (1.f + 0.18f * ahead) * (1.f - 0.5f * behind);
      if (behind > 0.05f) { float cut = 1400.f + (1.f - behind) * 7000.f; c.lp = c.lp > 0 ? std::min(c.lp, cut) : cut; }
    }
    if (elev < -0.35f) { gain *= 0.82f; c.lp = c.lp > 0 ? std::min(c.lp, 1800.f) : 1800.f; }
    else if (elev > 0.45f) gain *= 1.08f;
  }
  c.gL = gain * gl; c.gR = gain * gr;
  return true;
}

int AudioEngine::play(const std::string& name, float volume, float pitch) {
  Impl& m = *impl;
  if (!m.ok || !m.libReady) return 0;
  SoundRef r = m.lib.resolve(name);
  if (!r) return 0;
  if (r.mob && m.vMobs.load() <= 0) return 0;
  if (!m.gapOk(name, r.gap)) return 0;
  AudioCmd c; c.gL = c.gR = volume;
  return m.start(r, c, pitch);
}

int AudioEngine::playFileAt(const std::string& path, vec3 pos, float range, float volume, float pitch, float* seconds) {
  if (seconds) *seconds = 0;
  if (!impl || !impl->ok || path.empty()) return 0;
  Impl& m = *impl;
  if (m.vMobs.load() <= 0) return 0;
  static std::mutex failMx;
  static std::unordered_set<std::string> failed;
  { std::lock_guard<std::mutex> lk(failMx); if (failed.count(path)) return 0; }
  const std::string key = "file:" + path;
  std::shared_ptr<AudioBuffer> b = m.cached(key);
  if (!b) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, SR);
    ma_uint64 n = 0; void* pcm = nullptr;
    if (ma_decode_file(path.c_str(), &cfg, &n, &pcm) != MA_SUCCESS || !pcm || n < 2) {
      if (pcm) ma_free(pcm, nullptr);
      std::lock_guard<std::mutex> lk(failMx); failed.insert(path);
      DQ_WARN("audio: can't read %s", path.c_str());
      return 0;
    }
    b = std::make_shared<AudioBuffer>();
    b->d.assign((float*)pcm, (float*)pcm + (size_t)n);
    ma_free(pcm, nullptr);
    std::lock_guard<std::mutex> lk(m.cmx);
    m.cacheBytes += b->d.size() * 4;
    m.cache[key] = b;
  }
  AudioCmd c;
  if (!m.place(pos, range, volume, c)) return 0;
  c.t = AudioCmd::Play; c.handle = m.nextHandle++; c.buf = b.get();
  c.rate = std::clamp(pitch, 0.25f, 4.f); c.cat = CAT_MOBS;
  b->lastStart = m.now; m.nPlays++;
  m.send(c);
  if (seconds) *seconds = (float)(b->d.size() / (double)SR);
  return c.handle;
}

int AudioEngine::playAt(const std::string& name, vec3 pos, float range, float volume, float pitch) {
  Impl& m = *impl;
  if (!m.ok || !m.libReady) return 0;
  SoundRef r = m.lib.resolve(name);
  if (!r) return 0;
  if (r.mob && m.vMobs.load() <= 0) return 0;
  AudioCmd c;
  if (!m.place(pos, range, volume, c)) return 0;
  if (!m.gapOk(name, r.gap)) return 0;
  int h = m.start(r, c, pitch);
  // a real shot, so the bed can put the fight where it happened instead of rolling dice for hill gunfire
  if (h && name.rfind("zone_gun_", 0) == 0 && name.find("dry") == std::string::npos && name.find("mag") == std::string::npos
      && name.find("rack") == std::string::npos && name.find("jam") == std::string::npos && name.find("supp") == std::string::npos) {
    m.zoneGunAt = pos; m.zoneGunT = m.now;
  }
  return h;
}

// Exclusion: in the Zone a footfall is the surface under it (dirt, wood, metal, water, concrete), not the block game's step.
static const char* zoneStepName(const AudioScene& sc, const std::string& mat, const std::string& kind) {
  if (kind != "step" || sc.place.rfind("zone", 0) != 0) return nullptr;
  auto has = [&](const char* s) { return mat.find(s) != std::string::npos; };
  if (has("water")) return "zone_step_water";
  if (has("metal") || has("iron") || has("anvil") || has("copper")) return "zone_step_metal";
  if (has("wood") || has("ladder")) return "zone_step_wood";
  if (has("stone") || has("concrete") || has("brick")) return "zone_step_concrete";
  return "zone_step_dirt";
}

void AudioEngine::material(const std::string& mat, const std::string& kind) {
  Impl& m = *impl;
  if (!m.ok || !m.libReady) return;
  if (const char* z = zoneStepName(m.scene, mat, kind)) { play(z, 0.7f, 1); return; }
  SoundRef r = m.lib.material(mat, kind, false);
  if (!r || !m.gapOk(kind + mat, r.gap)) return;
  AudioCmd c; m.start(r, c, 1);
}

void AudioEngine::materialAt(const std::string& mat, const std::string& kind, vec3 pos, float range, float volume) {
  Impl& m = *impl;
  if (!m.ok || !m.libReady) return;
  if (const char* z = zoneStepName(m.scene, mat, kind)) { playAt(z, pos, range, volume, 1); return; }
  SoundRef r = m.lib.material(mat, kind, true);
  if (!r) return;
  AudioCmd c;
  if (!m.place(pos, range, volume, c)) return;
  m.start(r, c, 1);
}

void AudioEngine::note(const std::string& instrument, int pitch, vec3 pos) {
  Impl& m = *impl;
  if (!m.ok || !m.libReady) return;
  SoundRef r = m.lib.note(pitch, instrument);
  if (!m.gapOk("note" + std::to_string(pitch), r.gap)) return;
  AudioCmd c;
  if (!m.place(pos, 48, 1, c)) return;
  m.start(r, c, 1);
}

// A creature's call. The resource pack's recording (resynthesised) wins, else the original's synthesis. Where
// neither has that call, it borrows one so every creature is heard: hurt = its own call pitched up, death =
// its hurt (or call) pitched down, angry/howl/purr = its call; a step it has no sound for = the block underfoot.
void AudioEngine::voice(const std::string& creature, const std::string& kind, vec3 pos, float pitch) {
  Impl& m = *impl;
  if (!m.ok || !m.libReady || creature.empty()) return;
  auto has = [&](const std::string& n) { return (bool)m.lib.resolve(n); };
  // Exclusion: the Zone's mutants keep their engine creature ids, and speak with the Zone's own voices.
  if (m.scene.place.rfind("zone", 0) == 0 && kind != "step") {
    const char* fam = nullptr;
    if (creature == "wolf" || creature == "werewolf") fam = "dog";
    else if (creature == "boar") fam = "boar";
    else if (creature == "mouse") fam = "rat";
    else if (creature == "ghoul") fam = "crawler";
    else if (creature == "bigcat") fam = "cat";
    else if (creature == "husk") fam = "husk";
    else if (creature == "enderman") fam = "puppet";
    else if (creature == "ogre") fam = "brute";
    if (fam) {
      const char* k = "idle";
      if (kind == "angry" || kind == "howl") k = "hunt";
      else if (kind == "hurt") k = "hurt";
      else if (kind == "death") k = "death";
      playAt(std::string("zone_mut_") + fam + "_" + k, pos, kind == "death" ? 36.f : 28.f, 1, pitch);
      return;
    }
  }
  if (kind == "step") {
    if (has("mstep_" + creature)) { playAt("mstep_" + creature, pos, 16.f, 1, pitch); return; }
    if (m.scene.world) {
      BlockId id = m.scene.world->getBlock((int)std::floor(pos.x), (int)std::floor(pos.y - 0.2f), (int)std::floor(pos.z));
      if (id) materialAt(reg().block(id).sound, "step", pos, 14, 0.35f);
    }
    return;
  }
  if (creature == "golem") {   // an iron golem: no voice, a clang of iron when struck
    if (kind == "hurt" || kind == "death") playAt("anvil_land", pos, 28.f, kind == "death" ? 0.9f : 0.6f, pitch * (kind == "death" ? 0.7f : 1.3f));
    return;
  }
  std::string n = kind.empty() || kind == "say" ? creature : creature + "_" + kind;
  float pm = 1;
  if (!has(n)) {
    if (kind == "hurt") { n = creature; pm = 1.2f; }
    else if (kind == "death") { if (has(creature + "_hurt")) { n = creature + "_hurt"; pm = 0.8f; } else { n = creature; pm = 0.72f; } }
    else n = creature;
  }
  playAt(n, pos, 28.f, 1, pitch * pm);
}

double AudioEngine::audioClock() const { return impl ? impl->audioNow() : 0; }
void AudioEngine::musicNote(int ins, float midi, float dur, float vel, double at) {
  if (!impl) return;
  AudioCmd c; c.t = AudioCmd::Note; c.at = at; c.ins = (u8)ins; c.a = midi; c.b = dur; c.c = vel; c.bus = 0; impl->send(c);
  AudioCmd v; v.t = AudioCmd::Bus; v.bus = 0; v.a = 1; v.b = 0; impl->send(v);
}
int AudioEngine::playSamples(const std::vector<float>& mono48k, float volume, float pitch) {
  if (!impl || !impl->ok || mono48k.size() < 2) return 0;
  static std::deque<std::unique_ptr<AudioBuffer>> kept;   // a few studio buffers stay alive while they may be playing
  kept.push_back(std::make_unique<AudioBuffer>()); kept.back()->d = mono48k;
  while (kept.size() > 6) kept.pop_front();
  AudioCmd c; c.t = AudioCmd::Play; c.buf = kept.back().get(); c.gL = c.gR = volume; c.rate = pitch; c.cat = CAT_SFX; c.handle = impl->nextHandle++;
  impl->send(c);
  return c.handle;
}
void AudioEngine::musicClear() { if (!impl) return; AudioCmd c; c.t = AudioCmd::MusicClear; c.bus = 0; impl->send(c); }
void AudioEngine::stop(int handle) { AudioCmd c; c.t = AudioCmd::Stop; c.handle = handle; impl->send(c); }

void AudioEngine::update(double dt) {
  Impl& m = *impl;
  if (!m.ok) return;
  m.now += dt;
  // sounds that were waiting for their take to be rendered
  for (size_t i = 0; i < m.pending.size();) {
    auto& p = m.pending[i];
    if (auto b = m.cached(p.key)) { b->lastStart = m.now; p.cmd.buf = b.get(); m.send(p.cmd); m.pending.erase(m.pending.begin() + i); continue; }
    if (m.now - p.t > 0.3) { m.pending.erase(m.pending.begin() + i); continue; }
    i++;
  }
  if (!m.libReady) return;
  if (m.ambience) m.ambience->update(dt, m.scene);
  if (m.director) m.director->update(dt, m.scene);
  // keep the cache small: buffers not started for a while (longer than any of them lasts) go
  static double sweep = 0;
  if ((sweep += dt) > 10) {
    sweep = 0;
    std::lock_guard<std::mutex> l(m.cmx);
    if (m.cacheBytes > (48u << 20))
      for (auto it = m.cache.begin(); it != m.cache.end();) {
        double len = it->second->d.size() / (double)SR;
        if (m.now - it->second->lastStart > 30 + len) { m.cacheBytes -= it->second->d.size() * 4; it = m.cache.erase(it); } else ++it;
      }
  }
}

// ---- tools ----------------------------------------------------------------------------------------------------------
void AudioEngine::mixOffline(float* out, int frames) { if (impl->ok) impl->mix(out, frames); }
std::vector<float> AudioEngine::renderSound(const std::string& name, int take, u32 seed) {
  Impl& m = *impl;
  preloadWait(5);
  if (name.rfind("orig:", 0) == 0) {   // the original's own synthesis, even where the pack has the sound
    bool u = m.lib.usePack; m.lib.usePack = false;
    auto d = renderSound(name.substr(5), take, seed);
    m.lib.usePack = u; return d;
  }
  if (name.rfind("recipe:", 0) == 0) { SoundRef r; r.kind = SoundRef::Recipe; r.key = name.substr(7); return m.lib.render(r, take, seed); }
  SoundRef r;
  auto dot = name.find('.');
  if (name.rfind("pack:", 0) == 0) { const Profile* p = m.lib.packs().profile(name.substr(5)); return p ? m.lib.packs().render(*p, seed) : std::vector<float>{}; }
  if (name.rfind("note:", 0) == 0) r = m.lib.note(std::atoi(name.c_str() + 5), "wood");
  else if (dot != std::string::npos && name.find('.', dot + 1) == std::string::npos && name.rfind("fx.", 0) != 0 && name.rfind("vx.", 0) != 0 && name.rfind("mob.", 0) != 0)
    r = m.lib.material(name.substr(0, dot), name.substr(dot + 1), false);
  else r = m.lib.resolve(name);
  if (!r) return {};
  return m.lib.render(r, take, seed);
}
std::vector<std::string> AudioEngine::soundNames() const { return impl->lib.names(); }
void AudioEngine::preloadWait(double seconds) {
  double t0 = steadyNow();
  while (!impl->libReady && steadyNow() - t0 < seconds) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  for (;;) {
    bool idle; { std::lock_guard<std::mutex> l(impl->jmx); idle = impl->jobs.empty(); }
    if (idle) { std::lock_guard<std::mutex> l(impl->cmx); idle = impl->rendering.empty(); }
    if (idle || steadyNow() - t0 > seconds) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

} // namespace dq
