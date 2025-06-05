#include "modular_env.hpp"
#include <tuple>
#include <util/expected.h>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>
#include <uenv/repository.h>

namespace uenv {

util::expected<modular_env_paths, std::string>
read_modular_env(const std::filesystem::path& modular_uenv_json_path,
                       std::optional<std::filesystem::path> repo_arg) {

    using json = nlohmann::json;
    std::ifstream f(modular_uenv_json_path.c_str());
    json data;
    try {
        data = json::parse(f);
    } catch (std::exception& e) {
        return util::unexpected(
            fmt::format("error modular uenv json file {}: {}",
                        modular_uenv_json_path.string(), e.what()));
    }

    if (!data.contains("root")) {
        return util::unexpected(
            fmt::format("error {} doesn't specify the root-image",
                        modular_uenv_json_path.string()));
    }

    auto store = uenv::open_repository(*repo_arg);
    if (!store) {
        return util::unexpected(
            fmt::format("unable to open repo: {}", store.error()));
    }

    std::vector<std::tuple<std::filesystem::path, std::filesystem::path>>
        sub_images;
    std::filesystem::path sqfs_path =
        data["root"]["image"]["file"].get<std::string>();
    std::filesystem::path mount_path =
        data["root"]["image"]["prefix_path"].get<std::string>();

    // GPU image if present
    if (data.contains("gpu")) {
        std::filesystem::path image = data["gpu"]["image"]["file"];
        std::filesystem::path mount = data["gpu"]["image"]["prefix_path"];
        sub_images.push_back(std::make_tuple(image, mount));
    }

    if (data.contains("compilers")) {
      for(auto entry: data["compilers"]) {
        auto mount = entry["image"]["prefix_path"];
        auto sqfs = entry["image"]["file"];
        sub_images.push_back(std::make_tuple(sqfs,mount));
      }
    }

    return modular_env_paths{
      .sqfs_path = sqfs_path,
      .mount_path = mount_path,
      .sub_images = sub_images};
}

} // namespace uenv
