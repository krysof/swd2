#include "swd2/meo_module.hpp"

#include "swd2/meo.hpp"
#include "swd2/mz_executable.hpp"
#include "swd2/rsk_decoder.hpp"
#include "swd2/sprite_archive.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace swd2 {

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("MEO module cannot open " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

Marker MeoModule::run(GameContext& context, Marker) {
    auto decoded = decode_rsk_block(read_file(context.game_root / "MEO.RSK"));
    const auto archive = SpriteArchive::parse(std::move(decoded.data));
    const auto executable_path = context.game_root / "MEO.EXE";
    const auto executable = dos::MzExecutable::load(executable_path);
    const auto executable_bytes = read_file(executable_path);
    const auto image_start = static_cast<std::size_t>(executable.header_size());
    const auto image_size = static_cast<std::size_t>(executable.load_image_size());
    if (image_start > executable_bytes.size() ||
        image_size > executable_bytes.size() - image_start) {
        throw std::runtime_error("MEO executable load image is truncated");
    }
    MeoCopyProtection protection(meo_copy_protection_is_patched(
        std::span<const std::uint8_t>(executable_bytes)
            .subspan(image_start, image_size)));

    const auto fade_to_black = [&](IndexedFrame frame) {
        // MEO:276 decrements every nonzero DAC component by exactly one,
        // waits for one IRQ-0 tick, writes all 768 bytes, and repeats until
        // the palette is black.  Keep host-close separate from DOS keys so a
        // direction queued for RPG's title cannot disappear in this handoff.
        std::uint64_t ticks = 0U;
        while (std::any_of(frame.palette.begin(), frame.palette.end(),
                           [](std::uint8_t value) { return value != 0U; })) {
            if (context.platform.poll_frontend_quit()) return false;
            for (auto& component : frame.palette) {
                if (component != 0U) --component;
            }
            context.platform.present({
                IndexedFrame::width, IndexedFrame::height, frame.pixels,
                std::span<const std::uint8_t, 768>(frame.palette)});
            const auto before = ticks * 1000U / 70U;
            const auto after = ++ticks * 1000U / 70U;
            context.platform.delay_for(
                std::chrono::milliseconds(after - before));
        }
        return true;
    };

    while (true) {
        const auto time = context.platform.clock_time();
        const auto frame = render_meo_frame(archive, protection.choice(), time.minute, time.second);
        context.platform.present({IndexedFrame::width, IndexedFrame::height, frame.pixels,
                                  std::span<const std::uint8_t, 768>(frame.palette)});
        const auto action = context.platform.wait_for_input();
        if (action == InputAction::quit || action == InputAction::cancel) {
            return Marker::none;
        }

        MeoInput meo_input;
        if (action == InputAction::up) {
            meo_input = MeoInput::up;
        } else if (action == InputAction::down) {
            meo_input = MeoInput::down;
        } else if (action == InputAction::confirm) {
            meo_input = MeoInput::confirm;
        } else {
            continue;
        }

        const auto expected = meo_expected_color(frame, time.minute, time.second);
        const auto status = protection.input(meo_input, expected);
        if (status == MeoStatus::accepted) {
            if (!fade_to_black(frame)) return Marker::none;
            return Marker::menu_ready;
        }
        if (status == MeoStatus::rejected) {
            const auto rejected = render_meo_frame(archive, protection.choice(), time.minute,
                                                   time.second, true);
            context.platform.present({IndexedFrame::width, IndexedFrame::height, rejected.pixels,
                                      std::span<const std::uint8_t, 768>(rejected.palette)});
            if (!fade_to_black(rejected)) return Marker::none;
            return Marker::menu_rejected;
        }
    }
}

}  // namespace swd2
