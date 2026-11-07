#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pp::mods {

struct Json {
  enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;

  static Json str(std::string v) { Json j; j.t = Str; j.s = std::move(v); return j; }
  static Json num(double v) { Json j; j.t = Num; j.n = v; return j; }
  static Json boolean(bool v) { Json j; j.t = Bool; j.b = v; return j; }
  static Json array() { Json j; j.t = Arr; return j; }
  static Json object() { Json j; j.t = Obj; return j; }
  const Json* get(const std::string& k) const {
    if (t != Obj) return nullptr;
    for (auto& kv : o) if (kv.first == k) return &kv.second;
    return nullptr;
  }
  void set(const std::string& k, Json v) {
    t = Obj;
    for (auto& kv : o) if (kv.first == k) { kv.second = std::move(v); return; }
    o.emplace_back(k, std::move(v));
  }
  std::string asStr(const std::string& def = "") const { return t == Str ? s : t == Num ? std::to_string((long long)n) : def; }
  double asNum(double def = 0) const { return t == Num ? n : t == Bool ? (b ? 1 : 0) : def; }
  bool asBool(bool def = false) const { return t == Bool ? b : t == Num ? n != 0 : def; }
  std::string dump(int indent = 0) const;
  static bool parse(std::string_view text, Json& out, std::string* err);
};

}
