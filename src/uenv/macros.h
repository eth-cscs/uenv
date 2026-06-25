#include <spdlog/spdlog.h>

#include <sstream>

/** Verify that @p x is true (non-zero); otherwise, exit with an error message
    specified by a `printf(3)` format string in the second argument along with
    appropriate additional arguments. */
#define Tf_(x, fmts, ...)                                                      \
    do {                                                                       \
        if (!(x)) {                                                            \
            spdlog::error(fmt::runtime(std::string("{}:{} ") + fmts),          \
                          __FILE__, __LINE__, ##__VA_ARGS__);                  \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/** Verify that @p x is true (non-zero); otherwise, exit with a generic error
    message. */
#define T__(x)                                                                 \
    do {                                                                       \
        if (!(x)) {                                                            \
            spdlog::error("{}:{} assertion failed", __FILE__, __LINE__);       \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/** Verify that @p x is true (non-zero); otherwise, exit with a generic error
    message followed by errno and its stringified form. */
#define T_e(x)                                                                 \
    do {                                                                       \
        if (!(x)) {                                                            \
            spdlog::error("{}:{} errno: {}", __FILE__, __LINE__,               \
                          strerror(errno));                                    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/** Verify that @p x is true (non-zero); otherwise, exit with an error message
    specified by a `printf(3)` format string in the second argument along with
    appropriate additional arguments, followed by errno and its stringified
    form. */
#define Tfe(x, fmts, ...)                                                      \
    do {                                                                       \
        if (!(x)) {                                                            \
            spdlog::error(fmt::runtime(std::string("{}:{} ") + fmts),          \
                          __FILE__, __LINE__, errno, ##__VA_ARGS__);           \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/** Verify that @p x is zero (false); otherwise, exit with a generic error
    message. */
#define Z__(x)                                                                 \
    do {                                                                       \
        if (x) {                                                               \
            spdlog::error("{}:{}", __FILE__, __LINE__);                        \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/** Verify that @p x is zero (false); otherwise, exit with a generic error
    message followed by errno and its stringified form. */
#define Z_e(x)                                                                 \
    do {                                                                       \
        if (x) {                                                               \
            spdlog::error("{}:{} errno: {}", __FILE__, __LINE__, errno);       \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/** Verify that @p x is zero (false); otherwise, exit with an error message
    specified by a `printf(3)` format string in the second argument along with
    appropriate additional arguments, followed by errno and its stringified
    form. */
#define Zfe(x, fmts, ...)                                                      \
    do {                                                                       \
        if (x) {                                                               \
            spdlog::error(                                                     \
                fmt::runtime(std::string("{}:{} errno: {} ") + fmts),          \
                __FILE__, __LINE__, errno, ##__VA_ARGS__);                     \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
