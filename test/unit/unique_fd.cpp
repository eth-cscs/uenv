#include <fcntl.h>
#include <unistd.h>

#include <utility>

#include <catch2/catch_all.hpp>

#include <util/unique_fd.h>

namespace {

// true if fd names an open descriptor in this process.
bool is_open(int fd) {
    return fd >= 0 && ::fcntl(fd, F_GETFD) != -1;
}

int open_devnull() {
    return ::open("/dev/null", O_RDONLY | O_CLOEXEC);
}

} // namespace

TEST_CASE("unique_fd closes on destruction", "[unique_fd]") {
    const int raw = open_devnull();
    REQUIRE(is_open(raw));
    {
        util::unique_fd fd{raw};
        REQUIRE(fd);
        REQUIRE(fd.get() == raw);
    }
    REQUIRE(!is_open(raw));
}

TEST_CASE("unique_fd default constructs empty", "[unique_fd]") {
    util::unique_fd fd;
    REQUIRE(!fd);
    REQUIRE(fd.get() == -1);
    // destroying an empty unique_fd must not close descriptor -1
}

TEST_CASE("unique_fd move transfers ownership", "[unique_fd]") {
    const int raw = open_devnull();
    REQUIRE(is_open(raw));

    util::unique_fd source{raw};
    util::unique_fd sink{std::move(source)};

    REQUIRE(!source);
    REQUIRE(sink.get() == raw);
    // the descriptor survives the move: exactly one owner, and it is not the
    // moved-from one.
    REQUIRE(is_open(raw));

    sink.reset();
    REQUIRE(!is_open(raw));
}

TEST_CASE("unique_fd move assignment closes the descriptor it replaces",
          "[unique_fd]") {
    const int kept = open_devnull();
    const int replaced = open_devnull();
    REQUIRE(is_open(kept));
    REQUIRE(is_open(replaced));

    util::unique_fd source{kept};
    util::unique_fd sink{replaced};
    sink = std::move(source);

    REQUIRE(!is_open(replaced));
    REQUIRE(is_open(kept));
    REQUIRE(sink.get() == kept);
    REQUIRE(!source);
}

TEST_CASE("unique_fd self-move keeps the descriptor open", "[unique_fd]") {
    const int raw = open_devnull();
    util::unique_fd fd{raw};

    // reset(release()) without a self-move guard would close the descriptor
    // and then store it back, leaving the object holding a closed fd.
    auto& alias = fd;
    fd = std::move(alias);

    REQUIRE(fd.get() == raw);
    REQUIRE(is_open(raw));
}

TEST_CASE("unique_fd release gives up ownership without closing",
          "[unique_fd]") {
    const int raw = open_devnull();
    {
        util::unique_fd fd{raw};
        REQUIRE(fd.release() == raw);
        REQUIRE(!fd);
    }
    REQUIRE(is_open(raw));
    ::close(raw);
}
