#pragma once
#include <string>
#include <optional>
#include <semaphore.h>
#include <vector>

namespace uenv {

/* Variables for coordinating join */
struct join_t {
    bool winner_p;
    std::string sem_name;
    sem_t* sem;
    std::string shm_name;
    struct {
        pid_t winner_pid;
        int proc_left_ct; // serial-only access
    }* shared;
};

util::expected<void, std::string> join_begin(join_t& join,
                                             std::string join_tag);
util::expected<void, std::string> join_end(join_t& join, int join_ct,
                                           std::optional<pid_t> winner_pid);

util::expected<void, std::string> namespaces_join(pid_t pid, const std::vector<std::string>& ns_names);

} // namespace uenv

/* Variables for coordinating join */
struct join_t {
    bool winner_p;
    std::string sem_name;
    sem_t* sem;
    std::string shm_name;
    struct {
        pid_t winner_pid;
        int proc_left_ct; // serial-only access
    }* shared;
};

// util::expected<void, std::string> join_begin(join_t& join,
//                                              std::string join_tag);
// util::expected<void, std::string> join_end(join_t& join, int join_ct,
//                                            std::optional<pid_t> winner_pid);

// util::expected<void, std::string> namespaces_join(pid_t pid, const std::vector<std::string>& ns_names);
