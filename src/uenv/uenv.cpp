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

} // namespace uenv
