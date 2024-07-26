#pragma once

#include <expected>
#include <filesystem>
#include <vector>

#include <uenv/uenv.h>
// #include <util/expected.h>

namespace uenv {

// PIMPL forward declaration
struct datastore;

std::expected<datastore, std::string>
open_datastore(const std::filesystem::path&);

struct datastore_impl;
struct datastore {
  private:
    std::unique_ptr<datastore_impl> impl_;

  public:
    datastore(datastore&&);
    datastore(std::unique_ptr<datastore_impl>);

    // copying a datastore is not permitted
    datastore() = delete;
    datastore(const datastore&) = delete;

    const std::filesystem::path& path() const;
    const std::filesystem::path& db_path() const;

    std::expected<std::vector<uenv_record>, std::string>
    query(const uenv_label& label) const;

    ~datastore();

    friend std::expected<datastore, std::string>
    open_datastore(const std::filesystem::path&);
};

} // namespace uenv
