#include <string>
#include <string_view>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>

#include <uenv/print.h>
#include <uenv/repository.h>
#include <uenv/uenv.h>
#include <util/color.h>

namespace uenv {

util::expected<record_set_format, std::string>
get_record_set_format(bool no_header, bool json, bool list) {
    if (json && list) {
        return util::unexpected(
            "the --json and --list options are incompatible and can not be "
            "used at the same time");
    }

    if (!json && !list) {
        return no_header ? record_set_format::table_no_header
                         : record_set_format::table;
    }

    return json ? record_set_format::json : record_set_format::list;
}

std::string format_record_set_table(const record_set& records, bool no_header) {
    if (!no_header && records.empty()) {
        if (!no_header) {
            return "no matching uenv\n";
        }
        return "";
    }

    auto size_string = [](std::size_t size, unsigned width = 0) {
        auto size_mb = size / (1024 * 1024);
        return fmt::format(std::locale("en_US.UTF-8"), "{:>{}L}", size_mb,
                           width);
    };
    // print the records
    std::size_t w_name = std::string_view("uenv").size();
    std::size_t w_sys = std::string_view("system").size();
    std::size_t w_arch = std::string_view("arch").size();
    std::size_t w_size = std::string_view("size(MB)").size();
    std::size_t w_date = std::string_view("yyyy/mm/dd").size();
    std::size_t w_id = 16;

    for (auto& r : records) {
        w_name = std::max(w_name,
                          r.name.size() + r.version.size() + r.tag.size() + 2);
        w_sys = std::max(w_sys, r.system.size());
        w_arch = std::max(w_arch, r.uarch.size());
        w_size = std::max(w_size, size_string(r.size_byte).size());
    }
    w_name += 2;
    w_sys += 2;
    w_arch += 2;
    w_size += 2;
    w_date += 2;
    w_id += 2;
    std::string result;
    if (!no_header) {
        auto header =
            fmt::format("{:<{}}{:<{}}{:<{}}{:<{}}{:<{}}{:<{}}\n", "uenv",
                        w_name, "arch", w_arch, "system", w_sys, "id", w_id,
                        "size(MB)", w_size, "date", w_date);
        result += fmt::format("{}", color::yellow(header));
    }
    for (auto& r : records) {
        auto name = fmt::format("{}/{}:{}", r.name, r.version, r.tag);
        result +=
            fmt::format("{:<{}}{:<{}}{:<{}}{:<{}}{:<{}}{:s}\n", name, w_name,
                        r.uarch, w_arch, r.system, w_sys, r.id.string(), w_id,
                        size_string(r.size_byte, 6), w_size, r.date);
    }

    return result;
}

std::string format_record_set_list(const record_set& records) {
    std::string result;
    for (auto& r : records) {
        result += fmt::format("{}/{}:{}@{}%{}\n", r.name, r.version, r.tag,
                              r.system, r.uarch);
    }

    return result;
}

std::string format_record_set_format(const record_set& records,
                                     std::string_view fmtstring) {
    std::string result;
    for (auto& r : records) {
        // clang-format off
        result += fmt::format(
            fmt::runtime(fmtstring),
            fmt::arg("name",    r.name),
            fmt::arg("version", r.version),
            fmt::arg("tag",     r.tag),
            fmt::arg("system",  r.system),
            fmt::arg("uarch",   r.uarch),
            fmt::arg("id",      r.id),
            fmt::arg("digest",  r.sha),
            fmt::arg("size",    r.size_byte),
            fmt::arg("date",    r.date)
        );
        // clang-format on
        result += "\n";
    }

    return result;
}

std::string format_record_set_json(const record_set& records) {
    using nlohmann::json;
    std::vector<json> jrecords;
    for (auto& r : records) {
        jrecords.push_back(json{
            {"name", r.name},
            {"version", r.version},
            {"tag", r.tag},
            {"system", r.system},
            {"uarch", r.uarch},
            {"id", r.id.string()},
            {"digest", r.sha.string()},
            {"size", r.size_byte},
            {"date", fmt::format("{}", r.date)},
        });
    }
    return json{{"records", jrecords}}.dump();
}

void print_record_set(const record_set& records, record_set_format f,
                      std::string_view fmtstring) {
    switch (f) {
    case record_set_format::json:
        fmt::print("{}", format_record_set_json(records));
        return;
    case record_set_format::list:
        fmt::print("{}", format_record_set_format(records, fmtstring));
        return;
    case record_set_format::table:
        fmt::print("{}", format_record_set_table(records, false));
        return;
    case record_set_format::table_no_header:
        fmt::print("{}", format_record_set_table(records, true));
        return;
    }
}

} // namespace uenv
