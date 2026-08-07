#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <openssl/evp.h>

#include <util/sha.h>

namespace util {

bool is_sha_string(std::string_view s) {
    for (char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) {
            return false;
        }
    }
    return true;
}

// The digest state, held behind sha256_hasher's pimpl: an OpenSSL EVP digest
// context, so that libcrypto's accelerated SHA-256 does the work - SHA-NI or
// AVX2 on x86-64, the ARMv8 crypto extensions on aarch64, dispatched at run
// time. That is worth roughly an order of magnitude over a portable C
// implementation, and hashing bounds both `uenv image push` (which digests the
// whole squashfs before uploading it) and `uenv image pull` (which digests the
// download as it streams).
//
// None of the failure paths below are reachable short of allocation failure:
// OpenSSL is configured no-module/no-dso, so the default provider that supplies
// SHA-256 is compiled into libcrypto and cannot fail to load. They throw
// because the public API has no error channel, and a wrong digest would be far
// worse than an exception.
class sha256_impl {
  public:
    sha256_impl();
    ~sha256_impl();
    sha256_impl(const sha256_impl&) = delete;
    sha256_impl& operator=(const sha256_impl&) = delete;

    void init();
    void update(const unsigned char* p, std::size_t len);
    sha256 finalize() const;

  private:
    EVP_MD_CTX* ctx_;
};

sha256_impl::sha256_impl() : ctx_(EVP_MD_CTX_new()) {
    if (ctx_ == nullptr) {
        throw std::runtime_error("unable to allocate a SHA-256 digest context");
    }
    init();
}

sha256_impl::~sha256_impl() {
    EVP_MD_CTX_free(ctx_);
}

void sha256_impl::init() {
    if (EVP_DigestInit_ex(ctx_, EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("unable to initialise a SHA-256 digest");
    }
}

void sha256_impl::update(const unsigned char* p, std::size_t len) {
    if (EVP_DigestUpdate(ctx_, p, len) != 1) {
        throw std::runtime_error("unable to update a SHA-256 digest");
    }
}

sha256 sha256_impl::finalize() const {
    // EVP_DigestFinal_ex() appends the padding and consumes the context it is
    // given, so work on a copy: a hasher may keep being updated after
    // finalize().
    EVP_MD_CTX* copy = EVP_MD_CTX_new();
    if (copy == nullptr) {
        throw std::runtime_error("unable to allocate a SHA-256 digest context");
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    const bool ok = EVP_MD_CTX_copy_ex(copy, ctx_) == 1 &&
                    EVP_DigestFinal_ex(copy, digest, &len) == 1;
    EVP_MD_CTX_free(copy);
    if (!ok || len != 32) {
        throw std::runtime_error("unable to finalize a SHA-256 digest");
    }

    // render the 32 digest bytes as 64 lowercase-hex characters.
    std::string hex;
    hex.reserve(64);
    for (unsigned int i = 0; i < len; ++i) {
        fmt::format_to(std::back_inserter(hex), "{:02x}", digest[i]);
    }
    // trusted by construction: hex is exactly 64 lowercase-hex characters.
    return sha256::parse(hex).value();
}

sha256_hasher::sha256_hasher() : impl_(std::make_unique<sha256_impl>()) {
}

sha256_hasher::~sha256_hasher() = default;
sha256_hasher::sha256_hasher(sha256_hasher&&) noexcept = default;
sha256_hasher& sha256_hasher::operator=(sha256_hasher&&) noexcept = default;

void sha256_hasher::update(std::span<const std::byte> data) {
    impl_->update(reinterpret_cast<const unsigned char*>(data.data()),
                  data.size());
}

void sha256_hasher::reset() {
    impl_->init();
}

sha256 sha256_hasher::finalize() const {
    return impl_->finalize();
}

sha256 sha256_bytes(std::span<const std::byte> data) {
    sha256_hasher h;
    h.update(data);
    return h.finalize();
}

sha256 sha256_string(std::string_view text) {
    return sha256_bytes(
        {reinterpret_cast<const std::byte*>(text.data()), text.size()});
}

util::expected<sha256, std::string>
sha256_file(const std::filesystem::path& path,
            std::function<void(std::uint64_t)> progress) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return util::unexpected{fmt::format("unable to open {} for reading: {}",
                                            path.string(),
                                            std::strerror(errno))};
    }

    sha256_hasher h;
    // one chunk per megabyte: fine-grained enough to drive a progress bar
    // reported in MB, without calling back hundreds of thousands of times on a
    // multi-GB file. Heap-allocated: too large to sit on a thread stack.
    std::vector<std::byte> buf(1 << 20);
    std::uint64_t hashed = 0;
    std::size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
        h.update(std::span<const std::byte>{buf.data(), n});
        hashed += n;
        if (progress) {
            progress(hashed);
        }
    }

    if (std::ferror(f)) {
        std::fclose(f);
        return util::unexpected{fmt::format("error reading {}", path.string())};
    }
    std::fclose(f);

    return h.finalize();
}

} // namespace util
