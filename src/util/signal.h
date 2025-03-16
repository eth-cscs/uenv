#pragma once

#include <exception>

namespace util {

class signal_exception : public std::exception {
  public:
    const int signal;
    const std::string msg;
    signal_exception(int signal);
    const char* what() const throw();
};

void set_signal_catcher();
bool signal_raised();
int last_signal_raised();

} // namespace util
