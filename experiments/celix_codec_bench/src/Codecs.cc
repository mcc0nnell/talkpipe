#include "TalkPipeCodecBench.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <msgpack.hpp>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>

namespace talkpipe::bench {
namespace {

using Json = nlohmann::json;

constexpr char BASE64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::vector<std::uint8_t>& input) {
    std::string out;
    out.reserve(((input.size() + 2U) / 3U) * 4U);
    for (std::size_t i = 0; i < input.size(); i += 3U) {
        const std::uint32_t a = input[i];
        const std::uint32_t b = i + 1U < input.size() ? input[i + 1U] : 0U;
        const std::uint32_t c = i + 2U < input.size() ? input[i + 2U] : 0U;
        const std::uint32_t triple = (a << 16U) | (b << 8U) | c;
        out.push_back(BASE64_ALPHABET[(triple >> 18U) & 0x3fU]);
        out.push_back(BASE64_ALPHABET[(triple >> 12U) & 0x3fU]);
        out.push_back(i + 1U < input.size() ? BASE64_ALPHABET[(triple >> 6U) & 0x3fU] : '=');
        out.push_back(i + 2U < input.size() ? BASE64_ALPHABET[triple & 0x3fU] : '=');
    }
    return out;
}

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2;
    return -1;
}

std::vector<std::uint8_t> base64Decode(std::string_view input) {
    if (input.size() % 4U != 0U) throw std::runtime_error{"invalid base64 length"};
    std::vector<std::uint8_t> out;
    out.reserve((input.size() / 4U) * 3U);
    for (std::size_t i = 0; i < input.size(); i += 4U) {
        const int a = base64Value(input[i]);
        const int b = base64Value(input[i + 1U]);
        const int c = base64Value(input[i + 2U]);
        const int d = base64Value(input[i + 3U]);
        if (a < 0 || b < 0 || c == -1 || d == -1) throw std::runtime_error{"invalid base64"};
        const std::uint32_t triple = (static_cast<std::uint32_t>(a) << 18U) |
                                     (static_cast<std::uint32_t>(b) << 12U) |
                                     (static_cast<std::uint32_t>(std::max(c, 0)) << 6U) |
                                     static_cast<std::uint32_t>(std::max(d, 0));
        out.push_back(static_cast<std::uint8_t>((triple >> 16U) & 0xffU));
        if (c != -2) out.push_back(static_cast<std::uint8_t>((triple >> 8U) & 0xffU));
        if (d != -2) out.push_back(static_cast<std::uint8_t>(triple & 0xffU));
    }
    return out;
}

Json operationToJson(const Operation& operation) {
    return std::visit([](const auto& op) -> Json {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, Fetch>) {
            return Json{{"op", "fetch"}, {"id", op.id}, {"lane", op.lane},
                        {"retry", op.retry}, {"ok", op.ok}};
        } else if constexpr (std::is_same_v<T, Judge>) {
            return Json{{"op", "judge"}, {"id", op.id},
                        {"deps", op.dependencies}, {"ok", op.ok}};
        } else {
            return Json{{"op", "emit"}, {"id", op.id}, {"path", op.path}};
        }
    }, operation);
}

Operation operationFromJson(const Json& value) {
    const auto op = value.at("op").get<std::string>();
    if (op == "fetch") {
        return Fetch{value.at("id").get<std::uint32_t>(),
                     value.at("lane").get<std::uint8_t>(),
                     value.at("retry").get<std::uint8_t>(),
                     value.at("ok").get<bool>()};
    }
    if (op == "judge") {
        return Judge{value.at("id").get<std::uint32_t>(),
                     value.at("deps").get<std::array<std::uint32_t, 3>>(),
                     value.at("ok").get<bool>()};
    }
    if (op == "emit") {
        return Emit{value.at("id").get<std::uint32_t>(), value.at("path").get<std::string>()};
    }
    throw std::runtime_error{"unknown JSON operation"};
}

std::vector<std::uint8_t> encodeJson(const Plan& plan) {
    Json value;
    value["v"] = plan.version;
    value["ops"] = Json::array();
    for (const auto& operation : plan.operations) value["ops"].push_back(operationToJson(operation));
    if (!plan.evidence.empty()) value["evidence_b64"] = base64Encode(plan.evidence);
    const auto text = value.dump();
    return {text.begin(), text.end()};
}

Plan decodeJson(const std::vector<std::uint8_t>& payload) {
    const auto value = Json::parse(payload.begin(), payload.end());
    Plan plan;
    plan.version = value.at("v").get<std::uint32_t>();
    for (const auto& operation : value.at("ops")) plan.operations.push_back(operationFromJson(operation));
    if (value.contains("evidence_b64")) {
        plan.evidence = base64Decode(value.at("evidence_b64").get<std::string>());
    }
    return plan;
}

void packString(msgpack::packer<msgpack::sbuffer>& packer, std::string_view value) {
    packer.pack_str(static_cast<std::uint32_t>(value.size()));
    packer.pack_str_body(value.data(), static_cast<std::uint32_t>(value.size()));
}

void packBinary(msgpack::packer<msgpack::sbuffer>& packer, const std::vector<std::uint8_t>& value) {
    packer.pack_bin(static_cast<std::uint32_t>(value.size()));
    if (!value.empty()) {
        packer.pack_bin_body(reinterpret_cast<const char*>(value.data()),
                             static_cast<std::uint32_t>(value.size()));
    }
}

void packMapOperation(msgpack::packer<msgpack::sbuffer>& packer, const Operation& operation) {
    std::visit([&packer](const auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, Fetch>) {
            packer.pack_map(5);
            packString(packer, "op"); packString(packer, "fetch");
            packString(packer, "id"); packer.pack(op.id);
            packString(packer, "lane"); packer.pack(op.lane);
            packString(packer, "retry"); packer.pack(op.retry);
            packString(packer, "ok"); packer.pack(op.ok);
        } else if constexpr (std::is_same_v<T, Judge>) {
            packer.pack_map(4);
            packString(packer, "op"); packString(packer, "judge");
            packString(packer, "id"); packer.pack(op.id);
            packString(packer, "deps");
            packer.pack_array(3);
            for (auto dep : op.dependencies) packer.pack(dep);
            packString(packer, "ok"); packer.pack(op.ok);
        } else {
            packer.pack_map(3);
            packString(packer, "op"); packString(packer, "emit");
            packString(packer, "id"); packer.pack(op.id);
            packString(packer, "path"); packString(packer, op.path);
        }
    }, operation);
}

void packPositionalOperation(msgpack::packer<msgpack::sbuffer>& packer, const Operation& operation) {
    std::visit([&packer](const auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, Fetch>) {
            packer.pack_array(5);
            packer.pack(1); packer.pack(op.id); packer.pack(op.lane); packer.pack(op.retry); packer.pack(op.ok);
        } else if constexpr (std::is_same_v<T, Judge>) {
            packer.pack_array(4);
            packer.pack(2); packer.pack(op.id);
            packer.pack_array(3);
            for (auto dep : op.dependencies) packer.pack(dep);
            packer.pack(op.ok);
        } else {
            packer.pack_array(3);
            packer.pack(3); packer.pack(op.id); packString(packer, op.path);
        }
    }, operation);
}

std::vector<std::uint8_t> encodeMessagePack(const Plan& plan, bool positional) {
    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> packer{buffer};
    if (positional) {
        packer.pack_array(plan.evidence.empty() ? 2U : 3U);
        packer.pack(plan.version);
        packer.pack_array(static_cast<std::uint32_t>(plan.operations.size()));
        for (const auto& operation : plan.operations) packPositionalOperation(packer, operation);
        if (!plan.evidence.empty()) packBinary(packer, plan.evidence);
    } else {
        packer.pack_map(plan.evidence.empty() ? 2U : 3U);
        packString(packer, "v"); packer.pack(plan.version);
        packString(packer, "ops");
        packer.pack_array(static_cast<std::uint32_t>(plan.operations.size()));
        for (const auto& operation : plan.operations) packMapOperation(packer, operation);
        if (!plan.evidence.empty()) {
            packString(packer, "evidence"); packBinary(packer, plan.evidence);
        }
    }
    const auto* first = reinterpret_cast<const std::uint8_t*>(buffer.data());
    return {first, first + buffer.size()};
}

std::string objectString(const msgpack::object& object) {
    if (object.type != msgpack::type::STR) throw std::runtime_error{"expected MessagePack string"};
    return {object.via.str.ptr, object.via.str.size};
}

const msgpack::object& mapValue(const msgpack::object& object, std::string_view key) {
    if (object.type != msgpack::type::MAP) throw std::runtime_error{"expected MessagePack map"};
    for (std::uint32_t i = 0; i < object.via.map.size; ++i) {
        const auto& entry = object.via.map.ptr[i];
        if (entry.key.type == msgpack::type::STR &&
            std::string_view{entry.key.via.str.ptr, entry.key.via.str.size} == key) {
            return entry.val;
        }
    }
    throw std::runtime_error{"missing MessagePack map key: " + std::string{key}};
}

bool mapHas(const msgpack::object& object, std::string_view key) {
    if (object.type != msgpack::type::MAP) return false;
    for (std::uint32_t i = 0; i < object.via.map.size; ++i) {
        const auto& entry = object.via.map.ptr[i];
        if (entry.key.type == msgpack::type::STR &&
            std::string_view{entry.key.via.str.ptr, entry.key.via.str.size} == key) return true;
    }
    return false;
}

std::vector<std::uint8_t> objectBinary(const msgpack::object& object) {
    if (object.type != msgpack::type::BIN) throw std::runtime_error{"expected MessagePack binary"};
    const auto* first = reinterpret_cast<const std::uint8_t*>(object.via.bin.ptr);
    return {first, first + object.via.bin.size};
}

Operation decodeMapOperation(const msgpack::object& object) {
    const auto type = objectString(mapValue(object, "op"));
    const auto id = mapValue(object, "id").as<std::uint32_t>();
    if (type == "fetch") {
        return Fetch{id,
                     static_cast<std::uint8_t>(mapValue(object, "lane").as<unsigned int>()),
                     static_cast<std::uint8_t>(mapValue(object, "retry").as<unsigned int>()),
                     mapValue(object, "ok").as<bool>()};
    }
    if (type == "judge") {
        const auto& depsObj = mapValue(object, "deps");
        if (depsObj.type != msgpack::type::ARRAY || depsObj.via.array.size != 3U) {
            throw std::runtime_error{"invalid judge dependencies"};
        }
        std::array<std::uint32_t, 3> deps{};
        for (std::uint32_t i = 0; i < 3U; ++i) deps[i] = depsObj.via.array.ptr[i].as<std::uint32_t>();
        return Judge{id, deps, mapValue(object, "ok").as<bool>()};
    }
    if (type == "emit") return Emit{id, objectString(mapValue(object, "path"))};
    throw std::runtime_error{"unknown MessagePack map operation"};
}

Operation decodePositionalOperation(const msgpack::object& object) {
    if (object.type != msgpack::type::ARRAY || object.via.array.size < 3U) {
        throw std::runtime_error{"invalid positional operation"};
    }
    const auto* fields = object.via.array.ptr;
    const auto type = fields[0].as<int>();
    const auto id = fields[1].as<std::uint32_t>();
    if (type == 1 && object.via.array.size == 5U) {
        return Fetch{id,
                     static_cast<std::uint8_t>(fields[2].as<unsigned int>()),
                     static_cast<std::uint8_t>(fields[3].as<unsigned int>()),
                     fields[4].as<bool>()};
    }
    if (type == 2 && object.via.array.size == 4U) {
        const auto& depsObj = fields[2];
        if (depsObj.type != msgpack::type::ARRAY || depsObj.via.array.size != 3U) {
            throw std::runtime_error{"invalid positional judge dependencies"};
        }
        std::array<std::uint32_t, 3> deps{};
        for (std::uint32_t i = 0; i < 3U; ++i) deps[i] = depsObj.via.array.ptr[i].as<std::uint32_t>();
        return Judge{id, deps, fields[3].as<bool>()};
    }
    if (type == 3 && object.via.array.size == 3U) return Emit{id, objectString(fields[2])};
    throw std::runtime_error{"unknown positional operation"};
}

Plan decodeMessagePack(const std::vector<std::uint8_t>& payload, bool positional) {
    const auto unpacked = msgpack::unpack(reinterpret_cast<const char*>(payload.data()), payload.size());
    const auto& root = unpacked.get();
    Plan plan;
    if (positional) {
        if (root.type != msgpack::type::ARRAY || (root.via.array.size != 2U && root.via.array.size != 3U)) {
            throw std::runtime_error{"invalid positional plan"};
        }
        plan.version = root.via.array.ptr[0].as<std::uint32_t>();
        const auto& operations = root.via.array.ptr[1];
        if (operations.type != msgpack::type::ARRAY) throw std::runtime_error{"invalid positional operations"};
        plan.operations.reserve(operations.via.array.size);
        for (std::uint32_t i = 0; i < operations.via.array.size; ++i) {
            plan.operations.push_back(decodePositionalOperation(operations.via.array.ptr[i]));
        }
        if (root.via.array.size == 3U) plan.evidence = objectBinary(root.via.array.ptr[2]);
    } else {
        plan.version = mapValue(root, "v").as<std::uint32_t>();
        const auto& operations = mapValue(root, "ops");
        if (operations.type != msgpack::type::ARRAY) throw std::runtime_error{"invalid map operations"};
        plan.operations.reserve(operations.via.array.size);
        for (std::uint32_t i = 0; i < operations.via.array.size; ++i) {
            plan.operations.push_back(decodeMapOperation(operations.via.array.ptr[i]));
        }
        if (mapHas(root, "evidence")) plan.evidence = objectBinary(mapValue(root, "evidence"));
    }
    return plan;
}

} // namespace

bool operator==(const Fetch& lhs, const Fetch& rhs) noexcept {
    return lhs.id == rhs.id && lhs.lane == rhs.lane && lhs.retry == rhs.retry && lhs.ok == rhs.ok;
}

bool operator==(const Judge& lhs, const Judge& rhs) noexcept {
    return lhs.id == rhs.id && lhs.dependencies == rhs.dependencies && lhs.ok == rhs.ok;
}

bool operator==(const Emit& lhs, const Emit& rhs) noexcept {
    return lhs.id == rhs.id && lhs.path == rhs.path;
}

bool operator==(const Plan& lhs, const Plan& rhs) noexcept {
    return lhs.version == rhs.version && lhs.operations == rhs.operations && lhs.evidence == rhs.evidence;
}

std::vector<std::uint8_t> encode(const Plan& plan, Codec codec) {
    switch (codec) {
        case Codec::Json: return encodeJson(plan);
        case Codec::MessagePackMap: return encodeMessagePack(plan, false);
        case Codec::MessagePackPositional: return encodeMessagePack(plan, true);
    }
    throw std::runtime_error{"unknown codec"};
}

Plan decode(const std::vector<std::uint8_t>& payload, Codec codec) {
    switch (codec) {
        case Codec::Json: return decodeJson(payload);
        case Codec::MessagePackMap: return decodeMessagePack(payload, false);
        case Codec::MessagePackPositional: return decodeMessagePack(payload, true);
    }
    throw std::runtime_error{"unknown codec"};
}

std::string semanticSha256(const Plan& plan) {
    const auto canonical = encodeMessagePack(plan, true);
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(canonical.data(), canonical.size(), digest.data());
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : digest) out << std::setw(2) << static_cast<unsigned int>(byte);
    return out.str();
}

const char* codecName(Codec codec) noexcept {
    switch (codec) {
        case Codec::Json: return "json";
        case Codec::MessagePackMap: return "msgpack-map";
        case Codec::MessagePackPositional: return "msgpack-positional";
    }
    return "unknown";
}

Plan makePlanWorkload() {
    Plan plan;
    plan.operations.reserve(300);
    for (std::uint32_t i = 0; i < 100; ++i) {
        plan.operations.emplace_back(Fetch{i, static_cast<std::uint8_t>(i % 4U),
                                           static_cast<std::uint8_t>(i % 3U), true});
        plan.operations.emplace_back(Judge{i, {i, i + 1U, i + 2U}, i % 2U == 0U});
        std::ostringstream path;
        path << "artifact/" << std::setw(3) << std::setfill('0') << i;
        plan.operations.emplace_back(Emit{i, path.str()});
    }
    return plan;
}

Plan makeBinaryWorkload() {
    Plan plan;
    plan.operations.emplace_back(Fetch{17, 1, 2, true});
    plan.evidence.resize(65'536);
    for (std::size_t i = 0; i < plan.evidence.size(); ++i) {
        plan.evidence[i] = static_cast<std::uint8_t>((i * 31U + 7U) & 0xffU);
    }
    return plan;
}

} // namespace talkpipe::bench
