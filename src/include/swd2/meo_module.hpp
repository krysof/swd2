#pragma once

#include "swd2/runtime.hpp"

namespace swd2 {

class MeoModule final : public GameModule {
public:
    [[nodiscard]] Module module() const noexcept override { return Module::menu; }
    Marker run(GameContext& context, Marker input) override;
};

}  // namespace swd2
