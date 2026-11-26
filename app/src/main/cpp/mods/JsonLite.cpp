#include "JsonLite.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace minefade {
namespace {

void AppendUtf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7fu) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffu) {
        output.push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint <= 0xffffu) {
        output.push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else {
        output.push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view source) : source_(source) {}

    bool Parse(JsonValue& output, std::string& error) {
        SkipWhitespace();
        if (!ParseValue(output, 0u)) {
            error = error_.empty() ? "invalid JSON value" : error_;
            return false;
        }
        SkipWhitespace();
        if (position_ != source_.size()) {
            error = "unexpected trailing JSON data";
            return false;
        }
        error.clear();
        return true;
    }

private:
    void Fail(const char* reason) {
        if (error_.empty()) error_ = reason;
    }

    void SkipWhitespace() {
        while (position_ < source_.size()) {
            const char c = source_[position_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++position_;
        }
    }

    bool ParseValue(JsonValue& output, unsigned depth) {
        if (depth > 32u || position_ >= source_.size()) {
            Fail("JSON nesting or size limit exceeded");
            return false;
        }
        SkipWhitespace();
        if (position_ >= source_.size()) {
            Fail("unexpected end of JSON");
            return false;
        }
        const char c = source_[position_];
        if (c == '{') return ParseObject(output, depth + 1u);
        if (c == '[') return ParseArray(output, depth + 1u);
        if (c == '"') {
            std::string value;
            if (!ParseString(value)) return false;
            output.value = std::move(value);
            return true;
        }
        if (c == 't' && ConsumeLiteral("true")) { output.value = true; return true; }
        if (c == 'f' && ConsumeLiteral("false")) { output.value = false; return true; }
        if (c == 'n' && ConsumeLiteral("null")) { output.value = nullptr; return true; }
        return ParseNumber(output);
    }

    bool ConsumeLiteral(std::string_view literal) {
        if (source_.substr(position_, literal.size()) != literal) return false;
        position_ += literal.size();
        return true;
    }

    bool ReadHex4(std::uint32_t& value) {
        if (source_.size() - position_ < 4u) return false;
        value = 0u;
        for (int i = 0; i < 4; ++i) {
            const char c = source_[position_++];
            value <<= 4u;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool ParseString(std::string& output) {
        if (position_ >= source_.size() || source_[position_] != '"') return false;
        ++position_;
        output.clear();
        while (position_ < source_.size()) {
            const unsigned char c = static_cast<unsigned char>(source_[position_++]);
            if (c == '"') return output.size() <= 65536u;
            if (c < 0x20u) {
                Fail("control character in JSON string");
                return false;
            }
            if (c != '\\') {
                output.push_back(static_cast<char>(c));
            } else {
                if (position_ >= source_.size()) return false;
                const char escape = source_[position_++];
                switch (escape) {
                    case '"': output.push_back('"'); break;
                    case '\\': output.push_back('\\'); break;
                    case '/': output.push_back('/'); break;
                    case 'b': output.push_back('\b'); break;
                    case 'f': output.push_back('\f'); break;
                    case 'n': output.push_back('\n'); break;
                    case 'r': output.push_back('\r'); break;
                    case 't': output.push_back('\t'); break;
                    case 'u': {
                        std::uint32_t first = 0u;
                        if (!ReadHex4(first)) { Fail("invalid Unicode escape"); return false; }
                        std::uint32_t codepoint = first;
                        if (first >= 0xd800u && first <= 0xdbffu) {
                            if (source_.size() - position_ < 6u ||
                                source_[position_] != '\\' || source_[position_ + 1u] != 'u') {
                                Fail("unpaired Unicode surrogate");
                                return false;
                            }
                            position_ += 2u;
                            std::uint32_t second = 0u;
                            if (!ReadHex4(second) || second < 0xdc00u || second > 0xdfffu) {
                                Fail("invalid Unicode surrogate");
                                return false;
                            }
                            codepoint = 0x10000u + ((first - 0xd800u) << 10u) + (second - 0xdc00u);
                        } else if (first >= 0xdc00u && first <= 0xdfffu) {
                            Fail("unpaired Unicode surrogate");
                            return false;
                        }
                        AppendUtf8(output, codepoint);
                        break;
                    }
                    default: Fail("unknown JSON escape"); return false;
                }
            }
            if (output.size() > 65536u) { Fail("JSON string too long"); return false; }
        }
        Fail("unterminated JSON string");
        return false;
    }

    bool ParseNumber(JsonValue& output) {
        const std::size_t start = position_;
        if (position_ < source_.size() && source_[position_] == '-') ++position_;
        if (position_ >= source_.size()) return false;
        if (source_[position_] == '0') {
            ++position_;
        } else {
            if (source_[position_] < '1' || source_[position_] > '9') return false;
            while (position_ < source_.size() &&
                   source_[position_] >= '0' && source_[position_] <= '9') ++position_;
        }
        if (position_ < source_.size() && source_[position_] == '.') {
            ++position_;
            const std::size_t digits = position_;
            while (position_ < source_.size() &&
                   source_[position_] >= '0' && source_[position_] <= '9') ++position_;
            if (digits == position_) return false;
        }
        if (position_ < source_.size() && (source_[position_] == 'e' || source_[position_] == 'E')) {
            ++position_;
            if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-')) ++position_;
            const std::size_t digits = position_;
            while (position_ < source_.size() &&
                   source_[position_] >= '0' && source_[position_] <= '9') ++position_;
            if (digits == position_) return false;
        }
        const std::string number(source_.substr(start, position_ - start));
        char* end = nullptr;
        const double parsed = std::strtod(number.c_str(), &end);
        if (end == number.c_str() || *end != '\0' || !std::isfinite(parsed)) {
            Fail("non-finite or invalid JSON number");
            return false;
        }
        output.value = parsed;
        return true;
    }

    bool ParseObject(JsonValue& output, unsigned depth) {
        ++position_;
        JsonValue::Object object;
        SkipWhitespace();
        if (position_ < source_.size() && source_[position_] == '}') {
            ++position_;
            output.value = std::move(object);
            return true;
        }
        while (position_ < source_.size()) {
            SkipWhitespace();
            std::string key;
            if (!ParseString(key)) { Fail("object key must be a string"); return false; }
            SkipWhitespace();
            if (position_ >= source_.size() || source_[position_++] != ':') {
                Fail("missing colon after object key");
                return false;
            }
            JsonValue value;
            if (!ParseValue(value, depth)) return false;
            if (!object.emplace(std::move(key), std::move(value)).second) {
                Fail("duplicate JSON object key");
                return false;
            }
            SkipWhitespace();
            if (position_ >= source_.size()) break;
            const char separator = source_[position_++];
            if (separator == '}') {
                output.value = std::move(object);
                return true;
            }
            if (separator != ',') { Fail("invalid object separator"); return false; }
        }
        Fail("unterminated JSON object");
        return false;
    }

    bool ParseArray(JsonValue& output, unsigned depth) {
        ++position_;
        JsonValue::Array array;
        SkipWhitespace();
        if (position_ < source_.size() && source_[position_] == ']') {
            ++position_;
            output.value = std::move(array);
            return true;
        }
        while (position_ < source_.size()) {
            if (array.size() >= 16384u) { Fail("JSON array too large"); return false; }
            JsonValue value;
            if (!ParseValue(value, depth)) return false;
            array.push_back(std::move(value));
            SkipWhitespace();
            if (position_ >= source_.size()) break;
            const char separator = source_[position_++];
            if (separator == ']') {
                output.value = std::move(array);
                return true;
            }
            if (separator != ',') { Fail("invalid array separator"); return false; }
        }
        Fail("unterminated JSON array");
        return false;
    }

    std::string_view source_;
    std::size_t position_{0u};
    std::string error_;
};

} // namespace

const JsonValue* JsonValue::Find(std::string_view key) const noexcept {
    const auto* object = std::get_if<Object>(&value);
    if (object == nullptr) return nullptr;
    const auto found = object->find(std::string(key));
    return found == object->end() ? nullptr : &found->second;
}
const std::string* JsonValue::String() const noexcept { return std::get_if<std::string>(&value); }
const JsonValue::Array* JsonValue::Items() const noexcept { return std::get_if<Array>(&value); }
const JsonValue::Object* JsonValue::Members() const noexcept { return std::get_if<Object>(&value); }
bool JsonValue::Number(double& output) const noexcept {
    const auto* number = std::get_if<double>(&value);
    if (number == nullptr) return false;
    output = *number;
    return true;
}
bool JsonValue::Boolean(bool& output) const noexcept {
    const auto* boolean = std::get_if<bool>(&value);
    if (boolean == nullptr) return false;
    output = *boolean;
    return true;
}

bool ParseJson(std::string_view source, JsonValue& output, std::string& error) {
    if (source.empty() || source.size() > 1024u * 1024u) {
        error = "JSON source is empty or exceeds 1 MiB";
        return false;
    }
    Parser parser(source);
    return parser.Parse(output, error);
}

} // namespace minefade
