#pragma once

#include <exception>

#include <fmt/format.h>

namespace util {

class signal_exception : public std::exception {
  public:
    const int signal;
    const std::string msg;
    signal_exception(int signal)
        : signal(signal), msg(fmt::format("signal {} raised", signal)) {
    }
    const char* what() const throw() {
        return msg.c_str();
    }
};

void set_signal_catcher();
bool signal_raised();
int last_signal_raised();

} // namespace util
