// Live registry round-trip tests for the native OCI client.
//
// These drive push/pull/attach/referrers/copy against a real registry. A zot
// binary is started lazily on the first [registry] case and killed at process
// exit; each test uses a unique repository path, so OCI's per-repository
// namespacing keeps them isolated. If no zot binary is found the cases SKIP, so
// a normal `./test/unit` run is unaffected.
//
// The binary is located via -DUENV_TEST_ZOT_PATH (set by meson), the
// UENV_TEST_ZOT env override, or the PATH.

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include <oci/client.h>
#include <oci/digest.h>
#include <oci/manifest.h>
#include <oci/pull.h>
#include <oci/push.h>
#include <oci/reference.h>
#include <util/curl.h>
#include <util/fs.h>
#include <util/sha256.h>
#include <util/subprocess.h>

namespace {

// locate the zot binary: build-time path, env override, then PATH.
std::string zot_binary() {
    if (const char* e = std::getenv("UENV_TEST_ZOT")) {
        if (::access(e, X_OK) == 0) {
            return e;
        }
    }
#ifdef UENV_TEST_ZOT_PATH
    if (::access(UENV_TEST_ZOT_PATH, X_OK) == 0) {
        return UENV_TEST_ZOT_PATH;
    }
#endif
    // let execvp resolve "zot" on PATH; empty means "not found here".
    return "zot";
}

int free_port() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    int port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

bool ready(const std::string& base) {
    util::curl::request req;
    req.url = base + "/v2/";
    auto resp = util::curl::perform(req);
    return resp && (resp->status == 200 || resp->status == 401);
}

// a lazily-started zot registry, shared by all [registry] cases and killed when
// the process exits.
struct zot_instance {
    std::optional<util::subprocess> proc;
    std::string base; // http://127.0.0.1:PORT, empty when unavailable
    std::filesystem::path state;

    zot_instance() {
        const auto zot = zot_binary();
        const int port = free_port();
        state = util::make_temp_dir().value();
        const auto cfg = state / "config.json";
        {
            std::ofstream f{cfg};
            f << fmt::format(
                R"({{"storage":{{"rootDirectory":"{}"}},)"
                R"("http":{{"address":"127.0.0.1","port":"{}"}},)"
                R"("log":{{"level":"error","output":"{}"}}}})",
                (state / "registry").string(), port,
                (state / "zot.log").string());
        }
        auto p = util::run({zot, "serve", cfg.string()});
        if (!p) {
            return; // no zot binary / failed to launch -> tests skip
        }
        const auto url = fmt::format("http://127.0.0.1:{}", port);
        for (int i = 0; i < 150; ++i) { // up to ~30s
            if (p->finished()) {
                return; // zot died on startup
            }
            if (ready(url)) {
                proc = std::move(*p);
                base = url;
                return;
            }
            ::usleep(200 * 1000);
        }
        p->kill();
    }

    ~zot_instance() {
        if (proc) {
            proc->kill();
        }
    }
};

// the shared registry base URL, or empty when no registry could be started.
const std::string& registry_base() {
    static zot_instance instance;
    return instance.base;
}

void write_file(const std::filesystem::path& path, std::string_view content) {
    std::ofstream f{path, std::ios::binary};
    f << content;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream f{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("oci registry push/pull round-trip", "[registry]") {
    const auto base = registry_base();
    if (base.empty()) {
        SKIP("no zot binary available for the registry tests");
    }

    auto dir = util::make_temp_dir().value();
    const auto sqfs = dir / "store.squashfs";
    const std::string payload = "squashfs-bytes-round-trip";
    write_file(sqfs, payload);

    auto c = oci::client::create(base, "test/app/1.0");
    REQUIRE(c.has_value());

    auto pushed = oci::push_squashfs(*c, sqfs, oci::reference::tag("v1"));
    REQUIRE(pushed.has_value());

    // the pushed digest is the sha256 of the manifest the registry stores.
    auto resp = c->get_manifest(oci::reference::tag("v1"));
    REQUIRE(resp.has_value());
    const auto local = oci::digest::from_sha256(util::sha256_string(resp->body));
    REQUIRE(local == *pushed);
    if (resp->digest) {
        REQUIRE(*resp->digest == *pushed);
    }

    // pull the layer back; pull_squashfs self-verifies its digest internally.
    auto image = oci::parse_manifest(resp->body);
    REQUIRE(image.has_value());
    auto store = util::make_temp_dir().value();
    auto pulled = oci::pull_squashfs(*c, *image, store);
    REQUIRE(pulled.has_value());
    REQUIRE(read_file(store / "store.squashfs") == payload);
}

TEST_CASE("oci registry attach + referrers + pull_meta", "[registry]") {
    const auto base = registry_base();
    if (base.empty()) {
        SKIP("no zot binary available for the registry tests");
    }

    auto dir = util::make_temp_dir().value();
    const auto sqfs = dir / "store.squashfs";
    write_file(sqfs, "sqfs");
    const auto meta = dir / "meta";
    std::filesystem::create_directories(meta);
    write_file(meta / "env.json", R"({"views":{}})");

    auto c = oci::client::create(base, "test/meta-app/1.0");
    REQUIRE(c.has_value());

    auto pushed = oci::push_squashfs(*c, sqfs, oci::reference::tag("v1"));
    REQUIRE(pushed.has_value());

    auto att = oci::attach(*c, oci::reference::digest(*pushed), "uenv/meta", meta);
    REQUIRE(att.has_value());

    auto refs = c->referrers(*pushed);
    REQUIRE(refs.has_value());
    bool found_meta = false;
    for (const auto& d : *refs) {
        if (d.artifact_type && *d.artifact_type == "uenv/meta") {
            found_meta = true;
        }
    }
    REQUIRE(found_meta);

    auto store = util::make_temp_dir().value();
    auto got = oci::pull_meta(*c, *pushed, store);
    REQUIRE(got.has_value());
    REQUIRE(*got);
    REQUIRE(std::filesystem::exists(store / "meta" / "env.json"));
    REQUIRE(read_file(store / "meta" / "env.json") == R"({"views":{}})");
}

TEST_CASE("oci registry copy preserves digest", "[registry]") {
    const auto base = registry_base();
    if (base.empty()) {
        SKIP("no zot binary available for the registry tests");
    }

    auto dir = util::make_temp_dir().value();
    const auto sqfs = dir / "store.squashfs";
    write_file(sqfs, "copy-me");

    const std::string src_repo = "test/copy-src/1.0";
    const std::string dst_repo = "test/copy-dst/1.0";

    auto src = oci::client::create(base, src_repo);
    REQUIRE(src.has_value());
    auto pushed = oci::push_squashfs(*src, sqfs, oci::reference::tag("v1"));
    REQUIRE(pushed.has_value());

    auto copied = oci::copy_image(base, src_repo, dst_repo, *pushed, "v1",
                                  std::nullopt);
    REQUIRE(copied.has_value());

    // the destination manifest is byte-identical, so its digest is preserved.
    auto dst = oci::client::create(base, dst_repo);
    REQUIRE(dst.has_value());
    auto resp = dst->get_manifest(oci::reference::tag("v1"));
    REQUIRE(resp.has_value());
    const auto local = oci::digest::from_sha256(util::sha256_string(resp->body));
    REQUIRE(local == *pushed);
}
