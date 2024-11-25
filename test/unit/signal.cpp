#include <signal.h>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include <util/sigint.h>

TEST_CASE("ctrl-c", "[signal]") {
    int counter{0};

    auto on_sig = [&counter]() { ++counter; };

    // destruction with no signal: on_sig is not called
    { auto h = util::sigint_handle(on_sig); }
    REQUIRE(counter == 0);

    // raise signal inside a scope: call once
    {
        auto h = util::sigint_handle(on_sig);
        raise(SIGINT);
    }
    REQUIRE(counter == 1);

    // handlers that are not interrupted should not
    // test here to ensure that the global state that detects
    // whether a signal has been triggered was cleared previously
    { auto h = util::sigint_handle(on_sig); }

    REQUIRE(counter == 1);

    // raise another 4 times: it should cleanly handle the signal each time
    for (int i = 0; i < 4; ++i) {
        {
            auto h = util::sigint_handle(on_sig);
            raise(SIGINT);
        }
    }
    REQUIRE(counter == 5);
}
