#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <ext/stdio_filebuf.h>

#include <util/expected.h>

namespace util {

struct pipe {
    int state[2];
    int read() const {
        return state[0];
    }
    int write() const {
        return state[1];
    }
    void close() {
        ::close(read());
        ::close(write());
    }
    void close_read() {
        ::close(read());
    }
    void close_write() {
        ::close(write());
    }
};

class buffered_istream {
    std::unique_ptr<__gnu_cxx::stdio_filebuf<char>> buffer_;
    std::unique_ptr<std::istream> stream_;

  public:
    buffered_istream() = delete;
    buffered_istream(const pipe& p)
        : buffer_(new __gnu_cxx::stdio_filebuf<char>(p.read(),
                                                     std::ios_base::in, 1)),
          stream_(new std::istream(buffer_.get())) {
    }

    std::istream& stream();

    std::optional<std::string> getline();
};

class buffered_ostream {
    std::unique_ptr<__gnu_cxx::stdio_filebuf<char>> buffer_;
    std::unique_ptr<std::ostream> stream_;

  public:
    buffered_ostream() = delete;
    buffered_ostream(const pipe& p)
        : buffer_(new __gnu_cxx::stdio_filebuf<char>(p.write(),
                                                     std::ios_base::out, 1)),
          stream_(new std::ostream(buffer_.get())) {
    }

    std::ostream& stream();
    void putline(std::string_view line);
};

enum class proc_status { running, finished };

class subprocess {
  public:
    buffered_istream out;
    buffered_istream err;
    buffered_ostream in;
    pid_t pid;

    subprocess() = delete;

    subprocess(buffered_istream out, buffered_istream err, buffered_ostream in,
               pid_t pid)
        : out(std::move(out)), err(std::move(err)), in(std::move(in)),
          pid(pid) {
    }

    int wait();
    bool finished();
    int rvalue();
    void kill(int signal = 9);

  private:
    bool finished_ = false;
    std::optional<int> rcode_;
    void setrcode(int);
};

expected<subprocess, std::string> run(const std::vector<std::string>& argv);

} // namespace util
