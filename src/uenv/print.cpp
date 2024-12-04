#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

#include <uenv/repository.h>
#include <uenv/uenv.h>
#include <util/color.h>

namespace uenv {

std::string format_record_set(const record_set& records, bool no_header) {
    if (records.empty()) {
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

void print_record_set(const record_set& records, bool no_header) {
    fmt::print("{}", format_record_set(records, no_header));
}

} // namespace uenv
