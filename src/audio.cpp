#include "audio.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <cctype>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>

#include "image.h"
#include "log.h"

#ifdef PP_HAVE_OPENMPT
#include <libopenmpt/libopenmpt.h>
#endif

#include <vorbis/vorbisfile.h>

namespace pp {

namespace {
struct OggMem {
  const uint8_t* d;
  size_t n, pos;
};
size_t oggRead(void* p, size_t sz, size_t nm, void* h) {
  auto* m = (OggMem*)h;
  size_t want = sz * nm, got = std::min(want, m->n - m->pos);
  std::memcpy(p, m->d + m->pos, got);
  m->pos += got;
  return sz ? got / sz : 0;
}
int oggSeek(void* h, ogg_int64_t off, int whence) {
  auto* m = (OggMem*)h;
  ogg_int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (ogg_int64_t)m->pos : (ogg_int64_t)m->n;
  ogg_int64_t np = base + off;
  if (np < 0 || np > (ogg_int64_t)m->n) return -1;
  m->pos = (size_t)np;
  return 0;
}
long oggTell(void* h) { return (long)((OggMem*)h)->pos; }

bool decodeOgg(const std::vector<uint8_t>& bytes, Sample& out) {
  OggMem mem{bytes.data(), bytes.size(), 0};
  ov_callbacks cb{oggRead, oggSeek, nullptr, oggTell};
  OggVorbis_File vf;
  if (ov_open_callbacks(&mem, &vf, nullptr, 0, cb) < 0) return false;
  vorbis_info* vi = ov_info(&vf, -1);
  int ch = vi->channels;
  out.rate = (int)vi->rate;
  out.pcm.clear();
  std::vector<float> chunk;
  for (;;) {
    float** pcm = nullptr;
    int bs = 0;
    long n = ov_read_float(&vf, &pcm, 4096, &bs);
    if (n <= 0) break;
    for (long i = 0; i < n; i++) {
      out.pcm.push_back(pcm[0][i]);
      out.pcm.push_back(ch > 1 ? pcm[1][i] : pcm[0][i]);
    }
  }
  ov_clear(&vf);
  return !out.pcm.empty();
}
}

float volumeCurve(float v) {
  if (v <= 0) return 0;
  if (v >= 1) return 1;
  int db = (int)((std::log10(1.0 + 9.0 * v) - 1.0) * 2333.0);
  if (db < -2000) return 0;
  return (float)std::pow(10.0, db / 2000.0);
}

struct Audio::Music {
#ifdef PP_HAVE_OPENMPT
  openmpt_module* mod = nullptr;
  int lockOrder = -1;
  ~Music() { if (mod) openmpt_module_destroy(mod); }
#endif
  std::vector<uint8_t> file;
  Sample ogg;
  double pos = 0;
  bool loop = true, isOgg = false;
  float gain = 1.0f;
  float level = 1.0f;
  float top = 1.0f;
  float rise = 0.0f;
  bool fixed = false;
  float fade = 1.0f;
  std::string path;
};

Audio::Audio(const Vfs& v) : vfs_(v) { loadManifest(); }

Audio::~Audio() {
  if (stream_) SDL_DestroyAudioStream(stream_);
}

void Audio::loadManifest() {
  manifest_.clear();
  auto h = vfs_.read("properties/resources.xml");
  if (!h) return;
  std::string x(h->bytes.begin(), h->bytes.end());
  std::regex tag("<Sound\\b([^>]*)>", std::regex::icase);
  auto attr = [](const std::string& s, const char* name) {
    std::regex a(std::string("\\b") + name + "\\s*=\\s*(\"([^\"]*)\"|([^\\s/>]+))", std::regex::icase);
    std::smatch m;
    if (!std::regex_search(s, m, a)) return std::string();
    return m[2].matched ? m[2].str() : m[3].str();
  };
  for (std::sregex_iterator it(x.begin(), x.end(), tag), end; it != end; ++it) {
    std::string a = (*it)[1];
    std::string id = attr(a, "id"), path = attr(a, "path"), vol = attr(a, "volume");
    if (id.empty() || path.empty()) continue;
    manifest_[id] = {"sounds/" + path, vol.empty() ? 1.0f : (float)std::atof(vol.c_str())};
  }
}

void Audio::reloadAssets(const VfsChange& change) {
  if (!change.any) return;
  const bool all = change.touches("properties/resources.xml");
  std::vector<std::string> drop;
  for (auto& [id, smp] : cache_) {
    auto m = manifest_.find(id);
    if (all || m == manifest_.end() || change.touches(m->second.first + ".ogg")) drop.push_back(id);
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& id : drop) {
      const Sample* s = cache_[id].get();
      voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.s == s; }), voices_.end());
      cache_.erase(id);
    }
  }
  if (all) loadManifest();
}

const Sample* Audio::sample(const std::string& id) {
  auto c = cache_.find(id);
  if (c != cache_.end()) return c->second.get();
  auto m = manifest_.find(id);
  std::unique_ptr<Sample> out;
  if (m != manifest_.end()) {
    auto h = vfs_.read(m->second.first + ".ogg");
    if (h) {
      auto sm = std::make_unique<Sample>();
      if (decodeOgg(h->bytes, *sm)) {
        sm->baseVolume = m->second.second;
        out = std::move(sm);
      }
    }
  }
  if (!out) vfsLogMiss("sound", id);
  auto* raw = out.get();
  cache_[id] = std::move(out);
  return raw;
}

void Audio::play(const std::string& id, float semitones, float volume, int throttle, bool force) {
  const Sample* s = sample(id);
  if (!s) return;
  std::lock_guard<std::mutex> lk(mu_);
  auto lp = lastPlayed_.find(id);
  if (!force && throttle > 0 && lp != lastPlayed_.end() && updates_ - lp->second < throttle) return;
  lastPlayed_[id] = updates_;
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.pos >= (double)v.s->pcm.size() / 2; }), voices_.end());
  if (voices_.size() >= 32) return;
  double freq = s->rate * std::pow(2.0, semitones / 12.0);
  freq = std::clamp(freq, 100.0, 100000.0);
  Voice v;
  v.s = s;
  v.step = freq / 44100.0;
  v.vol = std::clamp(s->baseVolume * volume, 0.0f, 1.0f);
  voices_.push_back(v);
}

void Audio::playLoop(const std::string& id) {
  const Sample* s = sample(id);
  if (!s || s->pcm.size() < 4) return;
  std::lock_guard<std::mutex> lk(mu_);
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.pos >= (double)v.s->pcm.size() / 2; }), voices_.end());
  if (voices_.size() >= 32) return;
  Voice v;
  v.s = s;
  v.step = std::clamp((double)s->rate, 100.0, 100000.0) / 44100.0;
  v.vol = std::clamp(s->baseVolume, 0.0f, 1.0f);
  v.loop = true;
  voices_.push_back(v);
}

void Audio::stopSample(const std::string& id) {
  const Sample* s = sample(id);
  if (!s) return;
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& v : voices_)
    if (v.s == s) {
      if (!v.loop) {
        v.pos = (double)s->pcm.size() / 2;
      } else if (!v.fading) {
        v.fading = true;
        fadeStep(v);
      }
    }
}

void Audio::fadeStep(Voice& v) {
  v.inst -= 0.05f;
  if (v.inst > 0) {
    v.vol = std::clamp(v.s->baseVolume * v.inst, 0.0f, 1.0f);
    return;
  }
  v.fading = false;
  v.loop = false;
  v.pos = (double)v.s->pcm.size() / 2;
}

void Audio::stopLoops() {
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& v : voices_)
    if (v.loop) {
      v.loop = false;
      v.pos = (double)v.s->pcm.size() / 2;
    }
  fireLoop_ = false;
  chainLoop_ = false;
}

void Audio::stopSounds() {
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& v : voices_) {
    v.loop = false;
    v.pos = (double)v.s->pcm.size() / 2;
  }
  fireLoop_ = false;
  chainLoop_ = false;
}

void Audio::playFx(const std::vector<FxSound>& s) {
  for (const auto& f : s) play(f.id, f.semis, f.vol, f.throttle);
}

void Audio::onEvents(const std::vector<Event>& ev, const Board& b) {
  if (lvlBase_ >= 0 && music_ && !music_->fixed) {
    const int n = b.orangeLeft, ni = n <= 3 ? 6 : n <= 6 ? 5 : n <= 10 ? 4 : n <= 15 ? 3 : n <= 20 ? 2 : 1;
    std::lock_guard<std::mutex> lk(mu_);
    if (intensity_ == 0) intensity_ = ni;
    else if (ni > intensity_) {
      intensity_ = ni;
      pendOrder_ = lvlBase_ + ni - 1;
#ifdef PP_HAVE_OPENMPT
      pendRow_ = music_->mod ? openmpt_module_get_current_row(music_->mod) / 16 : 0;
#endif
    }
  }
  for (auto& e : ev) {
    switch (e.type) {
      case Event::PegHit: {
        if (e.c == 1) break;
        if (e.c > 1) {
          playLater(2 * (e.c - 1), "PEGHIT", (float)(e.a - 1 - 12), 1, 1);
          break;
        }
        int semis = e.a - 12;
        if (e.b >= 75000) play("PEGHIT3", 0, 1, 1);
        else if (e.b >= 25000) play("PEGHIT2", (float)semis, 1, 1);
        else play("PEGHIT", (float)semis, 1, 1);
        break;
      }
      case Event::PegPop: play("PEGPOP", 0, 1, 10); break;
      case Event::CoinSpin: play("COINSPIN"); break;
      case Event::CoinFlip: play(e.a ? "FREEBALL" : "COINSPIN_NO"); break;
      case Event::LevelDone:
        play((b.cfg.duel && b.players > 1) ? "LEVELDONE" : e.a ? "LEVELWON" : "LEVELLOST", 0, 1, 1);
        if (b.result.cleared) {
          playLater(130, "TING", 0, 1, 1);
          playLater(160, "APPLAUSE", 0, 1, 1);
        }
        break;
      case Event::ExtraBall: play(e.a == 0 ? "EXTRABALL" : e.a == 1 ? "EXTRABALL2" : "EXTRABALL3"); playLater(20, "ADD_BALL"); break;
      case Event::FreeBall: if (e.a != 2) { play("FREEBALL"); playLater(20, "ADD_BALL"); } break;
      case Event::Bump: play(e.a ? "FIREBALLBOUNCE" : "RICHOCHET", 0, 1, 2); break;
      case Event::PointBoost: play("POINTBOOST"); break;
      case Event::Fever:
        play("CYMBAL");
        duck_ = 1.0f; duckRate_ = 0.01f; duckHold_ = 0;
        if (vfs_.exists("music/odetojoy.ogg")) playMusic("music/odetojoy.ogg", false, 0, 0.85f);
        break;
      case Event::FeverSlot:
        if (e.b == 1) play("SCORECOUNTER", (float)b.feverBallsPaid, 1, 1);
        else if (e.b >= 2) play(e.a > 50000 ? "FIREWORKS2" : "FIREWORKS1", 0, 1, 1);
        break;
        break;
      case Event::Power: {
        static const char* ids[] = {"", "POWERUP_GUIDE", "POWERUP_FLIPPERS", "POWERUP_MULTIBALL", "POWERUP_PYRAMID", "POWERUP_SPACEBLAST",
                                    "POWERUP_SPOOKYBALL", "POWERUP_ZEN", "POWERUP", "POWERUP_FLOWERPOWER", "", "", "POWERUP_FIREBALL", "POWERUP_LUCKYSPIN", "POWERUP_LIGHTNING"};
        static const int duck[] = {0, 0, 0, 0, 70, 0, 100, 100, 0, 0, 0, 0, 200, 0, 0};
        if (e.a >= 200 && e.a <= 203) {
          static const char* r[] = {"TING", "FREEBALL", "AAH", "EXTRABALL"};
          play(r[e.a - 200]);
          break;
        }
        if (e.a == 102) { play("AAH", 0, 1, 1); break; }
        if (e.a >= masters::kFirstPower && e.a < masters::kPowerSlots) {
          play("POWERUP", 0, 1, 1);
          break;
        }
        if (e.a <= 0 || e.a > 14) break;
        if (e.a == 5) play(e.b ? "EXPLODE" : "POWERUP_SPACEBLAST", e.b ? -24.0f : 0.0f);
        else if (e.a == 6) play(e.b == 0 ? "POWERUP_SPOOKYBALL" : e.b == 1 ? "SPOOKYBALL2" : "SPOOKYBALL3");
        else if (ids[e.a][0]) play(ids[e.a], 0, 1, e.a == 8 ? 1 : 0);
        if (duck[e.a]) fadeMusic(duck[e.a]);
        break;
      }
      case Event::SpookyRespawn: play(e.a == 0 ? "SPOOKYBALL2" : e.a == 1 ? "SPOOKYBALL3" : "SPOOKYBALL4"); break;
      case Event::Penalty: if (e.a > 0) playLater(e.a, "PENALTY", 0, 1, 1); else play("PENALTY", 0, 1, 1); break;
      case Event::FlipperMove: play(e.a ? "FLIPPERUP" : "FLIPPERDOWN"); break;
      case Event::Zen: if (e.a == 1 || e.a == 2) play("GONG"); break;
      case Event::Hint: play("COINSPIN_NO"); break;
      case Event::StyleShot: if (e.a != 18) play("SKILLSHOT", 0, 1, 1); break;
      case Event::ChainStart: play("LIGHTNING_START"); chainLoopAt_ = updates_ + 40; break;
      case Event::ChainStrike: play("POWERUP_LIGHTNING_HIT", 0, 1, 1); break;
      case Event::ChainBolt:
        if (e.b >= 0) playLater(e.c & 0xff, "PEGSPARK", (float)((e.c >> 16) - 12), 1, 1);
        break;
      case Event::TimeBomb: play("EXPLODE"); break;
      default: break;
    }
  }
  int ph = (int)b.phase;
  if (b.fireHeld && !prevHeld_ && ph == (int)Phase::Aim) play("CANNONCOCK", 0, 1, 1);
  prevHeld_ = b.fireHeld;
  if (prevPhase_ >= 0 && ph != prevPhase_) {
    if (prevPhase_ == (int)Phase::Aim && ph == (int)Phase::Shot) play(!b.balls.empty() && b.balls[0].fire ? "FIREBALLSHOT" : "CANNONSHOT");
  }
  prevPhase_ = ph;
  if (!leaving_) {
    bool fireAlive = b.phase == Phase::Aim && b.pturn[b.cur][Board::P_FIREBALL] > 0;
    for (const auto& bl : b.balls) fireAlive = fireAlive || (bl.fire && bl.alive);
    if (fireAlive && !fireLoop_) {
      playLoop("FIREBALLLOOP");
      fireLoop_ = true;
    } else if (!fireAlive && fireLoop_) {
      stopSample("FIREBALLLOOP");
      fireLoop_ = false;
    }
  }
  if (!leaving_) {
    bool chainAlive = b.phase == Phase::Aim && b.cfg.nights && b.pturn[b.cur][Board::P_CHAIN] > 0;
    for (const auto& bl : b.balls) chainAlive = chainAlive || (bl.chain && bl.alive);
    chainAlive = chainAlive && updates_ >= chainLoopAt_;
    if (chainAlive && !chainLoop_) {
      playLoop("LIGHTNING_LOOP");
      chainLoop_ = true;
    } else if (!chainAlive && chainLoop_) {
      stopSample("LIGHTNING_LOOP");
      chainLoop_ = false;
    }
  }
  if (b.slowOn && !prevSlow_) {
    fadeMusic(75);
    playLater(30, "SLOWMO", 0, 1, 30);
  }
  prevSlow_ = b.slowOn;
  if (b.slowOn && b.slowC3 && b.slowSpeed >= 900 && b.slowSpeed < 920 && !sighDone_) { play("SIGH", 0, 1, 1); sighDone_ = true; }
  if (!b.slowOn) sighDone_ = false;
}

void Audio::tick() {
  updates_++;
  for (size_t i = 0; i < pending_.size();) {
    if (pending_[i].at <= updates_) {
      Pending p = pending_[i];
      pending_.erase(pending_.begin() + (long)i);
      play(p.id, p.semis, p.vol, p.throttle);
    } else {
      i++;
    }
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& v : voices_)
      if (v.fading) fadeStep(v);
  }
  if (duckHold_ > 0 && --duckHold_ == 0) duckRate_ = 0.01f;
  duck_ = std::clamp(duck_ + duckRate_, 0.4f, 1.0f);
}

void Audio::tickMusic() {
  std::lock_guard<std::mutex> lk(mu_);
  if (music_ && music_->level < music_->top) music_->level = std::min(music_->top, music_->level + music_->rise);
  for (auto& f : fading_) f->fade -= 0.01f;
  fading_.erase(std::remove_if(fading_.begin(), fading_.end(), [](const std::unique_ptr<Music>& f) { return f->fade <= 0.0f; }), fading_.end());
#ifdef PP_HAVE_OPENMPT
  if (pendOrder_ >= 0 && music_ && music_->mod) {
    if (openmpt_module_get_current_row(music_->mod) / 16 != pendRow_) {
      openmpt_module_set_position_order_row(music_->mod, pendOrder_, 0);
      music_->lockOrder = pendOrder_;
      pendOrder_ = -1;
    }
  }
#endif
}

void Audio::fadeMusic(int frames) {
  if (sfxVol_ <= 0 || musicVol_ <= 0) return;
  duckHold_ = std::max(duckHold_, frames);
  duckRate_ = -0.03f;
}

void Audio::playLevelMusic(bool nights) {
  if (playlist_.empty()) {
    for (int i = 0; i < (nights ? 10 : 9); i++)
      if (nights || i != 4) playlist_.push_back(i);
    uint32_t r = (uint32_t)SDL_GetTicks() * 2654435761u + 12345u;
    for (size_t i = 1; i < playlist_.size(); i++) {
      r = r * 1103515245u + 12345u;
      std::swap(playlist_[i], playlist_[(r >> 8) % (i + 1)]);
    }
  }
  int e = playlist_[playIdx_++ % playlist_.size()];
  duck_ = 1.0f;
  duckRate_ = 0.01f;
  duckHold_ = 0;
  if (nights) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "music/%02d.mo3", e + 1);
    playMusic(buf, true);
  } else {
    playMusic("music/PeggleBeats.mo3", true, e * 6);
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    lvlBase_ = nights ? 0 : e * 6;
    intensity_ = 0;
    pendOrder_ = -1;
#ifdef PP_HAVE_OPENMPT
    if (music_ && music_->mod) music_->lockOrder = lvlBase_;
#endif
  }
}

void Audio::playMusic(const std::string& path, bool loop, int startOrder, float gain, float level, float rise) {
  auto h = vfs_.read(path);
  if (!h) {
    vfsLogMiss("music", path);
    return;
  }
  { std::lock_guard<std::mutex> lk(mu_); lvlBase_ = -1; }
  duck_ = 1.0f;
  duckRate_ = 0.01f;
  duckHold_ = 0;
  auto m = std::make_unique<Music>();
  m->loop = loop;
  m->gain = gain;
  m->level = rise > 0.0f ? 0.0f : level;
  m->top = level;
  m->rise = rise;
  m->fixed = gain != 1.0f;
  std::string low = path;
  for (auto& c : low) c = (char)std::tolower((unsigned char)c);
  if (low.size() > 4 && low.substr(low.size() - 4) == ".ogg") {
    if (!decodeOgg(h->bytes, m->ogg)) {
      pp::log::writeOnce(pp::log::Level::Warn, "game", "cannot decode music " + path);
      return;
    }
    m->isOgg = true;
  } else {
#ifdef PP_HAVE_OPENMPT
    m->file = std::move(h->bytes);
    int err = 0;
    m->mod = openmpt_module_create_from_memory2(m->file.data(), m->file.size(), nullptr, nullptr, nullptr, nullptr, &err, nullptr, nullptr);
    if (!m->mod) {
      pp::log::writeOnce(pp::log::Level::Warn, "game", "cannot load music " + path);
      return;
    }
    openmpt_module_set_repeat_count(m->mod, loop ? -1 : 0);
    if (startOrder > 0) openmpt_module_set_position_order_row(m->mod, startOrder, 0);
#else
    pp::log::writeOnce(pp::log::Level::Warn, "game", "music " + path + " is skipped: this build cannot play tracker music");
    return;
#endif
  }
  m->path = path;
  std::lock_guard<std::mutex> lk(mu_);
  if (music_) {
    if (!m->fixed && music_->path != path) { music_->fade = 1.0f; fading_.push_back(std::move(music_)); }
  }
  if (m->fixed) fading_.clear();
  music_ = std::move(m);
}

int Audio::musicOrder() const {
#ifdef PP_HAVE_OPENMPT
  std::lock_guard<std::mutex> lk(mu_);
  if (music_ && music_->mod) return openmpt_module_get_current_order(music_->mod);
#endif
  return -1;
}

void Audio::fadeOutMusic() {
  std::lock_guard<std::mutex> lk(mu_);
  if (music_) { music_->fade = 1.0f; fading_.push_back(std::move(music_)); }
  lvlBase_ = -1;
}

void Audio::stopMusic() {
  std::lock_guard<std::mutex> lk(mu_);
  music_.reset();
  fading_.clear();
  lvlBase_ = -1;
}

int Audio::activeVoices() const {
  std::lock_guard<std::mutex> lk(mu_);
  int n = 0;
  for (auto& v : voices_) n += v.pos < (double)v.s->pcm.size() / 2;
  return n;
}

void Audio::render(float* out, int frames) {
  std::fill(out, out + (size_t)frames * 2, 0.0f);
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& v : voices_) {
    const auto& pcm = v.s->pcm;
    double n = (double)pcm.size() / 2;
    const float g = volumeCurve(v.vol * sfxVol_);
    for (int i = 0; i < frames && (v.loop || v.pos < n - 1); i++) {
      size_t k = (size_t)v.pos;
      size_t k1 = k + 1 < (size_t)n ? k + 1 : 0;
      float f = (float)(v.pos - (double)k);
      out[i * 2] += (pcm[k * 2] * (1 - f) + pcm[k1 * 2] * f) * g;
      out[i * 2 + 1] += (pcm[k * 2 + 1] * (1 - f) + pcm[k1 * 2 + 1] * f) * g;
      v.pos += v.step;
      if (v.loop && v.pos >= n) v.pos -= n;
    }
    if (!v.loop && v.pos >= n - 1) v.pos = n;
  }
  auto mixMusic = [&](Music& m, float mg) {
    if (m.isOgg) {
      double n = (double)m.ogg.pcm.size() / 2, step = m.ogg.rate / 44100.0;
      for (int i = 0; i < frames; i++) {
        if (m.pos >= n - 1) {
          if (!m.loop) break;
          m.pos = 0;
        }
        size_t k = (size_t)m.pos;
        out[i * 2] += m.ogg.pcm[k * 2] * mg;
        out[i * 2 + 1] += m.ogg.pcm[k * 2 + 1] * mg;
        m.pos += step;
      }
    }
#ifdef PP_HAVE_OPENMPT
    else if (m.mod) {
      std::vector<float> tmp((size_t)frames * 2);
      int done = 0;
      while (done < frames) {
        int want = frames - done;
        if (m.lockOrder >= 0) {
          int pat = openmpt_module_get_current_pattern(m.mod);
          int rows = openmpt_module_get_pattern_num_rows(m.mod, pat);
          if (openmpt_module_get_current_row(m.mod) >= rows - 1) want = std::min(want, 32);
        }
        size_t got = openmpt_module_read_interleaved_float_stereo(m.mod, 44100, (size_t)want, tmp.data());
        if (got == 0) break;
        for (size_t i = 0; i < got * 2; i++) out[((size_t)done) * 2 + i] += tmp[i] * mg;
        done += (int)got;
        if (m.lockOrder >= 0 && openmpt_module_get_current_order(m.mod) != m.lockOrder) openmpt_module_set_position_order_row(m.mod, m.lockOrder, 0);
      }
    }
#endif
  };
  if (music_) mixMusic(*music_, music_->fixed ? feverVol_ : musicVol_ * duck_ * music_->level);
  for (auto& f : fading_) mixMusic(*f, musicVol_ * duck_ * f->fade * f->level);
  for (int i = 0; i < frames * 2; i++) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

void Audio::callback(void* user, SDL_AudioStream* s, int additional, int) {
  auto* a = (Audio*)user;
  std::vector<float> buf(2048 * 2);
  int need = additional / (int)(sizeof(float) * 2);
  while (need > 0) {
    int n = std::min(need, 2048);
    a->render(buf.data(), n);
    SDL_PutAudioStreamData(s, buf.data(), n * 2 * (int)sizeof(float));
    need -= n;
  }
}

bool Audio::openDevice() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    pp::log::error("game", std::string("cannot open the audio device: ") + SDL_GetError());
    return false;
  }
  SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, this);
  if (!stream_) {
    pp::log::error("game", std::string("cannot open the audio device: ") + SDL_GetError());
    return false;
  }
  SDL_ResumeAudioStreamDevice(stream_);
  pp::log::info("game", "audio device opened");
  return true;
}

}
