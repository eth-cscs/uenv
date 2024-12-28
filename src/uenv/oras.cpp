#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include <barkeep/barkeep.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <uenv/oras.h>
#include <uenv/uenv.h>
#include <util/color.h>
#include <util/fs.h>
#include <util/signal.h>
#include <util/subprocess.h>

namespace uenv {
namespace oras {

using opt_creds = std::optional<credentials>;

// stores the "result" of calling the oras cli tool as an external process.
struct oras_output {
    // return code of the oras call
    int returncode = -1;

    // the full captured stdout output
    std::string stdout;

    // the full captured stderr output
    std::string stderr;
};

constexpr auto generic_error_message =
    "unknown error - rerun with the -vvv flag and send an error report to the "
    "CSCS service desk.";

error generic_error(std::string err) {
    return {-1, std::move(err), generic_error_message};
}

// Convert oras_output to a uenv::oras::error.
// oras returns 1 for all errors (from what we observe - there is no
// documentation).
// Hence, parse stderr & stdout for hints about what went wrong to generate the
// user-friendly message
error create_error(const oras_output& result) {
    if (result.returncode == 0) {
        return {0, {}, {}};
    }
    std::string message = generic_error_message;

    auto contains = [](std::string_view string,
                       std::string_view contents) -> bool {
        return string.find(contents) != std::string_view::npos;
    };
    std::string_view err = result.stderr;
    if (contains(err, "403") && contains(err, "Forbidden")) {
        return {
            403, result.stderr,
            "Invalid credentials were provided, or you may not have permission "
            "to perform the requested action.\nTry using the --token flag if "
            "you are trying to access restricted software.\nCSCS staff can "
            "configure oras to use their credentials."};
    }
    return {result.returncode, result.stderr, std::move(message)};
};

std::vector<std::string>
redact_arguments(const std::vector<std::string>& args) {
    std::vector<std::string> redacted_args{};
    bool redact_next = false;
    for (auto arg : args) {
        if (redact_next) {
            redacted_args.push_back(std::string(arg.size(), 'X'));
            redact_next = false;
        } else if (arg.find("password") != std::string::npos) {
            if (auto eqpos = arg.find("="); eqpos != std::string::npos) {
                redacted_args.push_back(
                    std::string(arg.begin(), arg.begin() + eqpos + 1) +
                    std::string(arg.size() - eqpos, 'X'));
            } else {
                redacted_args.push_back(arg);
                redact_next = true;
            }
        } else {
            redacted_args.push_back(arg);
        }
    }

    return redacted_args;
}

oras_output run_oras(std::vector<std::string> args) {
    auto oras = util::oras_path();
    if (!oras) {
        spdlog::error("no oras executable found");
        return {.returncode = -1};
    }

    args.insert(args.begin(), oras->string());
    spdlog::trace("run_oras: {}", fmt::join(redact_arguments(args), " "));
    auto proc = util::run(args);

    auto result = proc->wait();

    return {.returncode = result,
            .stdout = proc->out.string(),
            .stderr = proc->err.string()};
}

util::expected<util::subprocess, std::string>
run_oras_async(std::vector<std::string> args) {
    auto oras = util::oras_path();
    if (!oras) {
        return util::unexpected{"no oras executable found"};
    }

    args.insert(args.begin(), oras->string());
    spdlog::trace("run_oras: {}", fmt::join(args, " "));
    return util::run(args);
}

util::expected<std::vector<std::string>, error>
discover(const std::string& registry, const std::string& nspace,
         const uenv_record& uenv, const opt_creds token) {
    auto address =
        fmt::format("{}/{}/{}/{}/{}/{}:{}", registry, nspace, uenv.system,
                    uenv.uarch, uenv.name, uenv.version, uenv.tag);

    std::vector<std::string> args = {"discover",        "--format",  "json",
                                     "--artifact-type", "uenv/meta", address};
    if (token) {
        args.push_back("--password");
        args.push_back(token->token);
        args.push_back("--username");
        args.push_back(token->username);
    }
    auto result = run_oras(args);

    if (result.returncode) {
        spdlog::error("oras discover returncode={} stderr='{}'",
                      result.returncode, result.stderr);
        return util::unexpected{create_error(result)};
    }

    std::vector<std::string> manifests;
    using json = nlohmann::json;
    try {
        const auto raw = json::parse(result.stdout);
        for (const auto& j : raw["manifests"]) {
            manifests.push_back(j["digest"]);
        }
    } catch (std::exception& e) {
        spdlog::error("unable to parse oras discover json: {}", e.what());
        return util::unexpected(generic_error(e.what()));
    }

    return manifests;
}

util::expected<void, error>
pull_digest(const std::string& registry, const std::string& nspace,
            const uenv_record& uenv, const std::string& digest,
            const std::filesystem::path& destination, const opt_creds token) {
    auto address =
        fmt::format("{}/{}/{}/{}/{}/{}@{}", registry, nspace, uenv.system,
                    uenv.uarch, uenv.name, uenv.version, digest);

    spdlog::debug("oras::pull_digest: {}", address);

    std::vector<std::string> args{"pull", "--output", destination.string(),
                                  address};
    if (token) {
        args.push_back("--password");
        args.push_back(token->token);
        args.push_back("--username");
        args.push_back(token->username);
    }
    auto proc = run_oras(args);

    if (proc.returncode) {
        spdlog::error("unable to pull digest with oras: {}", proc.stderr);
        return util::unexpected{create_error(proc)};
    }

    return {};
}

util::expected<void, error> pull_tag(const std::string& registry,
                                     const std::string& nspace,
                                     const uenv_record& uenv,
                                     const std::filesystem::path& destination,
                                     const opt_creds token) {
    using namespace std::chrono_literals;
    namespace fs = std::filesystem;
    namespace bk = barkeep;

    auto address =
        fmt::format("{}/{}/{}/{}/{}/{}:{}", registry, nspace, uenv.system,
                    uenv.uarch, uenv.name, uenv.version, uenv.tag);

    spdlog::debug("oras::pull_tag: {}", address);
    std::vector<std::string> args{"pull",     "--concurrency",      "10",
                                  "--output", destination.string(), address};
    if (token) {
        args.push_back("--password");
        args.push_back(token->token);
        args.push_back("--username");
        args.push_back(token->username);
    }
    auto proc = run_oras_async(args);

    if (!proc) {
        spdlog::error("unable to pull tag with oras: {}", proc.error());
        return util::unexpected{generic_error(proc.error())};
    }

    const fs::path& sqfs = destination / "store.squashfs";

    std::size_t downloaded_mb{0u};
    std::size_t total_mb{uenv.size_byte / (1024 * 1024)};
    auto bar = bk::ProgressBar(
        &downloaded_mb,
        {
            .total = total_mb,
            .message = fmt::format("pulling {}", uenv.id.string()),
            .speed = 1.,
            .speed_unit = "MB/s",
            .style = color::use_color() ? bk::ProgressBarStyle::Rich
                                        : bk::ProgressBarStyle::Bars,
            .no_tty = !isatty(fileno(stdout)),
        });

    util::set_signal_catcher();
    while (!proc->finished()) {
        std::this_thread::sleep_for(100ms);
        // handle a signal, usually SIGTERM or SIGINT
        if (util::signal_raised()) {
            spdlog::error("signal raised - interrupting download");
            proc->kill();
            throw util::signal_exception(util::last_signal_raised());
        }
        if (fs::is_regular_file(sqfs)) {
            auto downloaded_bytes = fs::file_size(sqfs);
            downloaded_mb = downloaded_bytes / (1024 * 1024);
        }
    }
    downloaded_mb = total_mb;

    if (proc->rvalue()) {
        spdlog::error("unable to pull tag with oras: {}", proc->err.string());
        return util::unexpected{create_error({.returncode = proc->rvalue(),
                                              .stdout = proc->out.string(),
                                              .stderr = proc->err.string()})};
    }

    return {};
}

util::expected<void, error>
copy(const std::string& registry, const std::string& src_nspace,
     const uenv_record& src_uenv, const std::string& dst_nspace,
     const uenv_record& dst_uenv, const std::optional<credentials> token) {

    auto address = [&registry](auto& nspace, auto& record) -> std::string {
        return fmt::format("{}/{}/{}/{}/{}/{}:{}", registry, nspace,
                           record.system, record.uarch, record.name,
                           record.version, record.tag);
    };
    const auto src_url = address(src_nspace, src_uenv);
    const auto dst_url = address(dst_nspace, dst_uenv);

    std::vector<std::string> args = {"cp",          "--concurrency", "10",
                                     "--recursive", src_url,         dst_url};
    if (token) {
        args.push_back(fmt::format("--from-password={}", token->token));
        args.push_back(fmt::format("--from-username={}", token->username));
        args.push_back(fmt::format("--to-password={}", token->token));
        args.push_back(fmt::format("--to-username={}", token->username));
    }
    auto result = run_oras(args);

    if (result.returncode) {
        spdlog::error("oras cp {}: {}", result.returncode, result.stderr);
        return util::unexpected{create_error(result)};
    }

    return {};
}

} // namespace oras
} // namespace uenv
