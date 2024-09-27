#include <string>

#include "fmt/format.h"

#include "color.h"
#include "help.h"

namespace help {

block::block(std::string msg) : kind(none), lines{std::move(msg)} {
}

std::string render(const linebreak&) {
    return "";
}

std::string render(const block& b) {
    using enum help::block::admonition;
    std::string result{};
    switch (b.kind) {
    case none:
    case code:
        break;
    case note:
        result += fmt::format("{} - ", ::color::cyan("Note"));
        break;
    case xmpl:
        result += fmt::format("{} - ", ::color::blue("Example"));
        break;
    case info:
        result += fmt::format("{} - ", ::color::green("Info"));
        break;
    case warn:
        result += fmt::format("{} - ", ::color::red("Warning"));
        break;
    case depr:
        result += fmt::format("{} - ", ::color::red("Deprecated"));
    }
    bool first = true;
    for (auto& l : b.lines) {
        if (!first) {
            result += "\n";
        }
        if (b.kind == code) {
            result += fmt::format("  {}", ::color::white(l));
        } else {
            result += l;
        }
        first = false;
    }

    return result;
}

} // namespace help
