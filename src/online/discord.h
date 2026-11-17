#pragma once

#include <memory>
#include <string>

namespace pp::online {

class Discord {
public:
  struct Presence {
    std::string details;
    std::string state;
    std::string partyId;
    std::string joinSecret;
    int partySize = 0;
    int partyMax = 0;
    bool joinable = false;
  };
  enum class State { Disconnected, Connecting, Ready, Failed, Stopping, Stopped };
  struct Status {
    State state = State::Disconnected;
    bool activityConfirmed = false;
    bool joinSubscribed = false;
    int errorCode = 0;
    std::string error;
    std::string errorCommand;
  };
  explicit Discord(std::string applicationId);
  ~Discord();
  Discord(const Discord&) = delete;
  Discord& operator=(const Discord&) = delete;
  bool update(const Presence& presence);
  void pump();
  Status status() const;
  std::string takeJoinRequest();
  void clear();
  void stop();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
