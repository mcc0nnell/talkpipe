#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace talkpipe::bench {

enum class Codec : std::uint8_t {
    Json = 1,
    MessagePackMap = 2,
    MessagePackPositional = 3,
};

struct Fetch {
    std::uint32_t id{};
    std::uint8_t lane{};
    std::uint8_t retry{};
    bool ok{};
};

struct Judge {
    std::uint32_t id{};
    std::array<std::uint32_t, 3> dependencies{};
    bool ok{};
};

struct Emit {
    std::uint32_t id{};
    std::string path{};
};

using Operation = std::variant<Fetch, Judge, Emit>;

struct Plan {
    std::uint32_t version{1};
    std::vector<Operation> operations{};
    std::vector<std::uint8_t> evidence{};
};

struct WireResult {
    std::vector<std::uint8_t> payload{};
    std::string semanticSha256{};
};

class ITalkPipeWireService {
public:
    virtual ~ITalkPipeWireService() noexcept = default;
    virtual WireResult roundTrip(Codec codec, const std::vector<std::uint8_t>& payload) = 0;
};

bool operator==(const Fetch& lhs, const Fetch& rhs) noexcept;
bool operator==(const Judge& lhs, const Judge& rhs) noexcept;
bool operator==(const Emit& lhs, const Emit& rhs) noexcept;
bool operator==(const Plan& lhs, const Plan& rhs) noexcept;

std::vector<std::uint8_t> encode(const Plan& plan, Codec codec);
Plan decode(const std::vector<std::uint8_t>& payload, Codec codec);
std::string semanticSha256(const Plan& plan);
const char* codecName(Codec codec) noexcept;

Plan makePlanWorkload();
Plan makeBinaryWorkload();

} // namespace talkpipe::bench
