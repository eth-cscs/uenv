#include <fmt/format.h>

// Verify that @p x is true (non-zero); otherwise, return unexpected along with
// and fmt format string in the second argument
#define Tf_(x, fmts, ...)                                                      \
    do {                                                                       \
        if (!(x)) {                                                            \
            return util::unexpected(                                           \
                fmt::format(fmt::runtime(std::string("{}:{} ") + fmts),        \
                            __FILE__, __LINE__, ##__VA_ARGS__));               \
        }                                                                      \
    } while (0)

// Verify that @p x is true (non-zero); otherwise, return unexpected
#define T__(x)                                                                 \
    do {                                                                       \
        if (!(x)) {                                                            \
            return util::unexpected(fmt::format(                               \
                "Assertion {} failed in {}:{}", #x, __FILE__, __LINE__));      \
        }                                                                      \
    } while (0)

// Verify that @p x is true (non-zero); otherwise, return unexpected with a
// generic error message followed by errno and its stringified form.
#define T_e(x)                                                                 \
    do {                                                                       \
        if (!(x)) {                                                            \
            return util::unexpected(                                           \
                fmt::format("{} failed in {}:{}, strerror(errno): {}", #x,     \
                            __FILE__, __LINE__, strerror(errno)));             \
        }                                                                      \
    } while (0)

// Verify that @p x is true (non-zero); otherwise,return unexpected with an
// error message specified by a fmt format string in the second argument
// along with appropriate additional arguments, followed by errno and its
// stringified form. */
#define Tfe(x, fmts, ...)                                                      \
    do {                                                                       \
        if (!(x)) {                                                            \
            return util::unexpected(fmt::format(                               \
                fmt::runtime(std::string("{} failed in {}:{} ") + fmts), #x,   \
                __FILE__, __LINE__, errno, ##__VA_ARGS__));                    \
        }                                                                      \
    } while (0)

// Verify that @p x is zero (false); otherwise, return unexpected with a
// generic error message.
#define Z__(x)                                                                 \
    do {                                                                       \
        if (x) {                                                               \
            return util::unexpected(                                           \
                fmt::format("{} failed in {}:{}", #x, __FILE__, __LINE__));    \
        }                                                                      \
    } while (0)

// Verify that @p x is zero (false); otherwise, exit with a generic error
// message followed by errno and its stringified form.
#define Z_e(x)                                                                 \
    do {                                                                       \
        if (x) {                                                               \
            return util::unexpected(                                           \
                fmt::format("{} failed in {}:{} strerror(errno): {}", #x,      \
                            __FILE__, __LINE__, strerror(errno)));             \
        }                                                                      \
    } while (0)

// Verify that @p x is zero (false); otherwise, exit with an error message
// specified by a `printf(3)` format string in the second argument along with
// appropriate additional arguments, followed by errno and its stringified
// form. */
#define Zfe(x, fmts, ...)                                                      \
    do {                                                                       \
        if (x) {                                                               \
            return util::unexpected(fmt::format(                               \
                fmt::runtime(std::string("{}:{} errno: {} ") + fmts),          \
                __FILE__, __LINE__, errno, ##__VA_ARGS__));                    \
        }                                                                      \
    } while (0)
