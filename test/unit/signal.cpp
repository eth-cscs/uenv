#include <signal.h>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include <util/signal.h>

TEST_CASE("SIGINT", "[signal]") {
    util::set_signal_catcher();
    raise(SIGINT);
    REQUIRE(util::signal_raised());
    REQUIRE(!util::signal_raised());

    util::set_signal_catcher();
    REQUIRE(!util::signal_raised());
    raise(SIGTERM);
    REQUIRE(util::signal_raised());
    REQUIRE(!util::signal_raised());

    util::set_signal_catcher();
    raise(SIGINT);
    REQUIRE(util::signal_raised());
    REQUIRE(!util::signal_raised());
}
