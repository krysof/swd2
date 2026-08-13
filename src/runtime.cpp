#include "swd2/runtime.hpp"

#include <stdexcept>

namespace swd2 {

namespace {

std::size_t module_index(Module module) {
    switch (module) {
    case Module::menu: return 0;
    case Module::rpg: return 1;
    case Module::figure: return 2;
    case Module::demo: return 3;
    }
    throw std::runtime_error("unknown game module");
}

}  // namespace

void ModuleRegistry::add(std::unique_ptr<GameModule> module) {
    if (!module) {
        throw std::invalid_argument("cannot register a null game module");
    }
    const auto index = module_index(module->module());
    if (modules_[index]) {
        throw std::runtime_error("game module was registered twice");
    }
    modules_[index] = std::move(module);
}

GameModule* ModuleRegistry::find(Module module) const noexcept {
    try {
        return modules_[module_index(module)].get();
    } catch (...) {
        return nullptr;
    }
}

MonolithicRuntime::MonolithicRuntime(ModuleRegistry registry) : registry_(std::move(registry)) {}

LaunchResult MonolithicRuntime::run(GameContext& context) const {
    Launcher launcher;
    return launcher.run([&](Module module, Marker input) {
        auto* implementation = registry_.find(module);
        if (!implementation) {
            return ModuleResult{false, Marker::none};
        }
        return ModuleResult{true, implementation->run(context, input)};
    });
}

LaunchResult MonolithicRuntime::resume(GameContext& context,
                                       Marker marker) const {
    Launcher launcher;
    return launcher.resume(marker, [&](Module module, Marker input) {
        auto* implementation = registry_.find(module);
        if (!implementation) {
            return ModuleResult{false, Marker::none};
        }
        return ModuleResult{true, implementation->run(context, input)};
    });
}

}  // namespace swd2
