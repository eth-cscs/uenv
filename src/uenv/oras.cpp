// #include <filesystem>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <uenv/oras.h>
#include <uenv/uenv.h>
#include <util/fs.h>
#include <util/subprocess.h>

namespace uenv {
namespace oras {

// namespace fs = std::filesystem;

struct oras_output {
    int rcode = -1;
    std::string stdout;
    std::string stderr;
};

oras_output run_oras(std::vector<std::string> args) {
    auto oras = util::oras_path();
    if (!oras) {
        spdlog::error("run_oras: no oras executable found");
        return {.rcode = -1};
    }

    args.insert(args.begin(), oras->string());
    spdlog::trace("run_oras: {}", fmt::join(args, " "));
    auto proc = util::run(args);

    auto result = proc->wait();

    return {.rcode = result,
            .stdout = proc->out.string(),
            .stderr = proc->err.string()};
}

util::expected<std::vector<std::string>, std::string>
discover(const std::string& registry, const std::string& nspace,
         const uenv_record& uenv) {
    auto address =
        fmt::format("{}/{}/{}/{}/{}/{}:{}", registry, nspace, uenv.system,
                    uenv.uarch, uenv.name, uenv.version, uenv.tag);

    auto result = run_oras({"discover", "--format", "json", "--artifact-type",
                            "uenv/meta", address});

    if (result.rcode) {
        spdlog::error("oras discover {}: {}", result.rcode, result.stderr);
        return util::unexpected{result.stderr};
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
        return util::unexpected(fmt::format("", e.what()));
    }

    return manifests;
}

int pull_digest(const std::string& registry, const std::string& nspace, const uenv_record& uenv, const std::string& digest, const std::filesystem::path& destination) {
    auto address =
        fmt::format("{}/{}/{}/{}/{}/{}@{}", registry, nspace, uenv.system,
                    uenv.uarch, uenv.name, uenv.version, digest);

    spdlog::debug("oras::pull_digest: {}", address);

    //proc = run_command_internal(["pull", "-o", image_path,
    //        f"{source_address}@{digest}"])
    auto proc = run_oras({"pull", "--output", destination.string(), address});

    return 0;
}

auto run_oras_async() {
}

bool pull(std::string rego, std::string nspace);
} // namespace oras
} // namespace uenv
