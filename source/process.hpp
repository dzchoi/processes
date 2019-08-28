// Process: Thread-safe lightweight C++ module for executing processes in background
//
// 10/05/18, dzchoi, completed move constructor and decided not to provide move 
//                   assignment for good efficiency.
// 08/22/19, dzchoi, removed the mutex releasing while polling for ::waitpid().


#pragma once

#include <algorithm>  // min()
#include <cassert>  // assert()

#include "_pipe.hpp"
//#include <cstdlib>: _Exit()
//#include <system_error>: system_error(), system_category(), errno
//#include <unistd.h>: STD*_FILENO, close(), fork(), execvp()

#include <mutex>  // mutex, lock_guard<>, unique_lock<>, once_flag, call_once()
#include <condition_variable>
    // condition_variable, .notify_one(), .notify_all(), .wait(), .wait_for()
#include <chrono>
    // chrono::steady_clock::now(), chrono_literals, chrono::milliseconds, 
    // chrono::duration_cast<>
#include <thread>  // this_thread::sleep_for()

#include <string>  // basic_string<>, string, .c_str()
//#include <stdio.h>: dprintf()
#include <vector>  // vector<>, .push_back(), .shrink_to_fit(), .data()
//#include <initializer_list>: initializer_list<>

extern "C" {
#include <sys/wait.h>  // waitpid(), WNOHANG, WEXITSTATUS, ...
//#include <signal.h>: kill(), SIGKILL
}



class process {
private:
    template <typename CharT, typename Traits, typename Allocator>
    static std::vector<const CharT*> _to_vector(
        std::initializer_list<std::basic_string<CharT, Traits, Allocator>> args);

    template <typename =void>  // bogus template definition to have the definition in .hpp
    static int _fd_or_devnull(int fd);  // return fd of /dev/null if fd == DEVNULL.

protected:
    pid_t _pid = 0; // of child process

    enum {
        DONE     = 0,    // child is done running (terminated)
        UNWAITED = 1,    // running but no thread is waiting for
        AWAITED  = 2     // running and some thread is waiting for
    } _running = DONE;   // indicates if child process is running.
    int _exitcode = UNKNOWN_ERROR;  // exitcode of child process if terminated

    std::mutex _mtx_running;     // mutex protecting _running and _exitcode
    std::condition_variable _not_awaited;  // waiter for _running != AWAITED

    int _stdin  = DEVNULL;
    int _stdout = DEVNULL;
    int _stderr = DEVNULL;

public:
    const pid_t& pid = _pid;  // pid of child process

    // No access to _running is provided in public, use !poll() instead.

    const int& exitcode = _exitcode;
    static constexpr int UNKNOWN_ERROR = -127;     // unknown exitcode
    static constexpr int COMMAND_NOT_FOUND = 127;  // exitcode for "command not found"

    // Constants for fds
    enum {
        SAMEOUT = -3,  // SAMEOUT can be specified only for stderr.
        PIPE    = -2,
        DEVNULL = -1,  // -1 is specific to match "invalid fd" in _pipe<> class.
        STDIN   = STDIN_FILENO,
        STDOUT  = STDOUT_FILENO,
        STDERR  = STDERR_FILENO
    };

    const int& stdin  = _stdin;
    const int& stdout = _stdout;
    const int& stderr = _stderr;

    // Elaborate constructor
    explicit process(int fd0, const char* argv[], int fd1, int fd2);

    // Constructors for syntactic sugar
    explicit process( int fd0, std::initializer_list<std::string> args,
        int fd1 =DEVNULL, int fd2 =DEVNULL )
    : process(fd0, _to_vector(args).data(), fd1, fd2) {}

    explicit process( std::initializer_list<std::string> args,
        int fd1 =DEVNULL, int fd2 =DEVNULL )
    : process(DEVNULL, _to_vector(args).data(), fd1, fd2) {}

    // The process object is movable using move constructor (but not move assignment).
    // The behavior of accessing q (from current thread or other thread) after "process 
    // p { std::move(q) };" is undefined.
    process(process&& p);

    // Default constructor and move assignment are not supported. In case a container of 
    // processes is wanted, use .emplace*() on the container instead of .push*() or 
    // .insert().
    process() =delete;
    process& operator=(process&&) =delete;

    // The process object is not copyable.
    process(const process&) =delete;
    process& operator=(const process&) =delete;

    ~process();
    // Child process is not killed as process object is destroyed, so that we can 
    // pipeline multiple processes in line with creating temporary processes. Thus, 
    // explicit wait() and/or kill() is required not to make child process an orphan.
    // However, fds created using process::PIPEs are destroyed implicitly along with 
    // process objects since they are embedded in actual system process (as a target for 
    // standard streams) and do not need to be exposed outside.

    // Wait for child process to terminate indefinitely.
    void wait();

    // Wait for child process to terminate for the duration of timeout, returning true if 
    // terminated, or false if timed out (child process is still running).
    bool wait(const std::chrono::milliseconds& timeout);

    // The timed wait() uses a busy-polling loop (non-blocking call of ::waitpid() and 
    // short sleeps) instead of signal handler for SIGCHLD. However, when multiple 
    // threads wait for the same child process at the same time, they do not race but 
    // they help each other; one of them voluntarily waits polling and the others wait 
    // just for him. If his time is up and he cannot wait any longer, another one among 
    // the others waits voluntarily in place of him, and so on.

    // Check if child process has terminated, returning true if so, or false otherwise.
    // poll() should be used over directly inquiring _running, because _running holds 
    // only the information of the last poll() or wait() executed.
    bool poll();  // is the same as wait(0ms), only more optimized.

    // Send the specified signal or SIGTERM (=15) by default to the child.
    void kill(int sig =SIGKILL);
};  // class process



process::process(int fd0, const char* argv[], int fd1, int fd2)
:   _running { UNWAITED }
{
    assert(fd0 != SAMEOUT);
    assert(fd1 != SAMEOUT);

    _pipe<false> pipe_in = _fd_or_devnull(fd0);
    _pipe<true> pipe_out = _fd_or_devnull(fd1);
    _pipe<true> pipe_err = ( fd2 == SAMEOUT ? pipe_out.near : _fd_or_devnull(fd2) );

    // The _pipe<> class contains two file descriptors, "far" and "near". The "far" 
    // refers to the file descriptor far from child process, while "near" refers to the 
    // one near to child process.
    // When we are given a fdN >= 0 as argument, we simply put it at near and set far = 
    // -1. If fdN == -1 (PIPE), we create a pipe with two new file descriptors, which we 
    // put at near and far. Nears are used in a child process to redirect the child's 
    // standard streams (stdin/stdout/stdout) to. Fars (if != -1) are used for (outer) 
    // file descriptors that are connected to the corresponding (inner) standard streams 
    // of the child process; if far == -1, we don't have such a file descriptor connected 
    // to a child stream.
    // After all pipes/redirections set up, parent process closes nears because they are 
    // not used any longer by parent process, and child process closes both nears and 
    // fars because child process has its stdin/stdout/stderr now point (redirect) to 
    // them. So, from parent's view, a given fdN turns into -1 for redirection or a file 
    // descriptor for pipe.
    // Note also that if far == -1, the given fdN came from outside and process object 
    // cannot close the fdN on its destruction, but if far != -1, as created from inside, 
    // far is thought to be owned by process object and gets closed on the destruction.

    if ( (_pid = ::fork()) == 0 )  // run in child process!
    {
        // Redirection cases
        // case 1:      // case 2:      // case 3:
        // 3 <- 0       // 3 <- 0       // 3 <- 0
        // 3 <- 1       // 1 <- 1       // 2 <- 1
        // 2 <- 2       // 2 <- 2       // 2 <- 2

        // case 4:      // case 5:      // case 6 (swapping):
        // 3 <- 0       // 3 <- 0       // 3 <- 0
        // 1 <- 1       // 3 <- 1       // 2 <- 1
        // 1 <- 2       // 1 <- 2       // 1 <- 2
        // -->          // -->          // -->
        // 3 <- 0       // 3 <- 0       // 3 <- 0
        // 1 <- 2       // 1 <- 2       // 2 <- 4
        // 1 <- 1       // 3 <- 1       // 1 <- 2
                                        // 4 <- 1

        // redirect child's standard streams
        pipe_in.dup2(STDIN);
        if ( pipe_err.near == STDOUT ) {    // case 4/5/6
            if ( pipe_out.near == STDERR )  // case 6
                pipe_out.dup2();
            pipe_err.dup2(STDERR);
            pipe_out.dup2(STDOUT);
        } else {                            // case 1/2/3
            pipe_out.dup2(STDOUT);
            pipe_err.dup2(STDERR);
        }

        // close all fds but stdin/stdout/stderr.
        pipe_in .close();
        pipe_out.close();
        pipe_err.close();

#if !defined(NDEBUG) && defined(DEBUG)  // check if stdout/stderr is writable.
        dprintf(STDOUT, "Ok to write into \033[33mSTDOUT\033[0m\n");
        dprintf(STDERR, "Ok to write into \033[33mSTDERR\033[0m\n");
#endif

        if ( ::execvp(argv[0], const_cast<char**>(argv)) == -1 )
            std::_Exit(COMMAND_NOT_FOUND);  // returning 127 as most shells do.
                // instead of using std::exit() due to no need for cleaning up.
    }

    else {  // run in parent process!
        if ( _pid == -1 )
            throw std::system_error(errno, std::system_category());

        _stdin  = pipe_in .far;
        _stdout = pipe_out.far;
        _stderr = pipe_err.far;
        // Note the near ends of pipes will be closed on exit.
    }
}

process::process(process&& p)
:   _pid(p._pid),
    _running(p._running),
    _exitcode(p._exitcode),
    _stdin(p._stdin),
    _stdout(p._stdout),
    _stderr(p._stderr)
{
    // As a temporary object, we can assume that p came from current thread at which we 
    // are now running this constructor (although it can be hacked by "std::move(p)" with 
    // some p created from other thread). Then, we can also assume that no other threads 
    // than current thread are now working (i.e, waiting) on p and *this.
    //
    // If we are to support move assignment, we have to deal with the case where target 
    // of the assignment may have been shared with other threads, which may be accessing 
    // _exitcode and other members when we update them in move assignment.
    // See also: https://stackoverflow.com/a/46391077

    if ( this != &p ) {  // to handle "process p { std::move(p) };".
        p._pid      = 0;
        p._running  = DONE;
        p._exitcode = UNKNOWN_ERROR;
        p._stdin    = DEVNULL;
        p._stdout   = DEVNULL;
        p._stderr   = DEVNULL;
    }
}

/*
class process {
    ...
protected:
    const std::thread::id _tid = std::this_thread::get_id();
    ...
};

process::process(process&& p) { operator=(std::move(p)); }

process& process::operator=(process&& p)
{
    if ( this != &p ) {  // to handle "process p { std::move(p) }" or "p = std::move(p)".
        if ( _tid != p._tid || _tid != std::this_thread::get_id() )
            throw std::runtime_error("cannot move process between threads");

        std::unique_lock<std::mutex> lock(_mtx_running, std::try_to_lock);
        if ( !lock || _running != DONE )
            throw std::runtime_error("cannot move process that is being shared");

        // However, we cannot still be sure that there are no threads that are accessing 
        // this->_exitcode and other members while we update them below. Handling this 
        // problem needs another mutex (and condition_variable) besides _mtx_running for 
        // counting the number of threads that simultaneously access this object, which 
        // is too expensive just for supporting move assignment.

        std::swap(_pid, p._pid);
        std::swap(_running, p._running);
        std::swap(_exitcode, p._exitcode);
        std::swap(_stdin, p._stdin);
        std::swap(_stdout, p._stdout);
        std::swap(_stderr, p._stderr);
    }

    return *this;
}
*/

process::~process()
{
    // We do not check for each fd == -1 before ::close()ing; because ::close() will do 
    // no harm for -1, and even if fd != -1, fd might be already closed from explicitly 
    // closing a fdstream that shares the fd.

    ::close(_stdin);
    ::close(_stdout);
    ::close(_stderr);
}

void process::wait()
{
    std::unique_lock<std::mutex> lock(_mtx_running);
    _not_awaited.wait(lock, [this]{ return _running != AWAITED; });
        // wait for _running != AWAITED.

    if ( _running == UNWAITED ) {
        _running = AWAITED;  // no need to notify of this since nobody waits for AWAITED.

        int status;
        int wpid = ::waitpid(pid, &status, 0);
            // ::waitpid() is also protected by the mutex _mtx_running so that it cannot 
            // called simultaneously from two threads. Otherwise, the first call will 
            // succeed but the second call will result in ECHILD, meaning no child 
            // process with the pid exists.

        if ( wpid != -1 ) {
            if ( WIFEXITED(status) )
                _exitcode = WEXITSTATUS(status);
            else if ( WIFSIGNALED(status) )
                _exitcode = -WTERMSIG(status);
        }
        //else: Possibly because SIGCHLD's signal action is set to SIG_IGN explicitly.

        _running = DONE;
        _not_awaited.notify_all();
    }
}

bool process::wait(const std::chrono::milliseconds& timeout)
{
    const auto when_to_return = std::chrono::steady_clock::now() + timeout;

    std::unique_lock<std::mutex> lock(_mtx_running);
    if ( !_not_awaited.wait_for(lock, timeout, [this]{ return _running != AWAITED; }) )
        // wait for _running != AWAITED within the interval of timeout.
        return false;  // because timed out means someone else is still waiting.

    if ( _running == UNWAITED ) {
        _running = AWAITED;

        using namespace std::chrono_literals;
        int status, wpid;

        for ( auto dt = 1ms ; (wpid = ::waitpid(pid, &status, WNOHANG)) == 0 ; )
        // The do ... while() cannot be used here for for() because we have to check 
        // ::waitpid() at least once no matter how short the timeout is.
        {
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    when_to_return - std::chrono::steady_clock::now() );
            if ( remaining <= 0ms ) {
                // "I have no more time to wait. So, someone else wait please!"
                _running = UNWAITED;
                _not_awaited.notify_one();
                    // notify_one() is enough since everybody is willing to wait.
                return false;
            }

            std::this_thread::sleep_for(std::min(dt, remaining));
            if ( dt < 64ms ) dt *= 2;  // catnap for maximum 64ms at a time
        }

        if ( wpid != -1 ) {
            if ( WIFEXITED(status) )
                _exitcode = WEXITSTATUS(status);
            else if ( WIFSIGNALED(status) )
                _exitcode = -WTERMSIG(status);
        }
        //else: Possibly because SIGCHLD's signal action is set to SIG_IGN explicitly.

        _running = DONE;
        _not_awaited.notify_all();  // notify_all() since everybody wants it.
    }

    return true;
}

bool process::poll()
{
    std::lock_guard<std::mutex> lock(_mtx_running);

    if ( _running == UNWAITED ) {
        int status;
        int wpid = ::waitpid(pid, &status, WNOHANG);

        if ( wpid == 0 ) return false;
        if ( wpid != -1 ) {
            if ( WIFEXITED(status) )
                _exitcode = WEXITSTATUS(status);
            else if ( WIFSIGNALED(status) )
                _exitcode = -WTERMSIG(status);
        }
        //else: Possibly because SIGCHLD's signal action is set to SIG_IGN explicitly.

        _running = DONE;
        _not_awaited.notify_all();
    }

    return _running == DONE;
}

void process::kill(int sig)
{
    if ( !poll() )
        // !poll() is used instead of "_running != DONE" to be able to kill otherwise 
        // possibly defunct child process. (A defunct (zombie) child process that 
        // terminated but has not been waited for can be killed only by ::waitpid(), not 
        // ::kill().)
        if ( ::kill(pid, sig) == -1 )
            throw std::system_error(errno, std::system_category());
}



template <typename CharT, typename Traits, typename Allocator>
std::vector<const CharT*> process::_to_vector(
    std::initializer_list<std::basic_string<CharT, Traits, Allocator>> args)
{
    std::vector<const CharT*> result;
    for ( const auto& each: args )
        result.push_back(each.c_str());
    result.push_back(nullptr);
    result.shrink_to_fit();
    return std::move(result);
}

extern "C" {
#include <fcntl.h>  // open(), O_RDWR
}

template <typename>
int process::_fd_or_devnull(int fd) {
    static int devnull;  // initialized lazily
    static std::once_flag one_time;

    if ( fd == DEVNULL ) {
        std::call_once(one_time, []{
        // prevents multiple threads from running ::open() below simultaneously.
            devnull = ::open("/dev/null", O_RDWR);
            if ( devnull == -1 )
                throw std::system_error(errno, std::system_category());
        });

        return devnull;
    }
    else
        return fd;
}
