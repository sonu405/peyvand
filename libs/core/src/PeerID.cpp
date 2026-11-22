#include "PeerID.hpp"
#include <string>

constexpr char kHex[] = "0123456789abcdef";

constexpr int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

PeerID PeerID::generate() {
    Bytes b;
    std::size_t filled = 0;

    while (filled < b.size()) {
        const ssize_t n = getrandom(b.data() + filled, b.size() - filled, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            throw std::system_error(errno, std::generic_category(), "getrandom");
        }
        filled += static_cast<std::size_t>(n);
    }

    b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40); // version 4
    b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80); // variant 10
    return PeerID(b);
}

std::optional<PeerID> PeerID::from_bytes(std::span<const uint8_t> b) {
    if (b.size() != size)
        return std::nullopt;
    Bytes copy;
    std::ranges::copy(b, copy.begin());
    return PeerID(copy);
}

std::string PeerID::to_string() const {
    std::string s;
    s.reserve(36);

    for (std::size_t i = 0; i < size; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            s.push_back('-');
        s.push_back(kHex[bytes_[i] >> 4]);
        s.push_back(kHex[bytes_[i] & 0x0F]);
    }
    return s;
}

std::string PeerID::short_tag() const {
    std::string s;
    for (std::size_t i = 0; i < 2; ++i) {  // first 2 bytes = 4 hex chars
        s.push_back(kHex[bytes_[i] >> 4]);
        s.push_back(kHex[bytes_[i] & 0x0F]);
    }
    return s;
}
