#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace swd2 {

// Typed view of the combat fields copied by FIG.EXE's definition loader
// (1000:3396) from an ITEM.EXE record. Fields whose precise gameplay role is
// not yet proven retain their source offset instead of acquiring a guessed
// name. One ITEM HP word initializes both current and maximum runtime HP.
struct MonsterDefinition {
    std::uint16_t id{};
    std::uint16_t sprite_number{};
    std::array<std::uint8_t, 5> resistance_flags{};  // +16,+17,+18,+1a,+1b
    std::uint16_t vertical_position{};               // +24
    std::uint16_t status_strength{};                  // +26
    std::uint16_t ai_type{};                         // +28
    std::uint16_t hit_points{};                      // +2c
    std::uint16_t primary_ability_chance{};           // +2e
    std::uint16_t generic_ability{};                  // +32
    std::uint16_t level{};                           // +34
    std::uint16_t initiative_range{};                 // +36
    std::uint16_t physical_attack{};                 // +38
    std::uint16_t healing_ability{};                  // +3a
    std::uint16_t secondary_ability_chance{};         // +3e
    std::uint16_t speed{};                           // +40
    std::uint16_t special_ability_a{};                // +42
    std::uint16_t ability_points{};                   // +44
    std::uint16_t special_ability_b{};                // +46
    std::uint16_t experience_reward{};               // +48
    std::uint16_t money_reward{};                    // +4a
    std::uint16_t evasion{};                         // +4c
    std::uint16_t physical_defense{};                // +4e

    static MonsterDefinition parse(std::uint16_t id,
                                   std::span<const std::uint8_t> item_record);
};

}  // namespace swd2
