// clang++ test.process.cpp -lpthread



#include <chrono>

inline std::chrono::time_point<std::chrono::steady_clock> now()
{
    return std::chrono::steady_clock::now();
}

inline int duration_ms(const std::chrono::time_point<std::chrono::steady_clock>& since)
{
    return
        std::chrono::duration_cast<std::chrono::milliseconds>(now() - since).count();
}



#include "process.hpp"
#include "fdstream.hpp"
#include <string>
#include <iostream>



void simple_sleep()
{
    process proc { "sleep", "3" };
    proc.wait();  // not to make proc an orphan process
}

void test_simple_sleep()
{
    const auto since = now();
    std::thread sleep { simple_sleep };
    sleep.join();

    const int elapsed = duration_ms(since);
    assert( elapsed >= 3000 && elapsed <= 3010 );
    std::cout << "elapsed = " << elapsed << '\n';
}



void simple_sleep_in_one_line()
{
    process { "sleep", "3" }.wait();
}

void test_simple_sleep_in_one_line()
{
    const auto since = now();
    std::thread sleep { simple_sleep_in_one_line };
    sleep.join();

    const int elapsed = duration_ms(since);
    assert( elapsed >= 3000 && elapsed <= 3010 );
    std::cout << "elapsed = " << elapsed << '\n';
}



void test_piped_output()
{
    process ps { { "ps" }, process::PIPE };
    assert( ps.poll() == false );

    process grep { ps.stdout, { "grep", std::to_string(ps.pid) }, process::PIPE };
    assert( grep.poll() == false );

    ifdstream<> is { grep.stdout };

    std::string s;
    std::getline(is, s);
    std::cout << s << '\n';
    assert( ps.pid == std::stoi(s) );
    assert( is.peek() == EOF );

    ps.wait();
    grep.wait();
    assert( ps.exitcode == 0 && grep.exitcode == 0 );
}

int main()
{
    test_simple_sleep();
    test_simple_sleep_in_one_line();
    test_piped_output();
}



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
    process { { "bash", "-c", "time ls -l" }, process::DEVNULL, process::STDERR }.wait();
    // will output only timing statistics messages to stderr.
}
#endif

#if 0  // redirect stderr
int main()
{
    process { { "bash", "-c", "time ls -l" }, process::DEVNULL, process::STDOUT }.wait();
    // will output only timing statistics messages to stdout.
}
#endif

#if 0  // combine stdout and stderr into stdout
int main()
{
    process { { "bash", "-c", "time ls -l" }, process::STDOUT, process::SAMEOUT }.wait();
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
    process proc { { "bash", "-c", "time ls -l" }, process::PIPE, process::SAMEOUT };
        // proc.stderr goes to the same pipe for proc.stdout.
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
    //process proc { { "sleep", std::string("3") } };  // ok
    //process proc { { std::string("sleep"), "3" } };  // ok
    process proc { "sleep", "3" };  // ok
    //process proc { { "sleep", "3" } };  // run-time error in g++
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

#if 0  // pipeline in one statement
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
    os << "line 3\n";                 // "line 3" will not go until os.close().
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
    os << "ine 2";                     // chars will go into proc immdediately.
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
    std::cout << "func1(): done w/exitcode=" << proc.exitcode << "\n";
}

void func2(process& proc)
{
    using namespace std::chrono_literals;

    std::cout << "func2(): start\n";
    while ( !proc.wait(1s) )
        std::cout << "func2(): still running\n";
    std::cout << "func2(): done w/exitcode=" << proc.exitcode << "\n";
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

#if 0  // same as above, but using poll() instead of wait(timeout)
void func1(process& proc)
{
    std::cout << "func1(): start\n";
    proc.wait();
    std::cout << "func1(): done w/exitcode=" << proc.exitcode << "\n";
}

void func2(process& proc)
{
    using namespace std::chrono_literals;

    std::cout << "func2(): start\n";
    while ( !proc.poll() ) {
        std::this_thread::sleep_for(1s);
        std::cout << "func2(): still running\n";
    }
    std::cout << "func2(): done w/exitcode=" << proc.exitcode << "\n";
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

#if 0
void func(process& proc, int n)
{
    std::cout << n << " starts.\n";
    proc.wait();
    std::cout << n << " done.\n";
}

int main()
{
    process proc { "sleep", "10" };
    std::thread t[1000];
    for ( int i = 0 ; i < 1000 ; ++i )
        t[i] = std::thread { func, std::ref(proc), i };
    for ( int i = 0 ; i < 1000 ; ++i )
        t[i].join();
}
#endif

#if 0  // kill process with SIGKILL
void func(process& proc)
{
    std::cout << "func(): start\n";
    proc.wait();
    std::cout << "func(): done w/exitcode=" << proc.exitcode << "\n";
}

int main()
{
    process proc { "sleep", "10" };
    std::thread t { func, std::ref(proc) };
    process { "sleep", "1" }.wait();
    proc.kill();
    t.join();
    std::cout << "done\n";
}
#endif

#if 0  // move constructor
int main()
{
    std::vector<process> procs;

    for ( int i = 1 ; i <= 5 ; ++i )
        //procs.push_back(  // does not work due to having no default constructor.
        procs.emplace_back(
            std::initializer_list<std::string>{ "sleep", std::to_string(i) });
            // std::initializer_list<> is needed for template parameter of emplace_back() 
            // to recognize it properly.

    for ( auto& p: procs )
        p.wait();
}
#endif

#if 0  // error
void func(process& proc)
{
    std::cout << "func(): start\n";
    proc.wait();
    std::cout << "func(): done w/exitcode=" << proc.exitcode << "\n";
}

int main()
{
    process proc { "sleep", "5" };
    std::thread t { func, std::ref(proc) };
    process { "sleep", "0.1" }.wait();

    process proc2 { std::move(proc) };
        // modifies here proc which is being accessed by func().
    proc2.wait();
    t.join();
}
#endif
