// #include <filesystem>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <uenv/oras.h>
#include <util/fs.h>
#include <util/subprocess.h>

namespace uenv {
namespace oras {

// namespace fs = std::filesystem;

int run_oras(std::vector<std::string> args) {
    auto oras = util::oras_path();
    if (!oras) {
        spdlog::error("run_oras: no oras executable found");
        return 1;
    }

    args.insert(args.begin(), oras->string());
    auto proc = util::run(args);

    auto result = proc->wait();

    return result;
}

auto run_oras_async() {
}

bool pull(std::string rego, std::string nspace);
} // namespace oras
} // namespace uenv
