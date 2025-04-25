#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include <util/expected.h>
#include <util/subprocess.h>

#include <fmt/core.h>
#include <fmt/ranges.h>

namespace util {

expected<pipe, int> make_pipe() {
    pipe p;
    if (auto rval = ::pipe(p.state); rval == -1) {
        return unexpected(rval);
    }
    return p;
}

expected<subprocess, std::string>
run(const std::vector<std::string>& argv,
    std::optional<std::filesystem::path> runpath) {
    // WARNING: NEVER add logging of the call and its arguments here.
    // The arguments may contain sensitive information like passwords, and
    // we rely on the caller printing redacted logs one level up.
    if (argv.empty()) {
        return unexpected("need at least one argument");
    }

    // TODO: error handling
    pipe inpipe = *make_pipe();
    pipe outpipe = *make_pipe();
    pipe errpipe = *make_pipe();

    auto pid = ::fork();

    if (pid == 0) {
        // TODO: error handling
        ::dup2(outpipe.write(), STDOUT_FILENO);
        ::dup2(errpipe.write(), STDERR_FILENO);
        ::dup2(inpipe.read(), STDIN_FILENO);

        outpipe.close();
        errpipe.close();
        inpipe.close();

        // child(argv);
        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (auto& arg : argv) {
            args.push_back(const_cast<char*>(arg.data()));
        }
        args.push_back(nullptr);

        if (runpath) {
            if (std::filesystem::is_directory(*runpath)) {
                std::filesystem::current_path(*runpath);
            } else {
                return util::unexpected{fmt::format(
                    "the run path {} does not exist", runpath->string())};
            }
        }
        execvp(args[0], &args[0]);

        // this code only executes if the attempt to launch the subprocess
        // fails to launch.
        std::perror(
            fmt::format("subprocess error running '{}'", fmt::join(argv, " "))
                .c_str());
        exit(1);
    }

    outpipe.close_write();
    errpipe.close_write();
    inpipe.close_read();

    return subprocess{outpipe, errpipe, inpipe, pid};
}

void subprocess::setrcode(int status) {
    if (WIFEXITED(status)) {
        rcode_ = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        rcode_ = WTERMSIG(status);
    } else {
        rcode_ = 255;
    }
}

int subprocess::wait() {
    if (!finished_) {
        int status = 0;
        waitpid(pid, &status, 0);
        finished_ = true;
        setrcode(status);
    }
    return *rcode_;
}

bool subprocess::finished() {
    if (finished_) {
        return true;
    }

    int status;
    auto rc = waitpid(pid, &status, WNOHANG);
    if (rc == 0) {
        return false;
    }

    wait();

    return true;
}

void subprocess::kill(int signal) {
    if (!finished_) {
        ::kill(pid, signal);
        wait();
    }
    finished_ = true;
}

int subprocess::rvalue() {
    if (!finished_) {
        return wait();
    }
    return *rcode_;
}

std::istream& buffered_istream::stream() {
    return *stream_;
}

std::string buffered_istream::string() {
    return {std::istreambuf_iterator<char>(*stream_), {}};
}

std::optional<std::string> buffered_istream::getline() {
    if (std::string line; std::getline(stream(), line)) {
        return line;
    }
    return {};
}

std::ostream& buffered_ostream::stream() {
    return *stream_;
}

void buffered_ostream::putline(std::string_view line) {
    stream() << line << std::endl;
}

} // namespace util
