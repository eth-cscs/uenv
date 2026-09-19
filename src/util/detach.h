#pragma once

#include <chrono>
#include <functional>

namespace util {

// Run `work` in a detached grandchild of this process, and return without
// waiting for it. Used to do slow work (a network request) on behalf of a
// process that must return at once (tab completion), which must never be held
// up by it:
//
// - the grandchild is in a new session, and is reparented once its parent,
//   an intermediate child that exits at once, has been reaped here;
// - its stdin, stdout and stderr are /dev/null and every other descriptor is
//   closed, so it holds none of the caller's pipes open: a shell reading the
//   caller's output with $(...) waits for EOF, which a grandchild that kept
//   stdout would delay until it finished;
// - it is killed by SIGALRM after `limit`, whatever it is doing.
//
// Only call this from a single-threaded process: `work` runs after fork(2)
// without exec(2). Failure to fork is not reported: the work is not done.
void spawn_detached(const std::function<void()>& work,
                    std::chrono::seconds limit);

} // namespace util
