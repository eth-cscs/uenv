#include <atomic>
#include <csignal>

#include <fmt/format.h>

#include <util/signal.h>

namespace util {

std::atomic<bool> signal_received{false};
std::atomic<int> last_signal{0};

signal_exception::signal_exception(int signal)
    : signal(signal), msg(fmt::format("signal {} raised", signal)) {
}
const char* signal_exception::what() const throw() {
    return msg.c_str();
}

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        signal_received.store(true);
        last_signal.store(signal);
    }
}

void set_signal_catcher() {
    signal_received.store(false);
    struct sigaction h;

    h.sa_handler = signal_handler;
    sigemptyset(&h.sa_mask);
    h.sa_flags = SA_RESETHAND;
    sigaction(SIGINT, &h, nullptr);
    sigaction(SIGTERM, &h, nullptr);
}

bool signal_raised() {
    return signal_received.exchange(false);
}

int last_signal_raised() {
    return last_signal.load();
}

} // namespace util
