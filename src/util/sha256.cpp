#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>

#include <fmt/format.h>

#include "sha256.h"

namespace util {

namespace {

// SHA-256 round constants (first 32 bits of the fractional parts of the cube
// roots of the first 64 primes), FIPS 180-4 §4.2.2.
constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

// The digest state.
// Callers see it as the opaque sha256_state blob.
struct sha256_impl {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buffer[64];
    std::size_t buffer_len;

    void init();
    void update(const uint8_t* p, std::size_t len);
    sha256_digest final();
    void process_block(const uint8_t block[64]);
};

static_assert(sizeof(sha256_impl) <= sizeof(sha256_state::opaque),
              "sha256_state::opaque too small for the implementation state");
static_assert(alignof(sha256_impl) <= alignof(sha256_state),
              "sha256_state under-aligned for the implementation state");

// view the opaque blob as the implementation state (lifetime begun in
// sha256_init via placement new).
inline sha256_impl& impl(sha256_state& s) {
    return *std::launder(reinterpret_cast<sha256_impl*>(s.opaque));
}

void sha256_impl::init() {
    // initial hash values: fractional parts of the square roots of the first 8
    // primes, FIPS 180-4 §5.3.3.
    state[0] = 0x6a09e667;
    state[1] = 0xbb67ae85;
    state[2] = 0x3c6ef372;
    state[3] = 0xa54ff53a;
    state[4] = 0x510e527f;
    state[5] = 0x9b05688c;
    state[6] = 0x1f83d9ab;
    state[7] = 0x5be0cd19;
    bitlen = 0;
    buffer_len = 0;
}

void sha256_impl::process_block(const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 =
            rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 =
            rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void sha256_impl::update(const uint8_t* p, std::size_t len) {
    bitlen += static_cast<uint64_t>(len) * 8;

    // top up a partially-filled buffer first
    if (buffer_len > 0) {
        std::size_t need = 64 - buffer_len;
        std::size_t take = len < need ? len : need;
        std::memcpy(buffer + buffer_len, p, take);
        buffer_len += take;
        p += take;
        len -= take;
        if (buffer_len == 64) {
            process_block(buffer);
            buffer_len = 0;
        }
    }

    // process whole blocks straight from the input
    while (len >= 64) {
        process_block(p);
        p += 64;
        len -= 64;
    }

    // stash the remainder
    if (len > 0) {
        std::memcpy(buffer + buffer_len, p, len);
        buffer_len += len;
    }
}

sha256_digest sha256_impl::final() {
    uint64_t final_bitlen = bitlen;

    // append the 0x80 padding byte, then zeros up to a 56-byte boundary, then
    // the 64-bit big-endian length.
    uint8_t pad = 0x80;
    update(&pad, 1);

    uint8_t zero = 0x00;
    while (buffer_len != 56) {
        update(&zero, 1);
    }

    uint8_t lenbytes[8];
    for (int i = 0; i < 8; ++i) {
        lenbytes[i] = static_cast<uint8_t>(final_bitlen >> (56 - i * 8));
    }
    update(lenbytes, 8); // fills the block; update() flushes it

    sha256_digest digest{};
    for (int i = 0; i < 8; ++i) {
        digest.bytes[i * 4] = static_cast<uint8_t>(state[i] >> 24);
        digest.bytes[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
        digest.bytes[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
        digest.bytes[i * 4 + 3] = static_cast<uint8_t>(state[i]);
    }
    return digest;
}

} // namespace

std::string sha256_digest::hex() const {
    std::string out;
    out.reserve(64);
    for (std::uint8_t b : bytes) {
        fmt::format_to(std::back_inserter(out), "{:02x}", b);
    }
    return out;
}

void sha256_init(sha256_state& state) {
    auto* p = new (state.opaque) sha256_impl;
    p->init();
}

void sha256_update(sha256_state& state, std::span<const std::byte> data) {
    impl(state).update(reinterpret_cast<const uint8_t*>(data.data()),
                       data.size());
}

sha256_digest sha256_final(sha256_state& state) {
    return impl(state).final();
}

sha256_digest sha256_bytes(std::span<const std::byte> data) {
    sha256_state s;
    sha256_init(s);
    sha256_update(s, data);
    return sha256_final(s);
}

sha256_digest sha256_string(std::string_view text) {
    return sha256_bytes(
        {reinterpret_cast<const std::byte*>(text.data()), text.size()});
}

util::expected<sha256_digest, std::string>
sha256_file(const std::filesystem::path& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return util::unexpected{fmt::format("unable to open {} for reading: {}",
                                            path.string(),
                                            std::strerror(errno))};
    }

    sha256_state s;
    sha256_init(s);
    std::array<std::byte, 1 << 16> buf;
    std::size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
        sha256_update(s, std::span<const std::byte>{buf.data(), n});
    }

    if (std::ferror(f)) {
        std::fclose(f);
        return util::unexpected{fmt::format("error reading {}", path.string())};
    }
    std::fclose(f);

    return sha256_final(s);
}

} // namespace util
