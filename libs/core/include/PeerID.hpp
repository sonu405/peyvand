#include <SQLiteCpp/SQLiteCpp.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <sys/random.h>

using ID = std::array<uint8_t, 16>;

class PeerID {
  public:
    static constexpr std::size_t size = 16;
    using Bytes = std::array<uint8_t, size>;

    static PeerID generate();
    static std::optional<PeerID> from_bytes(std::span<const uint8_t> b); // length-checked

    std::span<const uint8_t, size> bytes() const {
        return bytes_;
    }
    std::string to_string() const; // dashed UUID

    friend bool operator==(const PeerID&, const PeerID&) = default;
    friend auto operator<=>(const PeerID&, const PeerID&) = default;

  private:
    explicit PeerID(const Bytes& b) : bytes_(b) {}
    Bytes bytes_{};
};

// This is needed if we want to created std::unordered_map<PeerID, Peers>
template <> struct std::hash<PeerID> {
    std::size_t operator()(const PeerID& id) const noexcept {
        std::size_t h;
        std::memcpy(&h, id.bytes().data(), sizeof(h));
        return h;
    }
};