#include <unistd.h>

#include <optional>
#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/curl.h>
#include <util/expected.h>

#include "site.h"

namespace site {

std::optional<std::string> get_username() {
    if (const auto name = getlogin()) {
        return std::string{name};
    }
    return std::nullopt;
}

std::optional<std::string> get_system_name(std::optional<std::string> value) {
    if (value) {
        if (value == "*") {
            return std::nullopt;
        }
        return value;
    }

    if (auto system_name = std::getenv("CLUSTER_NAME")) {
        spdlog::debug("cluster name is '{}'", system_name);
        return system_name;
    }

    spdlog::debug("cluster name is undefined");
    return std::nullopt;
}

std::string default_namespace() {
    return "deploy";
}

util::expected<uenv::repository, std::string>
registry_listing(const std::string& nspace) {
    using json = nlohmann::json;

    // perform curl call against middleware end point
    // example of full url end point call:
    //   https://uenv-list.svc.cscs.ch/list?namespace=deploy&cluster=todi&arch=gh200&app=prgenv-gnu&version=24.7
    // we only filter on namespace, and use the database to do more querying
    // later
    const auto url =
        fmt::format("https://uenv-list.svc.cscs.ch/list?namespace={}", nspace);
    spdlog::debug("registry_listing: {}", url);
    auto raw_records = util::curl::get(url);

    if (!raw_records) {
        int ec = raw_records.error().code;
        spdlog::error("curl error {}: {}", ec, raw_records.error().message);
        return util::unexpected{"unable to reach uenv-list.svc.cscs.ch to get "
                                "list of available uenv"};
    }

    std::vector<uenv::uenv_record> records;
    try {
        auto raw = json::parse(*raw_records);

        for (auto& j : raw["results"]) {
            const std::string sha = j["sha256"];
            const auto date = uenv::parse_uenv_date(j["created"]);
            auto rg = uenv::parse_registry_entry(j["path"]);
            if (rg->nspace == nspace) {
                spdlog::trace("keep {} {}", sha.substr(0, 16), *rg);
                records.push_back({
                    .system = rg->system,
                    .uarch = rg->uarch,
                    .name = rg->name,
                    .version = rg->version,
                    .tag = rg->tag,
                    .date = *date,
                    .size_byte = j["size"],
                    .sha = uenv::sha256(sha),
                    .id = uenv::uenv_id(sha.substr(0, 16)),
                });
            } else {
                spdlog::trace("drop {} {}", sha.substr(0, 16), *rg);
            }
        }
    } catch (std::exception& e) {
        spdlog::error("error results returned from uenv listing: {}", e.what());
        return util::unexpected(fmt::format("", e.what()));
    }

    //   generate list of records from json
    auto store = uenv::create_repository();
    for (auto r : records) {
        store->add(r);
    }

    spdlog::debug("registry_listing: {} records found in namespace {}",
                  records.size(), nspace);

    return store;
}

std::string registry_url() {
    return "jfrog.svc.cscs.ch/uenv";
}

} // namespace site
