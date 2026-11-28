#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace minefade {

class ProtectionManager {
public:
    static constexpr std::uint8_t kBuildPermission = 1u;
    static constexpr std::uint8_t kInteractPermission = 2u;
    static constexpr std::uint8_t kContainerPermission = 4u;
    static constexpr std::uint8_t kAllPermissions =
        kBuildPermission | kInteractPermission | kContainerPermission;

    bool Load(const std::string& sidecarPath);
    bool Save() const;

    bool CreateClaim(const std::string& owner, int centerX, int centerZ,
                     int radius, std::string* message = nullptr);
    bool Trust(const std::string& owner, const std::string& trusted,
               std::uint8_t permissions = kAllPermissions,
               std::string* message = nullptr);
    bool Untrust(const std::string& owner, const std::string& trusted,
                 std::string* message = nullptr);
    bool Abandon(const std::string& owner, std::string* message = nullptr);
    bool SetFlag(const std::string& owner, const std::string& flag,
                 bool enabled, std::string* message = nullptr);

    bool CanBuild(const std::string& player, int x, int z) const;
    bool CanUseContainer(const std::string& player, int x, int z) const;
    bool CanPvp(int x, int z) const;
    bool CanExplode(int x, int z) const;
    bool CanFireSpread(int x, int z) const;
    bool CanMobGrief(int x, int z) const;

    std::string ListFor(const std::string& owner) const;
    std::string DescribeAt(const std::string& player, int x, int z) const;
    static bool ValidIdentity(const std::string& identity);

private:
    struct TrustedPlayer {
        std::string identity;
        std::uint8_t permissions{0u};
    };
    struct Claim {
        int id{0};
        int minX{0};
        int minZ{0};
        int maxX{0};
        int maxZ{0};
        std::string owner;
        std::vector<TrustedPlayer> trusted;
        bool allowPvp{false};
        bool allowExplosions{false};
        bool allowFire{false};
        bool allowContainers{true};
        bool allowMobGrief{false};
    };

    Claim* FindClaimForOwner(const std::string& owner);
    const Claim* FindClaimAt(int x, int z) const;
    bool SaveLocked() const;
    static bool Overlaps(const Claim& a, const Claim& b);
    static bool IsInside(const Claim& claim, int x, int z);

    mutable std::mutex mutex_;
    std::string sidecarPath_;
    std::vector<Claim> claims_;
    int nextClaimId_{1};
    bool loaded_{false};
    bool damaged_{false};
};

} // namespace minefade
