#pragma once

#include "swd2/runtime.hpp"

namespace swd2 {

// In-process FIG replacement.  Battle assets, formations and shared-state
// transitions are portable C++; no DOS process or 4000:0000 transfer segment
// is used. Remaining work is differential replay/calibration, not a second
// platform-specific rules implementation.
class BattleModule final : public GameModule {
public:
    [[nodiscard]] Module module() const noexcept override { return Module::figure; }
    Marker run(GameContext& context, Marker input) override;
};

}  // namespace swd2
