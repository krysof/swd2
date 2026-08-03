#include "swd2/demo_timeline.hpp"

#include <stdexcept>

namespace swd2 {

std::vector<std::uint8_t> decode_demo_rle(std::span<const std::uint8_t> source) {
    std::vector<std::uint8_t> output;
    std::size_t cursor = 0;
    while (cursor < source.size()) {
        const auto control = source[cursor++];
        if ((control & 0x80U) != 0) {
            if (control == 0xffU && cursor + 2 <= source.size() &&
                source[cursor] == 0xffU && source[cursor + 1] == 0xffU) {
                return output;
            }
            const auto count = static_cast<std::size_t>(control & 0x7fU);
            if (cursor + count > source.size()) {
                throw std::runtime_error("truncated DEMO literal run");
            }
            output.insert(output.end(), source.begin() + static_cast<std::ptrdiff_t>(cursor),
                          source.begin() + static_cast<std::ptrdiff_t>(cursor + count));
            cursor += count;
        } else {
            if (cursor == source.size()) {
                throw std::runtime_error("truncated DEMO repeated run");
            }
            output.insert(output.end(), control, source[cursor++]);
        }
    }
    throw std::runtime_error("DEMO run stream has no ff ff ff terminator");
}

bool DemoByteMaskReveal::step(std::span<const std::uint8_t> source,
                              std::span<std::uint8_t> destination) {
    if (frame_ >= frame_count) return false;
    if (source.size() < region_offset + region_size ||
        destination.size() < region_offset + region_size) {
        throw std::runtime_error("DEMO reveal page is shorter than A000:aeff");
    }

    // DX begins 0101h at the start of every outer pass. DH is a three-way
    // divider while DL grows through 01,03,07,...,ff because the final RCL
    // consumes the carry produced by the eighth AH shift.
    std::uint8_t divider = 1;
    std::size_t pixel = region_offset;
    for (const auto mask_byte : mask_) {
        auto shifted_selector = selector_;
        for (unsigned bit = 0; bit < 8; ++bit, ++pixel) {
            if ((shifted_selector & mask_byte) != 0) {
                --divider;
                if (divider == 0) {
                    destination[pixel] = source[pixel];
                    divider = 3;
                }
            }
            shifted_selector = static_cast<std::uint8_t>(shifted_selector << 1U);
        }
    }
    selector_ = static_cast<std::uint8_t>((selector_ << 1U) | (selector_ & 1U));
    ++frame_;
    return true;
}

std::array<std::uint8_t, DemoByteMaskReveal::mask_size>
portable_demo_reveal_mask() noexcept {
    std::array<std::uint8_t, DemoByteMaskReveal::mask_size> result{};
    // The original deliberately sampled volatile IVT/BDA/DOS memory and thus
    // had no machine-independent bitmap. A fixed 16-bit maximal LFSR retains
    // the intended noisy dissolve while making replays and tests deterministic.
    std::uint16_t state = 0x1771;
    for (auto& value : result) {
        std::uint8_t byte = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto outgoing = static_cast<std::uint16_t>(state & 1U);
            state = static_cast<std::uint16_t>(state >> 1U);
            if (outgoing != 0) state ^= 0xb400U;
            byte = static_cast<std::uint8_t>(byte | (outgoing << bit));
        }
        value = byte;
    }
    return result;
}

namespace {

void draw(DemoFrame& frame, DemoSpriteSource source, std::uint8_t sprite,
          int x, int y, bool transparent) {
    frame.draws.push_back({source, sprite, x, y, transparent});
}

}  // namespace

void DemoTimeline::append_sword5(DemoFrame& frame) {
    // 1000:0432. State changes occur before drawing, but only on alternate
    // frames after the procession's x coordinate reaches 100.
    if (!alternate_tick_ && procession_state_ != 8) {
        if (!procession_started_ && procession_x_ <= 100) procession_started_ = true;
        if (procession_started_) ++procession_state_;
    }

    const auto old_x = procession_x_;
    procession_x_ -= 2;
    draw(frame, DemoSpriteSource::sword5, 0, old_x, 48, true);
    draw(frame, DemoSpriteSource::sword5, 3, old_x, 145, false);
    draw(frame, DemoSpriteSource::sword5, 2, old_x + 86, 7, false);
    if (procession_state_ == 0) {
        draw(frame, DemoSpriteSource::sword5, 1, old_x + 24, 0, false);
        return;
    }

    draw(frame, DemoSpriteSource::sword5, 11, old_x + 96, 36, false);
    draw(frame, DemoSpriteSource::sword5, 4, old_x + 24, 0, false);
    if (procession_state_ >= 2 && procession_state_ <= 7) {
        draw(frame, DemoSpriteSource::sword5,
             static_cast<std::uint8_t>(procession_state_ + 3), old_x + 20, 13, false);
    }
}

void DemoTimeline::append_actor(DemoFrame& frame) {
    // 1000:02d3. The final group replaces these backing archives once it
    // appears, so this actor deliberately disappears from later frames.
    if (final_group_started_) return;

    const auto old_x = actor_x_;
    actor_x_ -= 3;
    draw(frame, DemoSpriteSource::sword3, 0, old_x, 162, true);
    const auto upper_x = old_x + 65;
    switch (actor_state_) {
        case 0: draw(frame, DemoSpriteSource::sword3, 1, upper_x, 11, true); break;
        case 1: draw(frame, DemoSpriteSource::sword3, 2, upper_x, 13, true); break;
        case 2: draw(frame, DemoSpriteSource::sword3, 3, upper_x, 16, true); break;
        case 3: draw(frame, DemoSpriteSource::sword4, 0, upper_x, 18, true); break;
        case 4: draw(frame, DemoSpriteSource::sword4, 1, upper_x, 17, true); break;
        default: {
            draw(frame, DemoSpriteSource::sword4, 2, actor_x_ + 132, 162, false);
            const auto body_x = actor_x_ + 67;
            if (actor_state_ == 5) {
                draw(frame, DemoSpriteSource::sword4, 3, body_x, 17, true);
            } else {
                draw(frame, DemoSpriteSource::sword4, 4, body_x, 17, true);
                if (actor_state_ >= 6 && actor_state_ <= 8) {
                    draw(frame, DemoSpriteSource::sword4,
                         static_cast<std::uint8_t>(actor_state_ - 1), body_x + 18, 77, false);
                } else if (actor_state_ == 9 || actor_state_ == 10) {
                    draw(frame, DemoSpriteSource::sword3,
                         static_cast<std::uint8_t>(actor_state_ - 5), body_x + 18, 77, false);
                }
            }
            break;
        }
    }
}

void DemoTimeline::append_final_group(DemoFrame& frame) {
    // 1000:055b subtracts first, making frame 699 the first visible frame.
    final_group_x_ -= 3;
    if (!final_group_started_) {
        if (final_group_x_ > 120) return;
        final_group_started_ = true;
    }
    if (final_group_state_ != 29) ++final_group_state_;

    const auto x = final_group_x_;
    const auto base = DemoSpriteSource::sword6_expanded;
    const auto masked_base = [&](int y) { draw(frame, base, 0, x, y, true); };
    switch (final_group_state_) {
        case 1: masked_base(112); return;
        case 2: masked_base(10); return;
        case 3: masked_base(13); return;
        case 4:
            masked_base(13); draw(frame, base, 1, x + 66, 60, false); return;
        case 5:
            masked_base(17); draw(frame, base, 2, x + 61, 60, false); return;
        case 6:
            masked_base(21); draw(frame, base, 3, x + 62, 60, false); return;
        case 7:
            masked_base(25); draw(frame, base, 4, x + 63, 76, false); return;
        case 8:
            masked_base(25); draw(frame, base, 5, x + 68, 84, false); return;
        case 9:
            masked_base(25); draw(frame, base, 6, x + 85, 67, false); return;
        case 10: case 12: case 18: case 19: case 20: case 21:
            masked_base(25); return;
        case 11:
            masked_base(25); draw(frame, base, 7, x + 68, 71, false); return;
        case 13: case 17:
            masked_base(25); draw(frame, base, 8, x + 62, 100, false); return;
        case 14: case 16:
            masked_base(25); draw(frame, base, 9, x + 62, 97, false); return;
        case 15:
            masked_base(25); draw(frame, base, 10, x + 62, 97, false); return;
        case 22:
            masked_base(25);
            draw(frame, base, 11, x + 63, 100, false);
            draw(frame, base, 12, x + 95, 137, false);
            return;
        case 23:
            masked_base(25);
            draw(frame, base, 13, x + 62, 100, false);
            draw(frame, base, 12, x + 95, 137, false);
            return;
        case 24: case 25: case 26:
            masked_base(25);
            draw(frame, base, 14, x + 62, 100, false);
            draw(frame, base, 12, x + 95, 137, false);
            return;
        default: break;
    }

    // States 27..29 combine the expanded SWORD7 background with the original
    // SWORD2 frames, exactly mirroring the segment swaps in the DOS routine.
    draw(frame, DemoSpriteSource::sword7_expanded, 0, x + 3, 175, false);
    if (final_group_state_ == 27) {
        draw(frame, DemoSpriteSource::sword2, 3, x + 6, 22, true);
        draw(frame, DemoSpriteSource::sword2, 4, x + 42, 115, true);
    } else if (final_group_state_ == 28) {
        draw(frame, DemoSpriteSource::sword2, 5, x - 1, 19, true);
        draw(frame, DemoSpriteSource::sword2, 6, x + 26, 87, true);
    } else {
        draw(frame, DemoSpriteSource::sword7_expanded, 1, x - 6, 18, true);
    }
}

std::optional<DemoFrame> DemoTimeline::next() {
    if (tick_ == frame_count) return std::nullopt;
    DemoFrame frame;
    frame.tick = tick_;
    frame.start_music = tick_ == 200;  // CX reaches 0x2f8; FUN_1000_0bc9 starts RIX.

    draw(frame, DemoSpriteSource::sword1, 0, road_x_[0]--, 53, false);
    draw(frame, DemoSpriteSource::sword1, 1, road_x_[1]--, 57, false);
    draw(frame, DemoSpriteSource::sword1, 2, road_x_[2]--, 57, false);
    draw(frame, DemoSpriteSource::sword2, 1, road_x_[3]--, 41, false);
    draw(frame, DemoSpriteSource::sword2, 2, road_x_[4]--, 79, false);
    append_sword5(frame);
    append_actor(frame);

    alternate_tick_ = !alternate_tick_;
    if (!alternate_tick_ && actor_state_ != 10) {
        if (!actor_started_ && actor_x_ <= 100) actor_started_ = true;
        if (actor_started_) ++actor_state_;
    }
    append_final_group(frame);
    ++tick_;
    return frame;
}

}  // namespace swd2
