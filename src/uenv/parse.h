#pragma once

#include <string>
#include <vector>

#include <util/expected.h>

#include <uenv/elastic.h>
#include <uenv/mount.h>
#include <uenv/settings.h>
#include <uenv/uenv.h>
#include <uenv/view.h>
#include <util/lex.h>
#include <util/parse.h>
#include <util/semver.h>

namespace uenv {

// the parsing scaffolding (parse_error, the PARSE macro, parse_string) lives in
// util so that it can be shared with other parsers (e.g. src/oci). uenv keeps
// the familiar unqualified name via this alias.
using parse_error = util::parse_error;

util::expected<std::vector<view_description>, parse_error>
parse_view_args(const std::string& arg);

util::expected<std::vector<env_view_description>, parse_error>
parse_env_view_description(const std::string& arg);

util::expected<std::vector<uenv_description>, parse_error>
parse_uenv_args(const std::string& arg);

util::expected<std::vector<mount_description>, parse_error>
parse_mount_list(const std::string& arg);

util::expected<uenv_date, parse_error> parse_uenv_date(const std::string& arg);

util::expected<std::string, parse_error> parse_path(const std::string& in);

util::expected<uenv_label, parse_error> parse_uenv_label(const std::string& in);

util::expected<uenv_description, parse_error>
parse_uenv_description(const std::string& in);

util::expected<uenv_nslabel, parse_error>
parse_uenv_nslabel(const std::string& in);

util::expected<uenv_registry_entry, parse_error>
parse_registry_entry(const std::string& in);

// TODO: remove as soon as the old config file format is dropped
util::expected<config_line, parse_error>
parse_config_line(const std::string& arg);

util::expected<std::optional<std::string>, parse_error>
parse_cluster_name(const std::string& in);

util::expected<std::string, parse_error>
parse_xthostname(const std::string& in);

util::expected<std::string, parse_error> parse_repo_name(const std::string& in);

util::expected<std::vector<repo_label>, parse_error>
parse_repo_list(const std::string& in);

util::expected<repo_label, parse_error> parse_repo_label(const std::string& in);

util::expected<util::semver, parse_error> parse_semver(const std::string& arg);

} // namespace uenv
