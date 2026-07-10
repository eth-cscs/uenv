#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_all.hpp>

#include <util/fs.h>
#include <util/sha256.h>

namespace {
// digest a text chunk through the low-level streaming interface.
std::span<const std::byte> as_bytes(std::string_view s) {
    return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}
} // namespace

TEST_CASE("sha256 known vectors", "[sha256]") {
    // canonical FIPS 180-4 / NIST test vectors
    REQUIRE(util::sha256_string("").hex() ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(util::sha256_string("abc").hex() ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(util::sha256_string(
                "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
                .hex() ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256 zero-byte input", "[sha256]") {
    const std::string empty =
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

    // empty (zero-length) input must hash to the canonical empty digest, via
    // every interface that accepts data.
    REQUIRE(util::sha256_string("").hex() == empty);
    REQUIRE(util::sha256_bytes({}).hex() == empty);

    // low-level: init then final with no update at all.
    util::sha256_state s;
    util::sha256_init(s);
    REQUIRE(util::sha256_final(s).hex() == empty);

    // low-level: an explicit zero-length update is a no-op.
    util::sha256_state s2;
    util::sha256_init(s2);
    util::sha256_update(s2, {});
    REQUIRE(util::sha256_final(s2).hex() == empty);

    // a single NUL byte (0x00) is distinct from empty input and has its own
    // known digest.
    const std::byte nul{0x00};
    REQUIRE(util::sha256_bytes({&nul, 1}).hex() ==
            "6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d");
}

TEST_CASE("sha256_digest carries raw bytes", "[sha256]") {
    auto d = util::sha256_string("abc");
    REQUIRE(d.bytes[0] == 0xba);
    REQUIRE(d.bytes[1] == 0x78);
    REQUIRE(d.bytes[31] == 0xad);
    REQUIRE(d == util::sha256_string("abc"));
    REQUIRE_FALSE(d == util::sha256_string("abd"));
}

TEST_CASE("sha256 multi-block and incremental", "[sha256]") {
    // a million 'a' characters -> known NIST vector. fed in chunks to exercise
    // the streaming/buffering path of the low-level interface.
    const std::string expected =
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";

    util::sha256_state s;
    util::sha256_init(s);
    std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) {
        util::sha256_update(s, as_bytes(chunk));
    }
    REQUIRE(util::sha256_final(s).hex() == expected);

    // same input in a single buffer must match
    REQUIRE(util::sha256_string(std::string(1000000, 'a')).hex() == expected);
}

TEST_CASE("sha256 update splits do not change the digest", "[sha256]") {
    const std::string msg = "the quick brown fox jumps over the lazy dog";
    const std::string whole = util::sha256_string(msg).hex();

    for (std::size_t split = 0; split <= msg.size(); ++split) {
        util::sha256_state s;
        util::sha256_init(s);
        util::sha256_update(s,
                            as_bytes(std::string_view{msg}.substr(0, split)));
        util::sha256_update(s, as_bytes(std::string_view{msg}.substr(split)));
        REQUIRE(util::sha256_final(s).hex() == whole);
    }
}

namespace {
// write `content` to a fresh file in a temporary directory, and return its
// path.
std::filesystem::path scratch_file(std::string_view content) {
    auto dir = util::make_temp_dir().value();
    auto path = dir / "payload.bin";
    std::ofstream f{path, std::ios::binary};
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    f.close();
    return path;
}
} // namespace

TEST_CASE("sha256_file", "[sha256]") {
    // sha256_file streams the file in 1 MiB chunks; size the cases around that.
    constexpr std::size_t chunk = 1u << 20;

    SECTION("matches the one-shot digest") {
        const std::string content(3 * chunk + 12345, 'a');
        auto d = util::sha256_file(scratch_file(content));
        REQUIRE(d);
        REQUIRE(d->hex() == util::sha256_string(content).hex());
    }

    SECTION("an empty file digests the empty string") {
        auto d = util::sha256_file(scratch_file(""));
        REQUIRE(d);
        REQUIRE(d->hex() == util::sha256_string("").hex());
    }

    SECTION("a missing file is an error") {
        REQUIRE_FALSE(util::sha256_file("/no/such/file-xyz123"));
    }

    // the progress callback reports cumulative bytes hashed.
    SECTION("progress is monotonic and ends at the file size") {
        const std::string content(3 * chunk + 12345, 'b');
        auto path = scratch_file(content);

        std::vector<std::uint64_t> seen;
        auto d = util::sha256_file(
            path, [&seen](std::uint64_t hashed) { seen.push_back(hashed); });
        REQUIRE(d);

        // instrumenting the read must not corrupt the digest.
        REQUIRE(d->hex() == util::sha256_string(content).hex());

        REQUIRE_FALSE(seen.empty());
        REQUIRE(std::is_sorted(seen.begin(), seen.end()));
        REQUIRE(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
        REQUIRE(seen.back() == content.size());
        // 3 full chunks plus a partial one.
        REQUIRE(seen.size() == 4);
    }

    SECTION("a file below the chunk size reports once") {
        const std::string content(1024, 'c');
        std::vector<std::uint64_t> seen;
        auto d = util::sha256_file(
            scratch_file(content),
            [&seen](std::uint64_t hashed) { seen.push_back(hashed); });
        REQUIRE(d);
        REQUIRE(seen.size() == 1);
        REQUIRE(seen.front() == content.size());
    }

    SECTION("an empty file never reports progress") {
        std::vector<std::uint64_t> seen;
        auto d =
            util::sha256_file(scratch_file(""), [&seen](std::uint64_t hashed) {
                seen.push_back(hashed);
            });
        REQUIRE(d);
        REQUIRE(seen.empty());
    }
}
