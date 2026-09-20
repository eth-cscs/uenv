#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include <util/detach.h>
#include <util/fs.h>

namespace {

// wait up to `timeout` for `path` to exist
bool wait_for(const std::filesystem::path& path,
              std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::exists(path)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

// whether the process `pid` is running: a zombie is not, which the orphan
// stays in a container whose init does not reap it
bool alive(pid_t pid) {
    if (::kill(pid, 0) != 0) {
        return false;
    }
    std::ifstream stat(fmt::format("/proc/{}/stat", pid));
    std::string line;
    std::getline(stat, line);
    const auto state = line.rfind(')');
    return state == std::string::npos || state + 2 >= line.size() ||
           line[state + 2] != 'Z';
}

} // namespace

TEST_CASE("spawn_detached", "[detach]") {
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;
    const auto dir = *util::make_temp_dir();

    SECTION("does not wait for the work") {
        const auto start = std::chrono::steady_clock::now();
        util::spawn_detached(
            [&] {
                std::this_thread::sleep_for(1s);
                std::ofstream(dir / "done") << "done";
            },
            10s);
        REQUIRE(std::chrono::steady_clock::now() - start < 500ms);
        REQUIRE(!fs::exists(dir / "done"));
        REQUIRE(wait_for(dir / "done", 5s));
    }
    SECTION("holds none of the caller's descriptors") {
        // the read end of a pipe sees EOF once every write end is closed,
        // as a shell reading the output of $(...) does
        int fds[2];
        REQUIRE(::pipe(fds) == 0);
        util::spawn_detached(
            [&] {
                std::this_thread::sleep_for(2s);
                std::ofstream(dir / "done") << "done";
            },
            10s);
        ::close(fds[1]);
        const auto start = std::chrono::steady_clock::now();
        char c;
        REQUIRE(::read(fds[0], &c, 1) == 0);
        REQUIRE(std::chrono::steady_clock::now() - start < 500ms);
        ::close(fds[0]);
        REQUIRE(wait_for(dir / "done", 5s));
    }
    SECTION("is killed after the limit") {
        util::spawn_detached(
            [&] {
                std::ofstream(dir / "pid") << ::getpid();
                // SIGALRM is not delivered while blocked in the caller
                ::pause();
                std::ofstream(dir / "done") << "done";
            },
            1s);
        REQUIRE(wait_for(dir / "pid", 5s));
        pid_t pid = 0;
        // the file can be seen before it has been written
        for (int i = 0; i < 100 && pid == 0; ++i) {
            std::ifstream(dir / "pid") >> pid;
            std::this_thread::sleep_for(10ms);
        }
        REQUIRE(pid > 0);
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (alive(pid) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(50ms);
        }
        REQUIRE(!alive(pid));
        REQUIRE(!fs::exists(dir / "done"));
    }

    fs::remove_all(dir);
}
