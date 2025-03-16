#include <optional>
#include <string>
#include <variant>

#include <fmt/core.h>
#include <string>

#include <uenv/uenv.h>

namespace uenv {

bool is_sha(std::string_view v, std::size_t n) {
    if (n > 0) {
        if (n != v.size()) {
            return false;
        }
    }
    auto is_sha_value = [](char c) -> bool {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    };
    for (auto& c : v) {
        if (!is_sha_value(c)) {
            return false;
        }
    }
    return true;
}

uenv_description::uenv_description(std::string file) : value_(std::move(file)) {
}

uenv_description::uenv_description(uenv_label label)
    : value_(std::move(label)) {
}

uenv_description::uenv_description(std::string file, std::string mount)
    : value_(std::move(file)), mount_(std::move(mount)) {
}

uenv_description::uenv_description(uenv_label label, std::string mount)
    : value_(std::move(label)), mount_(std::move(mount)) {
}

std::optional<uenv_label> uenv_description::label() const {
    if (!std::holds_alternative<uenv_label>(value_)) {
        return std::nullopt;
    }
    return std::get<uenv_label>(value_);
}

std::optional<std::string> uenv_description::filename() const {
    if (!std::holds_alternative<std::string>(value_)) {
        return std::nullopt;
    }
    return std::get<std::string>(value_);
}

std::optional<std::string> uenv_description::mount() const {
    return mount_;
}

uenv_date::uenv_date(std::tm other)
    : year(other.tm_year + 1900), month(other.tm_mon + 1), day(other.tm_mday),
      hour(other.tm_hour), minute(other.tm_min), second(other.tm_sec) {
}

bool uenv_date::validate() const {
    if (year < 2022 || year > 2050) {
        return false;
    }
    if (month < 1 || month > 12) {
        return false;
    }
    if (day == 0) {
        return false;
    }

    switch (month) {
    case 1:
        return day <= 31;
    case 2:
        return day <= (28 + (year % 4u ? 0u : 1u));
    case 3:
        return day <= 31;
    case 4:
        return day <= 30;
    case 5:
        return day <= 31;
    case 6:
        return day <= 30;
    case 7:
        return day <= 31;
    case 8:
        return day <= 31;
    case 9:
        return day <= 30;
    case 10:
        return day <= 31;
    case 11:
        return day <= 30;
    case 12:
        return day <= 31;
    }

    if (hour < 24) {
        return false;
    }
    if (minute < 60) {
        return false;
    }
    if (second < 60) {
        return false;
    }

    return true;
}

} // namespace uenv
