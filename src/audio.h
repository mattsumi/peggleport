#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <utility>

#include "effects.h"
#include "sim.h"
#include "vfs.h"

struct SDL_AudioStream;

namespace pp {

struct Sample {
  std::vector<float> pcm;
  int rate = 44100;
  float baseVolume = 1.0f;
};

class Audio {
 public:
  explicit Audio(const Vfs& v);
  ~Audio();
  bool openDevice();
  void loadManifest();
  void reloadAssets(const VfsChange& change);
  const Sample* sample(const std::string& id);
  void play(const std::string& id, float semitones = 0, float volume = 1.0f, int throttle = 0, bool force = false);
  void playMusic(const std::string& path, bool loop = true, int startOrder = 0, float gain = 1.0f, float level = 1.0f, float rise = 0.0f);
  void playLevelMusic(bool nights);
  void playLoop(const std::string& id);
  void stopSample(const std::string& id);
  void stopLoops();
  void stopSounds();
  void playAfter(int delay, const std::string& id) { playLater(delay, id, 0, 1, 1); }
  void dropQueued() { pending_.clear(); stopLoops(); }
  void setBoardLeaving(bool on) { leaving_ = on; }
  void fadeMusic(int frames);
  void stopMusic();
  void fadeOutMusic();
  void setVolumes(float sfx, float music) { sfxVol_ = sfx; musicVol_ = music; }
  void setFeverVolume(float v) { feverVol_ = v; }
  void setFocusMuted(bool on);
  void onEvents(const std::vector<Event>& ev, const Board& b);
  void addBallSound(const Board& b);
  void tick();
  void tickMusic();
  void playFx(const std::vector<FxSound>& s);
  void render(float* out, int frames);
  int activeVoices() const;
  int rate() const { return 44100; }
  const std::map<std::string, std::pair<std::string, float>>& manifest() const { return manifest_; }
  float duckLevel() const { return duck_; }
  bool musicPlaying() const { return music_ != nullptr; }
  int musicOrder() const;

 private:
  struct Voice {
    const Sample* s = nullptr;
    double pos = 0, step = 1;
    float vol = 1;
    bool loop = false;
    bool fading = false;
    float inst = 1;
  };
  struct Music;
  static void fadeStep(Voice& v);
  const Vfs& vfs_;
  std::map<std::string, std::pair<std::string, float>> manifest_;
  std::map<std::string, std::unique_ptr<Sample>> cache_;
  std::map<std::string, int> lastPlayed_;
  std::vector<Voice> voices_;
  std::unique_ptr<Music> music_;
  mutable std::mutex mu_;
  SDL_AudioStream* stream_ = nullptr;
  float sfxVol_ = 1.0f, musicVol_ = 0.7f;
  float feverVol_ = 0.85f;
  float duck_ = 1.0f, duckRate_ = 0.01f;
  int duckHold_ = 0;
  struct Pending { int at; std::string id; float semis, vol; int throttle; };
  std::vector<Pending> pending_;
  std::vector<int> playlist_;
  size_t playIdx_ = 0;
  int prevPhase_ = -1;
  bool fireLoop_ = false;
  bool chainLoop_ = false;
  bool leaving_ = false;
  bool focusMuted_ = false;
  int chainLoopAt_ = 0;
  bool prevSlow_ = false;
  bool prevHeld_ = false;
  int lvlBase_ = -1, intensity_ = 0, pendOrder_ = -1, pendRow_ = 0;
  std::vector<std::unique_ptr<Music>> fading_;
  bool sighDone_ = false;
  void playLater(int delay, const std::string& id, float semis = 0, float vol = 1, int throttle = 0) {
    if (delay <= 1) play(id, semis, vol, throttle);
    else pending_.push_back({updates_ + delay - 1, id, semis, vol, throttle});
  }
  int updates_ = 0;
  static void callback(void* user, SDL_AudioStream* s, int additional, int total);
};

float volumeCurve(float v);

}
