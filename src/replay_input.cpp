#include "swd2/replay_input.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>

namespace swd2 {

namespace {

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::toupper(character));
                   });
    return value;
}

ReplayInputBoundary parse_boundary(const std::string& value) {
    if (value == "ANY") return ReplayInputBoundary::any;
    if (value == "WAIT") return ReplayInputBoundary::wait;
    if (value == "POLL") return ReplayInputBoundary::poll;
    if (value == "TEXT") return ReplayInputBoundary::text;
    if (value == "FRONTEND") return ReplayInputBoundary::frontend;
    throw std::runtime_error("unknown replay input boundary: " + value);
}

InputAction parse_action(const std::string& value) {
    if (value == "UP") return InputAction::up;
    if (value == "DOWN") return InputAction::down;
    if (value == "LEFT") return InputAction::left;
    if (value == "RIGHT") return InputAction::right;
    if (value == "PGUP" || value == "PAGEUP") return InputAction::page_up;
    if (value == "PGDN" || value == "PAGEDOWN") return InputAction::page_down;
    if (value == "HOME") return InputAction::home;
    if (value == "END") return InputAction::end;
    if (value == "ERASE" || value == "CLEAR" || value == "INSERT" ||
        value == "CTRL") {
        return InputAction::erase;
    }
    if (value == "OK" || value == "CONFIRM" || value == "ENTER") {
        return InputAction::confirm;
    }
    if (value == "CANCEL" || value == "ESC" || value == "ESCAPE") {
        return InputAction::cancel;
    }
    if (value == "TICK" || value == "NONE") return InputAction::none;
    if (value == "QUIT") return InputAction::quit;
    throw std::runtime_error("unknown replay input action: " + value);
}

void append_token(std::vector<ReplayInputStep>& result, std::string token) {
    token = upper(std::move(token));
    auto count = std::uint64_t{1};
    if (const auto star = token.rfind('*'); star != std::string::npos) {
        const auto count_text = token.substr(star + 1U);
        token.resize(star);
        if (count_text.empty() ||
            !std::all_of(count_text.begin(), count_text.end(),
                         [](unsigned char character) {
                             return std::isdigit(character) != 0;
                         })) {
            throw std::runtime_error("invalid replay repeat count");
        }
        count = std::stoull(count_text);
        if (count == 0 || count > 1'000'000U) {
            throw std::runtime_error("replay repeat count must be 1..1000000");
        }
    }
    if (token.empty()) throw std::runtime_error("empty replay input token");

    auto boundary = ReplayInputBoundary::any;
    if (const auto colon = token.find(':'); colon != std::string::npos) {
        if (token.find(':', colon + 1U) != std::string::npos) {
            throw std::runtime_error("replay input token has multiple boundaries");
        }
        boundary = parse_boundary(token.substr(0, colon));
        token.erase(0, colon + 1U);
    }
    const auto action = parse_action(token);
    if (boundary == ReplayInputBoundary::frontend &&
        action != InputAction::none && action != InputAction::quit) {
        throw std::runtime_error("FRONTEND replay steps accept only NONE or QUIT");
    }
    if (count > std::numeric_limits<std::size_t>::max() - result.size()) {
        throw std::runtime_error("replay input is too large");
    }
    if (result.size() + count > 1'000'000U) {
        throw std::runtime_error("replay input exceeds one million actions");
    }
    result.insert(result.end(), static_cast<std::size_t>(count),
                  ReplayInputStep{boundary, action});
}

}  // namespace

std::vector<ReplayInputStep> parse_replay_input(std::string_view text) {
    std::vector<ReplayInputStep> result;
    std::string token;
    auto comment = false;
    const auto flush = [&] {
        if (token.empty()) return;
        append_token(result, std::move(token));
        token.clear();
    };
    for (const auto raw : text) {
        const auto character = static_cast<unsigned char>(raw);
        if (comment) {
            if (character == '\n' || character == '\r') comment = false;
            continue;
        }
        if (character == '#') {
            flush();
            comment = true;
        } else if (character == ',' || std::isspace(character) != 0) {
            flush();
        } else {
            token.push_back(static_cast<char>(character));
        }
    }
    flush();
    if (result.empty()) throw std::runtime_error("empty replay input");
    return result;
}

std::string_view replay_boundary_name(ReplayInputBoundary boundary) noexcept {
    switch (boundary) {
    case ReplayInputBoundary::any: return "ANY";
    case ReplayInputBoundary::wait: return "WAIT";
    case ReplayInputBoundary::poll: return "POLL";
    case ReplayInputBoundary::text: return "TEXT";
    case ReplayInputBoundary::frontend: return "FRONTEND";
    }
    return "UNKNOWN";
}

std::string_view input_action_name(InputAction action) noexcept {
    switch (action) {
    case InputAction::none: return "NONE";
    case InputAction::up: return "UP";
    case InputAction::down: return "DOWN";
    case InputAction::left: return "LEFT";
    case InputAction::right: return "RIGHT";
    case InputAction::page_up: return "PAGEUP";
    case InputAction::page_down: return "PAGEDOWN";
    case InputAction::home: return "HOME";
    case InputAction::end: return "END";
    case InputAction::confirm: return "CONFIRM";
    case InputAction::cancel: return "CANCEL";
    case InputAction::quit: return "QUIT";
    case InputAction::erase: return "ERASE";
    }
    return "UNKNOWN";
}

}  // namespace swd2
