#pragma once

#include "swd2/runtime.hpp"

namespace swd2 {

// In-process replacement for DEMO.EXE. It owns no DOS memory segment and uses
// the same platform surface/audio APIs as MEO and RPG.
class DemoModule final : public GameModule {
public:
    [[nodiscard]] Module module() const noexcept override { return Module::demo; }
    Marker run(GameContext& context, Marker input) override;
};

}  // namespace swd2
