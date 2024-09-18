#include <string>

#include "fmt/format.h"

#include "color.h"
#include "help.h"

namespace help {

std::string render(const lst& l) {
    return fmt::format("{}", ::color::white(l.content));
}

std::string render(const block& b) {
    using enum help::block::admonition;
    std::string result{};
    switch (b.kind) {
    case none:
        break;
    case note:
        result += fmt::format("{} - ", ::color::cyan("note"));
        break;
    case info:
        result += fmt::format("{} - ", ::color::bright_green("info"));
        break;
    case warn:
        result += fmt::format("{} - ", ::color::bright_red("warning"));
        break;
    case deprecated:
        result += fmt::format("{} - ", ::color::bright_red("deprecated"));
    }
    bool first = true;
    for (auto& l : b.lines) {
        if (!first) {
            result += "\n";
        }
        result += l;
        first = false;
    }

    return result;
}

std::string render(const example& e) {
    using namespace color;
    auto result = fmt::format("{} - ", blue("Example"));
    bool first = true;
    for (auto& l : e.description) {
        if (!first) {
            result += "\n";
        }
        result += l;
        first = false;
    }
    result += ":";
    for (auto& l : e.code) {
        result += fmt::format("\n  {}", white(l));
    }
    if (!e.blocks.empty()) {
        result += "\n";
    }
    for (auto& b : e.blocks) {
        result += render(b);
    }

    return result;
}

} // namespace help
