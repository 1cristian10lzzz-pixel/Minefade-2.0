#pragma once

#include <cstdint>

namespace minefade {

enum class LegacyModelMode : std::uint8_t {
    Legacy = 0,
    Maes = 1,
    Auto = 2
};

class LegacyModelAdapter {
public:
    explicit LegacyModelAdapter(LegacyModelMode mode = LegacyModelMode::Auto)
        : mode_(mode) {}

    void SetMode(LegacyModelMode mode) { mode_ = mode; }
    LegacyModelMode Mode() const { return mode_; }
    bool UseMaes(bool modelValidated) const;

private:
    LegacyModelMode mode_;
};

} // namespace minefade
