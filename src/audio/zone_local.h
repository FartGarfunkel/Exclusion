// audio/zone_local.h — optional local recordings (a menu/ending outro, stalker taunts and idles).
// They are not part of the repo. The game looks beside the checkout and under userDir().
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace dq::snd {

// true when miniaudio can open this file. Used once at startup to tell wav/mp3/flac from anything else.
bool recordingOpens(const std::string& path);

class LocalStalkerAudio {
public:
  enum Moment { Idle, Greet, Contact, Warn };
  static void prepare();
  static const std::filesystem::path& outro();          // empty if none, or if miniaudio cannot open it
  static const std::filesystem::path& voiceRoot();      // the lines folder, even when the clips will not decode
  static std::filesystem::path line(int faction, Moment moment, std::uint32_t salt);   // empty if none will play
  static bool voicesPlayable();
  static const std::string& voiceDetail();
  static const std::string& outroDetail();
};

} // namespace dq::snd
