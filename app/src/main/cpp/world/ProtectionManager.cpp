#include "ProtectionManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace minefade {

bool ProtectionManager::ValidIdentity(const std::string& identity) {
    if (identity.size() < 2u || identity.size() > 64u) return false;
    for (const unsigned char c : identity) {
        const bool alphaNumeric = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9');
        if (!alphaNumeric && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

bool ProtectionManager::IsInside(const Claim& claim, int x, int z) {
    return x >= claim.minX && x <= claim.maxX &&
           z >= claim.minZ && z <= claim.maxZ;
}

bool ProtectionManager::Overlaps(const Claim& a, const Claim& b) {
    return a.minX <= b.maxX && a.maxX >= b.minX &&
           a.minZ <= b.maxZ && a.maxZ >= b.minZ;
}

ProtectionManager::Claim* ProtectionManager::FindClaimForOwner(const std::string& owner) {
    for (Claim& claim : claims_) if (claim.owner == owner) return &claim;
    return nullptr;
}

const ProtectionManager::Claim* ProtectionManager::FindClaimAt(int x, int z) const {
    for (const Claim& claim : claims_) if (IsInside(claim, x, z)) return &claim;
    return nullptr;
}

bool ProtectionManager::Load(const std::string& sidecarPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    sidecarPath_ = sidecarPath;
    claims_.clear();
    nextClaimId_ = 1;
    loaded_ = false;
    damaged_ = false;

    if (sidecarPath_.empty()) return false;
    std::error_code error;
    if (!std::filesystem::exists(sidecarPath_, error)) {
        if (error) return false;
        loaded_ = true;
        return true;
    }
    const std::uintmax_t bytes = std::filesystem::file_size(sidecarPath_, error);
    if (error || bytes == 0u || bytes > 32768u) {
        damaged_ = true;
        return false;
    }

    std::ifstream input(sidecarPath_, std::ios::binary);
    if (!input) return false;
    std::string line;
    if (!std::getline(input, line) || line != "MFCLAIMS1") {
        damaged_ = true;
        return false;
    }

    std::vector<Claim> loaded;
    std::size_t lineCount = 0u;
    while (std::getline(input, line)) {
        if (++lineCount > 640u || line.size() > 256u) {
            damaged_ = true;
            return false;
        }
        if (line.empty()) continue;
        std::istringstream row(line);
        char type = 0;
        row >> type;
        if (type == 'C') {
            Claim claim;
            int pvp = 0, explosions = 0, fire = 0, containers = 0, mobGrief = 0;
            if (!(row >> claim.id >> claim.minX >> claim.minZ >> claim.maxX >> claim.maxZ
                      >> claim.owner >> pvp >> explosions >> fire >> containers >> mobGrief)) {
                damaged_ = true;
                return false;
            }
            std::string extra;
            if (row >> extra || claim.id <= 0 || !ValidIdentity(claim.owner) ||
                claim.minX > claim.maxX || claim.minZ > claim.maxZ ||
                static_cast<long long>(claim.maxX) - claim.minX > 63ll ||
                static_cast<long long>(claim.maxZ) - claim.minZ > 63ll ||
                pvp < 0 || pvp > 1 || explosions < 0 || explosions > 1 ||
                fire < 0 || fire > 1 || containers < 0 || containers > 1 ||
                mobGrief < 0 || mobGrief > 1 || loaded.size() >= 32u) {
                damaged_ = true;
                return false;
            }
            claim.allowPvp = pvp != 0;
            claim.allowExplosions = explosions != 0;
            claim.allowFire = fire != 0;
            claim.allowContainers = containers != 0;
            claim.allowMobGrief = mobGrief != 0;
            for (const Claim& existing : loaded) {
                if (existing.id == claim.id || Overlaps(existing, claim)) {
                    damaged_ = true;
                    return false;
                }
            }
            nextClaimId_ = std::max(nextClaimId_, claim.id + 1);
            loaded.push_back(std::move(claim));
        } else if (type == 'T') {
            int claimId = 0, permissions = 0;
            std::string identity, extra;
            if (!(row >> claimId >> identity >> permissions) || (row >> extra) ||
                !ValidIdentity(identity) || permissions <= 0 ||
                permissions > static_cast<int>(kAllPermissions)) {
                damaged_ = true;
                return false;
            }
            auto claim = std::find_if(loaded.begin(), loaded.end(),
                [claimId](const Claim& value) { return value.id == claimId; });
            if (claim == loaded.end() || claim->trusted.size() >= 16u ||
                std::any_of(claim->trusted.begin(), claim->trusted.end(),
                    [&identity](const TrustedPlayer& trusted) { return trusted.identity == identity; })) {
                damaged_ = true;
                return false;
            }
            claim->trusted.push_back({identity, static_cast<std::uint8_t>(permissions)});
        } else {
            damaged_ = true;
            return false;
        }
    }
    if (input.bad()) {
        damaged_ = true;
        return false;
    }
    claims_ = std::move(loaded);
    loaded_ = true;
    return true;
}

bool ProtectionManager::SaveLocked() const {
    if (!loaded_ || damaged_ || sidecarPath_.empty()) return false;
    const std::string temporary = sidecarPath_ + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "MFCLAIMS1\n";
    for (const Claim& claim : claims_) {
        output << "C " << claim.id << ' ' << claim.minX << ' ' << claim.minZ << ' '
               << claim.maxX << ' ' << claim.maxZ << ' ' << claim.owner << ' '
               << (claim.allowPvp ? 1 : 0) << ' '
               << (claim.allowExplosions ? 1 : 0) << ' '
               << (claim.allowFire ? 1 : 0) << ' '
               << (claim.allowContainers ? 1 : 0) << ' '
               << (claim.allowMobGrief ? 1 : 0) << '\n';
        for (const TrustedPlayer& trusted : claim.trusted) {
            output << "T " << claim.id << ' ' << trusted.identity << ' '
                   << static_cast<unsigned int>(trusted.permissions) << '\n';
        }
    }
    output.flush();
    if (!output) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    output.close();
    std::error_code error;
    std::filesystem::rename(temporary, sidecarPath_, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

bool ProtectionManager::Save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return SaveLocked();
}

bool ProtectionManager::CreateClaim(const std::string& owner, int centerX, int centerZ,
                                     int radius, std::string* message) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto fail = [message](const char* text) { if (message != nullptr) *message = text; return false; };
    if (!loaded_ || damaged_) return fail("No se pudo leer el archivo de parcelas; no se sobrescribieron tus datos.");
    if (!ValidIdentity(owner)) return fail("Identidad de jugador no válida.");
    if (std::count_if(claims_.begin(), claims_.end(),
        [&owner](const Claim& claim) { return claim.owner == owner; }) >= 8) {
        return fail("Has alcanzado el límite de 8 parcelas.");
    }
    radius = std::clamp(radius, 4, 32);
    Claim claim;
    claim.id = nextClaimId_;
    claim.minX = centerX - radius;
    claim.minZ = centerZ - radius;
    claim.maxX = centerX + radius - 1;
    claim.maxZ = centerZ + radius - 1;
    claim.owner = owner;
    for (const Claim& existing : claims_) {
        if (Overlaps(existing, claim)) return fail("La parcela se solapa con otra parcela protegida.");
    }
    claims_.push_back(claim);
    ++nextClaimId_;
    if (!SaveLocked()) {
        claims_.pop_back();
        --nextClaimId_;
        return fail("No se pudo guardar la parcela.");
    }
    if (message != nullptr) {
        *message = "Parcela #" + std::to_string(claim.id) + " creada (" +
            std::to_string(claim.minX) + "," + std::to_string(claim.minZ) + ") a (" +
            std::to_string(claim.maxX) + "," + std::to_string(claim.maxZ) +
            "). Solo tú puedes construir en ella.";
    }
    return true;
}

bool ProtectionManager::Trust(const std::string& owner, const std::string& trusted,
                              std::uint8_t permissions, std::string* message) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto fail = [message](const char* text) { if (message != nullptr) *message = text; return false; };
    if (!loaded_ || damaged_) return fail("El archivo de parcelas no está disponible.");
    if (!ValidIdentity(owner) || !ValidIdentity(trusted) || owner == trusted ||
        permissions == 0u || (permissions & ~kAllPermissions) != 0u)
        return fail("Identidad o permisos no válidos.");
    std::vector<Claim*> owned;
    for (Claim& claim : claims_) if (claim.owner == owner) owned.push_back(&claim);
    if (owned.empty()) return fail("Primero crea una parcela con /claim create.");
    const std::vector<Claim> backup = claims_;
    for (Claim* claim : owned) {
        auto found = std::find_if(claim->trusted.begin(), claim->trusted.end(),
            [&trusted](const TrustedPlayer& value) { return value.identity == trusted; });
        if (found == claim->trusted.end()) {
            if (claim->trusted.size() >= 16u) {
                claims_ = backup;
                return fail("Máximo 16 jugadores autorizados por parcela.");
            }
            claim->trusted.push_back({trusted, permissions});
        } else {
            found->permissions = permissions;
        }
    }
    if (!SaveLocked()) {
        claims_ = backup;
        return fail("No se pudieron guardar los permisos.");
    }
    if (message != nullptr) *message = "Jugador autorizado en todas tus parcelas. Permisos: " +
        std::string((permissions & kBuildPermission) != 0u ? "construir " : "") +
        std::string((permissions & kInteractPermission) != 0u ? "interactuar " : "") +
        std::string((permissions & kContainerPermission) != 0u ? "cofres" : "");
    return true;
}

bool ProtectionManager::Untrust(const std::string& owner, const std::string& trusted,
                                std::string* message) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto fail = [message](const char* text) { if (message != nullptr) *message = text; return false; };
    if (!loaded_ || damaged_) return fail("El archivo de parcelas no está disponible.");
    const std::vector<Claim> backup = claims_;
    bool removed = false;
    for (Claim& claim : claims_) {
        if (claim.owner != owner) continue;
        const auto before = claim.trusted.size();
        claim.trusted.erase(std::remove_if(claim.trusted.begin(), claim.trusted.end(),
            [&trusted](const TrustedPlayer& value) { return value.identity == trusted; }), claim.trusted.end());
        removed = removed || before != claim.trusted.size();
    }
    if (!removed) return fail("Ese jugador no estaba autorizado.");
    if (!SaveLocked()) {
        claims_ = backup;
        return fail("No se pudo guardar el cambio.");
    }
    if (message != nullptr) *message = "Permiso retirado de todas tus parcelas.";
    return true;
}

bool ProtectionManager::Abandon(const std::string& owner, std::string* message) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto fail = [message](const char* text) { if (message != nullptr) *message = text; return false; };
    if (!loaded_ || damaged_) return fail("El archivo de parcelas no está disponible.");
    const std::vector<Claim> backup = claims_;
    const auto before = claims_.size();
    claims_.erase(std::remove_if(claims_.begin(), claims_.end(),
        [&owner](const Claim& claim) { return claim.owner == owner; }), claims_.end());
    if (before == claims_.size()) return fail("No tienes parcelas que abandonar.");
    if (!SaveLocked()) {
        claims_ = backup;
        return fail("No se pudo guardar el abandono.");
    }
    if (message != nullptr) *message = "Has abandonado tus parcelas. El terreno vuelve a estar sin protección.";
    return true;
}

bool ProtectionManager::SetFlag(const std::string& owner, const std::string& flag,
                                bool enabled, std::string* message) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto fail = [message](const char* text) { if (message != nullptr) *message = text; return false; };
    if (!loaded_ || damaged_) return fail("El archivo de parcelas no está disponible.");
    const std::vector<Claim> backup = claims_;
    bool found = false;
    for (Claim& claim : claims_) {
        if (claim.owner != owner) continue;
        found = true;
        if (flag == "pvp") claim.allowPvp = enabled;
        else if (flag == "explosions") claim.allowExplosions = enabled;
        else if (flag == "fire") claim.allowFire = enabled;
        else if (flag == "containers") claim.allowContainers = enabled;
        else if (flag == "mob_grief") claim.allowMobGrief = enabled;
        else return fail("Bandera desconocida. Usa pvp, explosions, fire, containers o mob_grief.");
    }
    if (!found) return fail("No tienes una parcela.");
    if (!SaveLocked()) {
        claims_ = backup;
        return fail("No se pudo guardar la bandera.");
    }
    if (message != nullptr) *message = "Bandera " + flag + (enabled ? " aplicada a todas tus parcelas." : " desactivada en todas tus parcelas.");
    return true;
}

bool ProtectionManager::CanBuild(const std::string& player, int x, int z) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (damaged_) return false;
    const Claim* claim = FindClaimAt(x, z);
    if (claim == nullptr || claim->owner == player) return true;
    const auto trusted = std::find_if(claim->trusted.begin(), claim->trusted.end(),
        [&player](const TrustedPlayer& value) { return value.identity == player; });
    return trusted != claim->trusted.end() &&
           (trusted->permissions & kBuildPermission) != 0u;
}

bool ProtectionManager::CanUseContainer(const std::string& player, int x, int z) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (damaged_) return false;
    const Claim* claim = FindClaimAt(x, z);
    if (claim == nullptr || claim->owner == player) return true;
    if (!claim->allowContainers) return false;
    const auto trusted = std::find_if(claim->trusted.begin(), claim->trusted.end(),
        [&player](const TrustedPlayer& value) { return value.identity == player; });
    return trusted != claim->trusted.end() &&
           (trusted->permissions & kContainerPermission) != 0u;
}

bool ProtectionManager::CanPvp(int x, int z) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (damaged_) return false;
    const Claim* claim = FindClaimAt(x, z);
    return claim == nullptr || claim->allowPvp;
}
bool ProtectionManager::CanExplode(int x, int z) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (damaged_) return false;
    const Claim* claim = FindClaimAt(x, z);
    return claim == nullptr || claim->allowExplosions;
}
bool ProtectionManager::CanFireSpread(int x, int z) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (damaged_) return false;
    const Claim* claim = FindClaimAt(x, z);
    return claim == nullptr || claim->allowFire;
}
bool ProtectionManager::CanMobGrief(int x, int z) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (damaged_) return false;
    const Claim* claim = FindClaimAt(x, z);
    return claim == nullptr || claim->allowMobGrief;
}

std::string ProtectionManager::ListFor(const std::string& owner) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream output;
    bool found = false;
    for (const Claim& claim : claims_) {
        if (claim.owner != owner) continue;
        found = true;
        output << "Parcela #" << claim.id << " · X " << claim.minX << "…" << claim.maxX
               << " · Z " << claim.minZ << "…" << claim.maxZ
               << " · autorizados " << claim.trusted.size() << "\\n";
    }
    return found ? output.str() : "No tienes parcelas. Usa /claim create para proteger 32×32 bloques.";
}

std::string ProtectionManager::DescribeAt(const std::string& player, int x, int z) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Claim* claim = FindClaimAt(x, z);
    if (claim == nullptr) return "Esta zona no está protegida.";
    std::ostringstream output;
    const bool canBuild = claim->owner == player ||
        std::any_of(claim->trusted.begin(), claim->trusted.end(),
            [&player](const TrustedPlayer& value) {
                return value.identity == player && (value.permissions & kBuildPermission) != 0u;
            });
    const bool canOpenContainers = claim->owner == player ||
        (claim->allowContainers && std::any_of(claim->trusted.begin(), claim->trusted.end(),
            [&player](const TrustedPlayer& value) {
                return value.identity == player && (value.permissions & kContainerPermission) != 0u;
            }));
    output << "Parcela #" << claim->id << " · dueño " << claim->owner
           << " · construir: " << (canBuild ? "sí" : "no")
           << " · cofres: " << (canOpenContainers ? "sí" : "no");
    return output.str();
}

} // namespace minefade
