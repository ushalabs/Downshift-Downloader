#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace downshift::torrent {
// Session-local liveness only: never persisted as the user's paused/running intent.
class StallRecovery {
public:
    using Clock=std::chrono::steady_clock;
    enum class Action { None, Discover, Reconnect };
    Action observe(Clock::time_point now,bool active,std::int64_t received,std::int64_t verified){
        if(!active){armed_=false;return Action::None;}
        if(!armed_||received>received_||verified>verified_){
            armed_=true;received_=received;verified_=verified;progress_=now;
            retry_=now+std::chrono::seconds(90);announced_=false;attempt_=0;
            return Action::None;
        }
        if(now-progress_<std::chrono::seconds(45))return Action::None;
        if(!announced_){announced_=true;return Action::Discover;}
        if(now<retry_)return Action::None;
        retry_=now+std::chrono::seconds(std::min(600,90*(1<<std::min(attempt_,3))));
        attempt_=std::min(attempt_+1,3);
        return Action::Reconnect;
    }
private:
    bool armed_=false,announced_=false;
    int attempt_=0;
    std::int64_t received_=0,verified_=0;
    Clock::time_point progress_{},retry_{};
};
}
