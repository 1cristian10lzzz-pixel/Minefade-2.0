#pragma once

#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace minefade {

struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Storage value{nullptr};

    const JsonValue* Find(std::string_view key) const noexcept;
    const std::string* String() const noexcept;
    const Array* Items() const noexcept;
    const Object* Members() const noexcept;
    bool Number(double& output) const noexcept;
    bool Boolean(bool& output) const noexcept;
};

bool ParseJson(std::string_view source, JsonValue& output, std::string& error);

} // namespace minefade
