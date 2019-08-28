// process: thread-safe lightweight C++ module for executing commands in background
//
// References:
// - https://gist.github.com/konstantint/d49ab683b978b3d74172
// - https://github.com/tsaarni/cpp-subprocess

// Todo:
// Provide move operations as std::thread does, to support like:
// - process proc[4];
//   for ( int i = 0 ; i < 4 ; ++i )
//       proc[i] = process { "sleep", std::to_string(i) };
// - process proc;
//   proc = process { "sleep", "1" };
//   proc.wait();  // now movable since terminated
//   proc = process { "sleep", "1" };
// We do have concept of temporary (unbound) process, but it is not so easy because:
// - only processes that are not AWAITED can be moved.
// - those std::mutex and std::condition_variable members are not movable basically.
// - those const members pid, stdin, stdout, and stderr are not movable.
// Refer to: https://stackoverflow.com/a/46391077



#include <algorithm>  // min()
#include <cassert>  // assert()

#include "pipe.hpp"
// <cstdlib>: _Exit()
// <system_error>: system_error(), system_category(), errno
// <unistd.h>: STD*_FILENO, fork(), execvp()

#include <mutex>  // mutex, lock_guard<>, unique_lock<>, once_flag, call_once()
#include <condition_variable>
    // condition_variable, .notify_one(), .notify_all(), .wait(), .wait_for()
#include <chrono>
    // chrono::steady_clock::now(), chrono_literals, chrono::milliseconds, 
    // chrono::duration_cast<>
#include <thread>  // this_thread::sleep_for()

#include <string>  // basic_string<>, string, .c_str()
// <stdio.h>: dprintf()
#include <vector>  // vector<>, .push_back(), .shrink_to_fil(), .data()
// <initializer_list>: initializer_list<>

extern "C" {
#include <sys/wait.h>  // waitpid(), WNOHANG, WEXITSTATUS, ...
// <signal.h>: kill(), SIGKILL
}



class process {
private:
    // These are not accessed out of constructor, so could be defined locally in 
    // constructor, but in order to use _pipe<>'s destructor at the right time.
    // Todo: https://www.justsoftwaresolutions.co.uk/threading/thread-safe-copy-constructors.html
    _pipe<false> pipe_in;
    _pipe<true> pipe_out;
    _pipe<true> pipe_err;

    template <typename CharT, typename Traits, typename Allocator>
    static std::vector<const CharT*> _to_vector(
	std::initializer_list<std::basic_string<CharT, Traits, Allocator>> args);

protected:
    enum running_t {
	DONE	= 0,	// child is done running (terminated)
	ALONE	= 1,	// no thread is waiting for child
	AWAITED = 2	// some thread is waiting for child
    };

    running_t running = DONE;  // indicates if child process is running.

    std::mutex mutex_running;  // mutex protecting running and exitcode
    std::condition_variable not_awaited;  // waiter for running != AWAITED

    template <typename =void>  // bogus template for having the definition in .hpp
    static int _devnull();  // return fd of /dev/null.

public:
    static constexpr int UNKNOWN = -127;  // unknown exitcode
    int exitcode = UNKNOWN;  // exitcode of child process if terminated

    enum {
	SAMEOUT = -3,  // SAMEOUT can be specified only for stderr.
	PIPE	= -2,
	DEVNULL = -1,  // -1 is intentional to match invalid fd in _pipe<> class.
	STDIN	= STDIN_FILENO,
	STDOUT	= STDOUT_FILENO,
	STDERR	= STDERR_FILENO
    };

    const int& stdin  = pipe_in.far;
    const int& stdout = pipe_out.far;
    const int& stderr = pipe_err.far;

    // We can think of a pipe just as a new file descriptor created for (redirecting) 
    // input/output of child process. Or, we could use an existing file descriptor like 
    // STDIN_FILENO/STDOUT_FILENO for (redirecting) input/output of child process. The 
    // _pipe<> class actually contains two file descriptors, "far" and "near", but we can 
    // ignore "near" for good reason as "near" is deleted (closed) from parent and can be 
    // accessed only by child process. So, pipe can be abstracted as a single file 
    // descriptor.
    // However, we need to distinguish between an existing fd (for redirection) and a new 
    // fd (for pipe) created here because we have to delete the second ones in our 
    // destructor but do nothing for the first ones, so only the second ones are 
    // remembered as is and the first ones are remembered as just -1 (in 
    // process::stdin/stdout/stderr). We can think that all input fds except 
    // process::PIPE turn into -1 after using them for redirection.

    const pid_t pid; // of child process

    // native constructor
    process(int fd0, const char* argv[], int fd1, int fd2);

    process( int fd0, std::initializer_list<std::string> args,
	int fd1 =DEVNULL, int fd2 =DEVNULL )
    : process(fd0, _to_vector(args).data(), fd1, fd2) {}

    process( std::initializer_list<std::string> args,
	int fd1 =DEVNULL, int fd2 =DEVNULL )
    : process(DEVNULL, _to_vector(args).data(), fd1, fd2) {}

    ~process() {}  // defined trivial not to generate implicit copy/move operations
	// Child process is not killed as process object is destroyed, so that we can 
	// pipeline multiple processes in line with creating temporary processes. Thus, 
	// explicit wait() and/or kill() is required not to make child process an orphan.
	// However, fds created for process::PIPEs are destroyed along with process 
	// object since they are embedded (as a target for standard streams) in internal 
	// actual process and do not need to be exposed outside.

    // wait for child process to terminate indefinitely.
    void wait();

    // wait for child process to terminate for the duration of timeout, returning true if 
    // terminated, or false if timed out (child process is still running).
    bool wait(const std::chrono::milliseconds& timeout);

    // bool wait(timeout) uses a busy-polling loop (non-blocking call of ::waitpid() and 
    // short sleeps) instead of signal handler for SIGCHLD. However, when multiple 
    // threads wait for the same child process at the same time, they do not race but 
    // they help each other; one of them voluntarily waits polling and the others wait 
    // just for him. If his time is up and he cannot wait any longer, another one among 
    // the others waits voluntarily in place of him, and so on.

    // check if child process has terminated, returning true if so, or false otherwise.
    bool poll();  // is the same as wait(0ms), only more optimized.

    // poll() should be used over directly inquiring the protected data member "running", 
    // because "running" holds only the information of the last poll() or wait() 
    // executed.

    // send the specified signal or SIGTERM (=15) to the child.
    void kill(int sig =SIGKILL);
};

process::process(int fd0, const char* argv[], int fd1, int fd2)
:   pipe_in  { (assert(fd0 != SAMEOUT), fd0 == DEVNULL) ? _devnull() : fd0 },
    pipe_out { (assert(fd1 != SAMEOUT), fd1 == DEVNULL) ? _devnull() : fd1 },
    pipe_err { fd2 == SAMEOUT ? pipe_out.near : fd2 == DEVNULL ? _devnull() : fd2 },
    pid { ::fork() }  // will be initialized after the above pipe_in/pipe_out/pipe_err.
{
    if ( pid == 0 )  // run in child process!
    {
	// Redirection cases:
	// case 1	// case 2	// case 3
	// 3 <- 0	// 3 <- 0	// 3 <- 0
	// 3 <- 1	// 1 <- 1	// 2 <- 1
	// 2 <- 2	// 2 <- 2	// 2 <- 2

	// case 4	// case 5	// case 6 (swapping)
	// 3 <- 0	// 3 <- 0	// 3 <- 0
	// 1 <- 1	// 3 <- 1	// 2 <- 1
	// 1 <- 2	// 1 <- 2	// 1 <- 2
	// -->		// -->		// -->
	// 3 <- 0	// 3 <- 0	// 3 <- 0
	// 1 <- 2	// 1 <- 2	// 2 <- 4
	// 1 <- 1	// 3 <- 1	// 1 <- 2
					// 4 <- 1

	// redirect child's standard streams
	pipe_in.dup2(STDIN);
	if ( pipe_err.near == STDOUT ) {    // case 4/5/6
	    if ( pipe_out.near == STDERR )  // case 6
		pipe_out.dup2();
	    pipe_err.dup2(STDERR);
	    pipe_out.dup2(STDOUT);
	} else {			    // case 1/2/3
	    pipe_out.dup2(STDOUT);
	    pipe_err.dup2(STDERR);
	}

	// We don't need any fds but stdin/stdout/stderr.
	pipe_in ._close();
	pipe_out._close();
	pipe_err._close();

#if !defined(NDEBUG) && defined(DEBUG)  // check if stdout/stderr is writable.
	dprintf(STDOUT, "Ok to write into \033[33mSTDOUT\033[0m\n");
	dprintf(STDERR, "Ok to write into \033[33mSTDERR\033[0m\n");
#endif

	if ( ::execvp(argv[0], const_cast<char**>(argv)) == -1 )
	    std::_Exit(127);  // instead of std::exit() due to no need for cleaning up.
		// returning 127 according to most shells.
    }

    else {  // run in parent process!
	if ( pid == -1 )
	    throw std::system_error(errno, std::system_category());

	// close unnecessary fds in pipes
	pipe_in ._close_near();
	pipe_out._close_near();
	pipe_err._close_near();

	running = ALONE;
    }
}

void process::wait()
{
    std::unique_lock<std::mutex> lock(mutex_running);
    not_awaited.wait(lock, [this]{ return running != AWAITED; });

    if ( running == ALONE ) {
	running = AWAITED;  // For the importance of AWAITED, see comment in poll().
	lock.unlock();  // no need to notify of AWAITED since nobody will wait for it.

	int status;
	int wpid = ::waitpid(pid, &status, 0);

	lock.lock();
	if ( wpid != -1 ) {
	    if ( WIFEXITED(status) )
		exitcode = WEXITSTATUS(status);
	    else if ( WIFSIGNALED(status) )
		exitcode = -WTERMSIG(status);
	}
	//else: Possibly, SIGCHLD's signal action is set to SIG_IGN explicitly.

	running = DONE;
	not_awaited.notify_all();
    }
}

bool process::wait(const std::chrono::milliseconds& timeout)
{
    const auto when = std::chrono::steady_clock::now() + timeout;

    std::unique_lock<std::mutex> lock(mutex_running);
    if ( !not_awaited.wait_for(lock, timeout, [this]{ return running != AWAITED; }) )
	return false;  // someone is still waiting after timed out.

    if ( running == ALONE ) {
	running = AWAITED;
	lock.unlock();  // keeping critical section as small as possible

	using namespace std::chrono_literals;
	int status, wpid;

	for ( auto dt = 1ms ; (wpid = ::waitpid(pid, &status, WNOHANG)) == 0 ; )
	// We cannot use here do ... while() for for() because we have to check 
	// ::waitpid() at least once however short the timeout is specified.
	{
	    const auto remaining =
		std::chrono::duration_cast<std::chrono::milliseconds>(
		    when - std::chrono::steady_clock::now() );
	    if ( remaining <= 0ms ) {
		// "I have no more time to wait. So, someone else wait instead please!"
		running = ALONE;
		not_awaited.notify_one();
		    // notify_one() is enough since everybody is willing to wait.
		return false;
	    }

	    std::this_thread::sleep_for(std::min(dt, remaining));
	    if ( dt < 64ms ) dt *= 2;  // wait for maximum 64ms at a time
	}

	lock.lock();
	if ( wpid != -1 ) {
	    if ( WIFEXITED(status) )
		exitcode = WEXITSTATUS(status);
	    else if ( WIFSIGNALED(status) )
		exitcode = -WTERMSIG(status);
	}
	//else: Possibly, SIGCHLD's signal action is set to SIG_IGN explicitly.

	running = DONE;
	not_awaited.notify_all();  // notify_all() since everybody wants it.
    }

    return true;
}

bool process::poll()
{
    std::lock_guard<std::mutex> lock(mutex_running);

    if ( running == ALONE ) {
	int status;
	int wpid = ::waitpid(pid, &status, WNOHANG);
	    // We do not lock mutex here since ::waitpid() returns immediately. 
	    // Otherwise, we should do "running = AWAITED" before unlocking mutex so that 
	    // other threads cannot run ::waitpid() until we set back "running = ALONE" 
	    // though they can still acquire mutex. (If there are two calls of 
	    // ::waitpid() and the first call happens to succeed, the second call will 
	    // result in ECHILD, which means child process has been released and the 
	    // parent has no child with the old child pid.) In this regard, we can think 
	    // of "running = AWAITED" as a soft kind of mutex.

	if ( wpid == 0 ) return false;
	if ( wpid != -1 ) {
	    if ( WIFEXITED(status) )
		exitcode = WEXITSTATUS(status);
	    else if ( WIFSIGNALED(status) )
		exitcode = -WTERMSIG(status);
	}
	//else: Possibly, SIGCHLD's signal action is set to SIG_IGN explicitly.

	running = DONE;
	not_awaited.notify_all();
    }

    return running == DONE;
}

void process::kill(int sig)
{
    if ( !poll() )
	// We use !poll() instead of "running != DONE" to be able to kill otherwise 
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
int process::_devnull() {
    static int fd_devnull;  // initialized lazily
    static std::once_flag one_time;

    std::call_once(one_time, []{
    // provents multiple threads from running ::open() below at the same time.
	fd_devnull = ::open("/dev/null", O_RDWR);
	if ( fd_devnull == -1 )
	    throw std::system_error(errno, std::system_category());
    });

    return fd_devnull;
}



#include "fdstream.hpp"
#include <iostream>

#if 0  // simple command
int main()
{
    process proc { "sleep", "3" };
    proc.wait();  // not to make proc an orphan process
}
#endif

#if 0  // run in one line
int main()
{
    process { "sleep", "3" }.wait();
}
#endif

#if 0  // command for output
int main()
{
    process proc { { "ls", "-l" }, process::STDOUT };
    proc.wait();
}
#endif

#if 0  // abbreviation for process
int main()
{
    using proc = process;
    proc ls { { "ls", "-l" }, proc::STDOUT };
    ls.wait();
}
#endif

#if 0  // run via shell
int main()
{
    process { { "/bin/sh", "-c", "ls -l $HOME" }, process::STDOUT }.wait();
}
#endif

#if 0  // command for input
int main()
{
    std::cout << "Enter lines to write into /tmp/test.txt and ^D:\n";
    process { process::STDIN, { "/bin/sh", "-c", "cat >/tmp/test.txt" } }.wait();
}
#endif

#if 0  // command for input and output
int main()
{
    process proc { process::STDIN, { "sort" }, process::STDOUT };
    std::cout << "Enter lines to sort and ^D:\n";
    proc.wait();
}
#endif

#if 0  // output to stderr only
int main()
{
    process { { "ls", "-y" }, process::DEVNULL, process::STDERR }.wait();
    // will output error messages from `ls -y` to stderr.
}
#endif

#if 0  // redirect stderr
int main()
{
    process { { "ls", "-y" }, process::DEVNULL, process::STDOUT }.wait();
    // will output error messages from `ls -y` to stdout.
}
#endif

#if 0  // combine stdout and stderr into stdout
int main()
{
    process { { "ls", "-y" }, process::STDOUT, process::SAMEOUT }.wait();
}
#endif

#if 0  // exchange stdout and stderr
int main()
{
    process { { "ls", "-l" }, process::STDERR, process::STDOUT }.wait();
}
#endif

#if 0  // piped output
int main()
{
    process proc { { "ls", "-l" }, process::PIPE };
    ifdstream<> is { proc.stdout };

    std::string s;
    while ( getline(is, s) )
	std::cout << '[' << s << "]\n";

    proc.wait();
}
#endif

#if 0  // piped output with stdout and stderr combined
int main()
{
    process proc { { "ls", "-y" }, process::PIPE, process::SAMEOUT };
	// proc.stderr goes to the same pipe of proc.stdout.
    ifdstream<> is { proc.stdout };

    std::string s;
    while ( getline(is, s) )
	std::cout << '[' << s << "]\n";

    proc.wait();
}
#endif

#if 0  // piped input
int main()
{
    process proc { process::PIPE, { "sort" }, process::STDOUT };
    ofdstream<> is { proc.stdin };

    is << "line 2\n" << "line 1\n";
    is.close();

    proc.wait();
}
#endif

#if 0  // piped input and output
int main()
{
    process proc { process::PIPE, { "sort" }, process::PIPE };
    ofdstream<> os { proc.stdin };
    ifdstream<> is { proc.stdout };

    os << "line 2\n" << "line 1\n";
    os.close();

    std::string s;
    while ( getline(is, s) )
	std::cout << '[' << s << "]\n";

    proc.wait();
}
#endif

#if 0  // constructor syntax
int main()
{
    //process proc { { "sleep", "3" }, process::DEVNULL };  // ok
    //process proc ( { "sleep", "3" }, process::DEVNULL );  // ok
    //process proc { process::DEVNULL, { "sleep", "3" } };  // ok
    //process proc { { std::string("sleep"), std::string("3") } };  // ok
    process proc { "sleep", "3" };  // ok
    //process proc { { "sleep", "3" } };  // error in g++
	// because the outer (not inner) brace is taken as the first parameter 
	// initializer_list<string> containing a single string, which is { "sleep", "3" 
	// }.

    proc.wait();
    std::cout << "done\n";
}
#endif

#if 0  // pipeline
int main()
{
    process proc1 { { "ls", "-l" }, process::PIPE };
    process proc2 { proc1.stdout, { "sort", "-n", "-k5" }, process::PIPE };
    process proc3 { proc2.stdout, { "grep", "cpp" }, process::STDOUT };

    proc3.wait();
}
#endif

#if 1  // pipeline in one statement
int main()
{
    process proc {
	process{
	    process{ { "ls", "-l" }, process::PIPE }.stdout,
	    { "sort", "-n", "-k5" }, process::PIPE  // sort by file size
	}.stdout,
	{ "grep", "cpp" }, process::STDOUT
    };

    proc.wait();  // waiting for the outermost process "grep".
}
#endif

#if 0  // Temporary (unbound) process object closes all its unused pipes when destroyed.
int main()
{
    process proc {
	process { process::PIPE, { "cat" }, process::PIPE }.stdout,
	    // The stdin for "cat" process gets closed automatically as the temporary 
	    // (unbound) process object is destroyed.
	{ "sort" }, process::STDOUT
    };
    // The stdin for proc is unspecified and so having fd of "/dev/null".

    proc.wait();
}
#endif

#if 0  // pipeline with piped input
int main()
{
    process proc1 { process::PIPE, { "cat" }, process::PIPE };
    ofdstream<> os { proc1.stdin };
    os << "line 2\n" << "line 1\n";

    process proc2 { proc1.stdout, { "sort" }, process::STDOUT };
    os.close();  // Without it, proc1 (and proc2 as well) will not end.

    // [Note that proc1.stdin is created with O_CLOEXEC on. Otherwise, when creating 
    // proc2, proc2 would inherit proc1.stdin that was not closed yet and would keep 
    // proc1.stdin open within its process, with which proc1 would not terminate even 
    // after the later os.close().]

    proc1.wait();
    proc2.wait();
}
#endif

#if 0  // ownership of fds created
int main()
{
    process proc1 { process::STDIN, { "cat" }, process::PIPE };
    process proc2 { proc1.stdout, { "sort" }, process::STDOUT };

    ofdstream<> os { proc1.stdin };  // will craete a void stream.
    // process::STDIN == 1 (by default), but proc1.stdin == -1, which means proc1 does 
    // not own the given fd of process::STDIN and does not close it when proc1 is 
    // destroyed. Similarly, proc2.stdin equals -1 and is different from proc1.stdout 
    // that is created in proc1 although proc2 uses in itself proc1.stdout taken as its 
    // argument. This way, proc1 can close proc1.stdout on destroying, but proc2 cannot.

    std::cout << "Enter lines to sort and ^D:\n";
    proc1.wait();
    proc2.wait();
}
#endif

#if 0  // point of running
int main()
{
    process proc1 { "sleep", "3" };
    process proc2 { process::PIPE, { "cat" }, process::STDOUT };
    // proc1 starts to run on creating the process, and so does proc2. Both in 
    // background. However, proc2 will stop immediately waiting for input from its stdin.

    ofdstream<> os { proc2.stdin };
    os << "line 1\n";
    os.close();  // Now proc2 will see EOF on its stdin and terminate.
		 // Without it, proc2 will not terminate.

    proc2.wait();
}
#endif

#if 0  // buffering
int main()
{
    process proc { process::PIPE, { "cat" }, process::STDOUT };
    ofdstream<> os { proc.stdin };

    os << "line 1\n" << std::flush;   // line will go into proc at the std::flush,
    os << "line 2" << std::endl;      // or will go into proc at the std::endl.
    os << "line 3\n";		      // "line 3" will not go until os.close().
				      // Note, C++ has only std::cout line-buffered.
    process { "sleep", "1" }.wait();  // wait and sleep for 1 sec.
    os.close();
    proc.wait();
}
#endif

#if 0  // no buffering
int main()
{
    process proc { process::PIPE, { "cat" }, process::STDOUT };
    ofdstream<> os { proc.stdin, 0 };  // no buffering mode

    os << "line 1\n";
    os.put('l');
    os << "ine 2";		       // chars will go into proc immdediately.
    process { "sleep", "1" }.wait();
    os.close();
    proc.wait();
}
#endif

#if 0  // automatic flushing
int main()
{
    process proc { process::PIPE, { "cat" }, process::STDOUT };
    ofdstream<> os { proc.stdin };

    std::unitbuf(os);  //Or, os << std::unitbuf;
    os << "line 1\n";  // will be flushed after each output operation.
    os << "line 2";
    process { "sleep", "1" }.wait();
    os.close();
    proc.wait();
}
#endif

#if 0  // tied fdstreams
int main()
{
    process proc { process::PIPE, { "cat" }, process::PIPE };
    ofdstream<> os { proc.stdin };
    ifdstream<> is { proc.stdout };
    is.tie(&os);  // os will get flush()ed before any input operation on is.

    os << "Hello, ";
    os << "World.\n";
    os << "done\n";  // Without the tie() above, we should flush os ourselves.

    std::string s;
    while ( getline(is, s) ) {
	std::cout << '[' << s << "]\n";
	if ( s == "done" )
	    break;
    }
    process { "sleep", "1" }.wait();

    os.close();
    proc.wait();
}
#endif

#if 0  // sharing of fds
int main()
{
    process proc2 { process::PIPE, { "cat" }, process::STDOUT };
    process proc1 { { "ls", "-l" }, proc2.stdin };
    ofdstream<> os { proc2.stdin, 0 };
    os << "line inserted\n";
    // proc2.stdin is shared between os and proc1 so that both can write into it, just 
    // like parent process and child process can share the same process::STDIN.

    os.close();  // closes proc2.stdin to terminate proc2.
    proc2.wait();
}
#endif

#if 0  // redirect stdout to a file
int main()
{
    ofdstream<> os { "/tmp/test.txt" };
    if ( os ) {
	process proc { { "ls", "-l" }, os.fd() };
	proc.wait();
	std::cout << "done\n";
    }
}
#endif

#if 0  // redirect stdout to a file
int main()
{
    ofdstream<> os { "/tmp/test.txt" };
    if ( os ) {
	process proc { process::PIPE, { "cat" }, os.fd() };
	ofdstream<> os { proc.stdin };
	assert( proc.stdin == os.fd() );  // is true.

	os << "line 2\n" << "line 1\n";
	os.close();
	proc.wait();
	std::cout << "done\n";
    }
}
#endif

#if 0  // redirect stdout to ofstream
int main()
{
    std::ofstream os { "/tmp/test.txt" };
    if ( os ) {
	process proc { { "ls", "-l" }, fd(os) };
	    // ::fd() returns the underlying fd for ifstream/ofstream/fstream.
	proc.wait();
	std::cout << "done\n";
    }
}
#endif

#if 0  // defunct process
int main()
{
    process proc { "sleep", "1" };  // Note no wait for proc.
	// proc becomes a defunct process until main process terminates.
    process { "sleep", "10" }.wait();
    std::cout << "done\n";
}
#endif

#if 0  // timed wait
int main()
{
    using namespace std::chrono_literals;
    process proc { "sleep", "10" };

    bool done = proc.wait(1s);
	// proc becomes an orphan process after main process terminates.

    if ( done )
	std::cout << "done\n";
    else
	std::cout << "child process with pid " << proc.pid << " left behind.\n";
}
#endif

#if 0  // simultaneous wait() and wait(timeout)
void func1(process& proc)
{
    std::cout << "func1(): start\n";
    proc.wait();
    std::cout << "func1(): end w/exitcode=" << proc.exitcode << "\n";
}

void func2(process& proc)
{
    using namespace std::chrono_literals;

    std::cout << "func2(): start\n";
    while ( !proc.wait(1s) )
	std::cout << "func2(): still running\n";
    std::cout << "func2(): end w/exitcode=" << proc.exitcode << "\n";
}

int main()
{
    process proc { "sleep", "5" };

    std::thread t2 { func2, std::ref(proc) };
    std::thread t1 { func1, std::ref(proc) };

    t1.join();
    t2.join();
}
#endif

#if 0  // test wait() and poll()
process proc { { "sleep", "5" } };

void func1()
{
    std::printf("func1(): start\n");
    proc.wait();
    std::printf("func1(): end (exitcode=%d)\n", proc.exitcode);
}

void func2()
{
    using namespace std::chrono_literals;

    std::printf("func2(): start\n");
    while ( !proc.poll() ) {
	std::this_thread::sleep_for(1s);
	std::printf("func2(): still running\n");
    }
    std::printf("func2(): end (exitcode=%d)\n", proc.exitcode);
}

int main()
{
    std::thread t2 { func2 };
    std::thread t1 { func1 };

    t1.join();
    t2.join();
}
#endif

#if 0  // test kill()
process proc { { "sleep", "5" } };

void func1()
{
    std::printf("func1(): start\n");
    proc.wait();
    std::printf("func1(): end (exitcode=%d)\n", proc.exitcode);
}

void func2()
{
    using namespace std::chrono_literals;

    std::printf("func2(): start\n");
    while ( !proc.wait(1s) ) {
	std::printf("func2(): still running\n");
	proc.kill();
	std::printf("func2(): process killed\n");
/*
	for ( int i = 0 ; i < 100 ; i++ ) {
	    std::this_thread::sleep_for(1us);
	    proc.kill(SIGKILL);
	    // We still have a chance to get ESRCH (No such process).
	}
*/
    }
    std::printf("func2(): end (exitcode=%d)\n", proc.exitcode);
}

int main()
{
    std::thread t2 { func2 };
    std::thread t1 { func1 };

    t1.join();
    t2.join();
}
#endif
