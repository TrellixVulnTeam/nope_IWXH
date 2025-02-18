// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process.h"

#include <sys/resource.h>
#include <sys/wait.h>

#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/kill.h"

#if defined(OS_MACOSX)
#include <sys/event.h>
#endif

namespace {

#if !defined(OS_NACL_NONSFI)

bool WaitpidWithTimeout(base::ProcessHandle handle,
                        int* status,
                        base::TimeDelta wait) {
  // This POSIX version of this function only guarantees that we wait no less
  // than |wait| for the process to exit.  The child process may
  // exit sometime before the timeout has ended but we may still block for up
  // to 256 milliseconds after the fact.
  //
  // waitpid() has no direct support on POSIX for specifying a timeout, you can
  // either ask it to block indefinitely or return immediately (WNOHANG).
  // When a child process terminates a SIGCHLD signal is sent to the parent.
  // Catching this signal would involve installing a signal handler which may
  // affect other parts of the application and would be difficult to debug.
  //
  // Our strategy is to call waitpid() once up front to check if the process
  // has already exited, otherwise to loop for |wait|, sleeping for
  // at most 256 milliseconds each time using usleep() and then calling
  // waitpid().  The amount of time we sleep starts out at 1 milliseconds, and
  // we double it every 4 sleep cycles.
  //
  // usleep() is speced to exit if a signal is received for which a handler
  // has been installed.  This means that when a SIGCHLD is sent, it will exit
  // depending on behavior external to this function.
  //
  // This function is used primarily for unit tests, if we want to use it in
  // the application itself it would probably be best to examine other routes.

  if (wait == base::TimeDelta::Max()) {
    return HANDLE_EINTR(waitpid(handle, status, 0)) > 0;
  }

  pid_t ret_pid = HANDLE_EINTR(waitpid(handle, status, WNOHANG));
  static const int64 kMaxSleepInMicroseconds = 1 << 18;  // ~256 milliseconds.
  int64 max_sleep_time_usecs = 1 << 10;  // ~1 milliseconds.
  int64 double_sleep_time = 0;

  // If the process hasn't exited yet, then sleep and try again.
  base::TimeTicks wakeup_time = base::TimeTicks::Now() + wait;
  while (ret_pid == 0) {
    base::TimeTicks now = base::TimeTicks::Now();
    if (now > wakeup_time)
      break;
    // Guaranteed to be non-negative!
    int64 sleep_time_usecs = (wakeup_time - now).InMicroseconds();
    // Sleep for a bit while we wait for the process to finish.
    if (sleep_time_usecs > max_sleep_time_usecs)
      sleep_time_usecs = max_sleep_time_usecs;

    // usleep() will return 0 and set errno to EINTR on receipt of a signal
    // such as SIGCHLD.
    usleep(sleep_time_usecs);
    ret_pid = HANDLE_EINTR(waitpid(handle, status, WNOHANG));

    if ((max_sleep_time_usecs < kMaxSleepInMicroseconds) &&
        (double_sleep_time++ % 4 == 0)) {
      max_sleep_time_usecs *= 2;
    }
  }

  return ret_pid > 0;
}

#if defined(OS_MACOSX)
// Using kqueue on Mac so that we can wait on non-child processes.
// We can't use kqueues on child processes because we need to reap
// our own children using wait.
static bool WaitForSingleNonChildProcess(base::ProcessHandle handle,
                                         base::TimeDelta wait) {
  DCHECK_GT(handle, 0);
  DCHECK_GT(wait, base::TimeDelta());

  base::ScopedFD kq(kqueue());
  if (!kq.is_valid()) {
    DPLOG(ERROR) << "kqueue";
    return false;
  }

  struct kevent change = {0};
  EV_SET(&change, handle, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, NULL);
  int result = HANDLE_EINTR(kevent(kq.get(), &change, 1, NULL, 0, NULL));
  if (result == -1) {
    if (errno == ESRCH) {
      // If the process wasn't found, it must be dead.
      return true;
    }

    DPLOG(ERROR) << "kevent (setup " << handle << ")";
    return false;
  }

  // Keep track of the elapsed time to be able to restart kevent if it's
  // interrupted.
  bool wait_forever = (wait == base::TimeDelta::Max());
  base::TimeDelta remaining_delta;
  base::TimeTicks deadline;
  if (!wait_forever) {
    remaining_delta = wait;
    deadline = base::TimeTicks::Now() + remaining_delta;
  }

  result = -1;
  struct kevent event = {0};

  while (wait_forever || remaining_delta > base::TimeDelta()) {
    struct timespec remaining_timespec;
    struct timespec* remaining_timespec_ptr;
    if (wait_forever) {
      remaining_timespec_ptr = NULL;
    } else {
      remaining_timespec = remaining_delta.ToTimeSpec();
      remaining_timespec_ptr = &remaining_timespec;
    }

    result = kevent(kq.get(), NULL, 0, &event, 1, remaining_timespec_ptr);

    if (result == -1 && errno == EINTR) {
      if (!wait_forever) {
        remaining_delta = deadline - base::TimeTicks::Now();
      }
      result = 0;
    } else {
      break;
    }
  }

  if (result < 0) {
    DPLOG(ERROR) << "kevent (wait " << handle << ")";
    return false;
  } else if (result > 1) {
    DLOG(ERROR) << "kevent (wait " << handle << "): unexpected result "
                << result;
    return false;
  } else if (result == 0) {
    // Timed out.
    return false;
  }

  DCHECK_EQ(result, 1);

  if (event.filter != EVFILT_PROC ||
      (event.fflags & NOTE_EXIT) == 0 ||
      event.ident != static_cast<uintptr_t>(handle)) {
    DLOG(ERROR) << "kevent (wait " << handle
                << "): unexpected event: filter=" << event.filter
                << ", fflags=" << event.fflags
                << ", ident=" << event.ident;
    return false;
  }

  return true;
}
#endif  // OS_MACOSX

bool WaitForExitWithTimeoutImpl(base::ProcessHandle handle,
                                int* exit_code,
                                base::TimeDelta timeout) {
  base::ProcessHandle parent_pid = base::GetParentProcessId(handle);
  base::ProcessHandle our_pid = base::GetCurrentProcessHandle();
  if (parent_pid != our_pid) {
#if defined(OS_MACOSX)
    // On Mac we can wait on non child processes.
    return WaitForSingleNonChildProcess(handle, timeout);
#else
    // Currently on Linux we can't handle non child processes.
    NOTIMPLEMENTED();
#endif  // OS_MACOSX
  }

  int status;
  if (!WaitpidWithTimeout(handle, &status, timeout))
    return false;
  if (WIFSIGNALED(status)) {
    *exit_code = -1;
    return true;
  }
  if (WIFEXITED(status)) {
    *exit_code = WEXITSTATUS(status);
    return true;
  }
  return false;
}
#endif  // !defined(OS_NACL_NONSFI)

}  // namespace

namespace base {

Process::Process(ProcessHandle handle) : process_(handle) {
}

Process::Process(RValue other)
    : process_(other.object->process_) {
  other.object->Close();
}

Process& Process::operator=(RValue other) {
  if (this != other.object) {
    process_ = other.object->process_;
    other.object->Close();
  }
  return *this;
}

// static
Process Process::Current() {
  return Process(GetCurrentProcessHandle());
}

// static
Process Process::Open(ProcessId pid) {
  if (pid == GetCurrentProcId())
    return Current();

  // On POSIX process handles are the same as PIDs.
  return Process(pid);
}

// static
Process Process::OpenWithExtraPriviles(ProcessId pid) {
  // On POSIX there are no privileges to set.
  return Open(pid);
}

// static
Process Process::DeprecatedGetProcessFromHandle(ProcessHandle handle) {
  DCHECK_NE(handle, GetCurrentProcessHandle());
  return Process(handle);
}

#if !defined(OS_LINUX)
// static
bool Process::CanBackgroundProcesses() {
  return false;
}
#endif  // !defined(OS_LINUX)

bool Process::IsValid() const {
  return process_ != kNullProcessHandle;
}

ProcessHandle Process::Handle() const {
  return process_;
}

Process Process::Duplicate() const {
  if (is_current())
    return Current();

  return Process(process_);
}

ProcessId Process::Pid() const {
  DCHECK(IsValid());
  return GetProcId(process_);
}

bool Process::is_current() const {
  return process_ == GetCurrentProcessHandle();
}

void Process::Close() {
  process_ = kNullProcessHandle;
  // if the process wasn't terminated (so we waited) or the state
  // wasn't already collected w/ a wait from process_utils, we're gonna
  // end up w/ a zombie when it does finally exit.
}

bool Process::Terminate(int result_code, bool wait) const {
  // result_code isn't supportable.
  DCHECK(IsValid());
  // We don't wait here. It's the responsibility of other code to reap the
  // child.
  // TODO(rvargas) crbug/417532: Move the implementation here.
  return KillProcess(process_, result_code, wait);
}

bool Process::WaitForExit(int* exit_code) {
  return WaitForExitWithTimeout(TimeDelta::Max(), exit_code);
}

bool Process::WaitForExitWithTimeout(TimeDelta timeout, int* exit_code) {
  return WaitForExitWithTimeoutImpl(Handle(), exit_code, timeout);
}

#if !defined(OS_LINUX)
bool Process::IsProcessBackgrounded() const {
  // See SetProcessBackgrounded().
  DCHECK(IsValid());
  return false;
}

bool Process::SetProcessBackgrounded(bool value) {
  // POSIX only allows lowering the priority of a process, so if we
  // were to lower it we wouldn't be able to raise it back to its initial
  // priority.
  DCHECK(IsValid());
  return false;
}
#endif  // !defined(OS_LINUX)

int Process::GetPriority() const {
  DCHECK(IsValid());
  return getpriority(PRIO_PROCESS, process_);
}

}  // namespace base
