#include <catch2/catch_all.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <util/fs.h>
#include <util/sha.h>

using namespace util;

TEST_CASE("is_sha_string", "[sha]") {
    REQUIRE(is_sha_string(""));
    REQUIRE(is_sha_string("0123456789abcdef"));
    REQUIRE_FALSE(is_sha_string("ABCDEF")); // uppercase is not lowercase hex
    REQUIRE_FALSE(is_sha_string("g"));      // out of range
    REQUIRE_FALSE(is_sha_string("12 34"));  // whitespace
}

TEST_CASE("sha256 parse accepts exactly 64 lowercase hex", "[sha]") {
    const std::string hex(64, 'a');
    auto s = sha256::parse(hex);
    REQUIRE(s.has_value());
    REQUIRE(s->string() == hex);
}

TEST_CASE("uenv_id parse accepts exactly 16 lowercase hex", "[sha]") {
    const std::string hex(16, 'b');
    auto s = uenv_id::parse(hex);
    REQUIRE(s.has_value());
    REQUIRE(s->string() == hex);
}

TEST_CASE("sha256 parse rejects malformed", "[sha]") {
    // wrong length
    REQUIRE_FALSE(sha256::parse(std::string(63, 'a')).has_value());
    REQUIRE_FALSE(sha256::parse(std::string(65, 'a')).has_value());
    REQUIRE_FALSE(sha256::parse("").has_value());
    // uppercase is not lowercase hex
    REQUIRE_FALSE(sha256::parse(std::string(64, 'A')).has_value());
    // non-hex character
    REQUIRE_FALSE(sha256::parse(std::string(63, 'a') + "g").has_value());
}

TEST_CASE("sha default is a valid all-zero value", "[sha]") {
    sha256 s;
    REQUIRE(s.string() == std::string(64, '0'));
}

TEST_CASE("sha equality and ordering", "[sha]") {
    auto a = sha256::parse(std::string(64, 'a')).value();
    auto b = sha256::parse(std::string(64, 'a')).value();
    auto c = sha256::parse(std::string(64, 'c')).value();
    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
    REQUIRE(a < c);
}

TEST_CASE("sha is hashable and formattable", "[sha]") {
    const std::string hex(64, 'd');
    auto s = sha256::parse(hex).value();

    // usable as an unordered_map key (exercises std::hash specialisation).
    std::unordered_map<sha256, int> m;
    m[s] = 7;
    REQUIRE(m.at(s) == 7);

    // fmt formatter renders the text form.
    REQUIRE(fmt::format("{}", s) == hex);
}

namespace {
// view a text chunk as bytes, for the streaming hasher.
std::span<const std::byte> as_bytes(std::string_view s) {
    return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}
} // namespace

TEST_CASE("sha256 known vectors", "[sha]") {
    // canonical FIPS 180-4 / NIST test vectors
    REQUIRE(util::sha256_string("").string() ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(util::sha256_string("abc").string() ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(util::sha256_string(
                "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
                .string() ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256 zero-byte input", "[sha]") {
    const std::string empty =
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

    // empty (zero-length) input must hash to the canonical empty digest, via
    // every interface that accepts data.
    REQUIRE(util::sha256_string("").string() == empty);
    REQUIRE(util::sha256_bytes({}).string() == empty);

    // streaming: finalize with no update at all.
    util::sha256_hasher h;
    REQUIRE(h.finalize().string() == empty);

    // streaming: an explicit zero-length update is a no-op.
    util::sha256_hasher h2;
    h2.update({});
    REQUIRE(h2.finalize().string() == empty);

    // a single NUL byte (0x00) is distinct from empty input and has its own
    // known digest.
    const std::byte nul{0x00};
    REQUIRE(util::sha256_bytes({&nul, 1}).string() ==
            "6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d");
}

TEST_CASE("sha256 multi-block and incremental", "[sha]") {
    // a million 'a' characters -> known NIST vector. fed in chunks to exercise
    // the streaming/buffering path of the hasher.
    const std::string expected =
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";

    util::sha256_hasher h;
    std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) {
        h.update(as_bytes(chunk));
    }
    REQUIRE(h.finalize().string() == expected);

    // same input in a single buffer must match
    REQUIRE(util::sha256_string(std::string(1000000, 'a')).string() ==
            expected);
}

TEST_CASE("sha256 update splits do not change the digest", "[sha]") {
    const std::string msg = "the quick brown fox jumps over the lazy dog";
    const std::string whole = util::sha256_string(msg).string();

    for (std::size_t split = 0; split <= msg.size(); ++split) {
        util::sha256_hasher h;
        h.update(as_bytes(std::string_view{msg}.substr(0, split)));
        h.update(as_bytes(std::string_view{msg}.substr(split)));
        REQUIRE(h.finalize().string() == whole);
    }
}

TEST_CASE("sha256 finalize is non-destructive", "[sha]") {
    util::sha256_hasher h;
    h.update(as_bytes("abc"));
    const auto first = h.finalize();
    // finalize() must not disturb the state: calling it again, or feeding more
    // data, still works.
    REQUIRE(h.finalize() == first);
    REQUIRE(first == util::sha256_string("abc"));

    h.update(as_bytes("def"));
    REQUIRE(h.finalize() == util::sha256_string("abcdef"));
}

TEST_CASE("sha256 reset restarts the hasher", "[sha]") {
    util::sha256_hasher h;
    h.update(as_bytes("garbage that should be discarded"));
    h.reset();
    h.update(as_bytes("abc"));
    REQUIRE(h.finalize() == util::sha256_string("abc"));
}

TEST_CASE("sha256_hasher is movable", "[sha]") {
    SECTION("move construction carries the state") {
        util::sha256_hasher h;
        h.update(as_bytes("abc"));
        util::sha256_hasher moved{std::move(h)};
        moved.update(as_bytes("def"));
        REQUIRE(moved.finalize() == util::sha256_string("abcdef"));
    }

    SECTION("move assignment carries the state") {
        util::sha256_hasher h;
        h.update(as_bytes("abc"));
        util::sha256_hasher other;
        other.update(as_bytes("discarded"));
        other = std::move(h);
        other.update(as_bytes("def"));
        REQUIRE(other.finalize() == util::sha256_string("abcdef"));
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

TEST_CASE("sha256_file", "[sha]") {
    // sha256_file streams the file in 1 MiB chunks; size the cases around that.
    constexpr std::size_t chunk = 1u << 20;

    SECTION("matches the one-shot digest") {
        const std::string content(3 * chunk + 12345, 'a');
        auto d = util::sha256_file(scratch_file(content));
        REQUIRE(d);
        REQUIRE(d.value() == util::sha256_string(content));
    }

    SECTION("an empty file digests the empty string") {
        auto d = util::sha256_file(scratch_file(""));
        REQUIRE(d);
        REQUIRE(d.value() == util::sha256_string(""));
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
        REQUIRE(d.value() == util::sha256_string(content));

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

// hidden (the leading dot in the tag): hashing 1 GiB is far too slow to belong
// in the default run. Run it explicitly with `./test/unit "[sha-bench]"` to
// measure the digest throughput, which is what bounds `uenv image push` and
// `uenv image pull` on multi-GB squashfs images.
TEST_CASE("sha256 throughput", "[.sha-bench]") {
    constexpr std::size_t chunk = 1u << 20;
    constexpr std::size_t chunks = 1024;

    std::vector<std::byte> buf(chunk, std::byte{0x5a});
    util::sha256_hasher h;

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < chunks; ++i) {
        h.update(std::span<const std::byte>{buf});
    }
    const auto digest = h.finalize();
    const auto stop = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(stop - start).count();
    const double mb = static_cast<double>(chunk * chunks) / (1024.0 * 1024.0);
    WARN(fmt::format("sha256: {:.0f} MiB in {:.3f} s = {:.0f} MiB/s ({})", mb,
                     seconds, mb / seconds, digest));
}
