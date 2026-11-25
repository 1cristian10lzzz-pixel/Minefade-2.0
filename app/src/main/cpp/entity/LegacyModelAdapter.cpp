#include "LegacyModelAdapter.h"

namespace minefade {
bool LegacyModelAdapter::UseMaes(bool modelValidated) const {
    switch (mode_) {
        case LegacyModelMode::Legacy: return false;
        case LegacyModelMode::Maes: return modelValidated;
        case LegacyModelMode::Auto: return modelValidated;
    }
    return false;
}
} // namespace minefade
