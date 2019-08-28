// process.cpp + pipe.hpp

// based on ideas from:
// - https://gist.github.com/konstantint/d49ab683b978b3d74172
// - https://github.com/tsaarni/cpp-subprocess

#include <ext/stdio_filebuf.h>  // stdio_filebuf<>, BUFSIZ, exit()
    // __gnu_cxx::stdio_filebuf<> -> std::basic_filebuf<> -> std::basic_streambuf<>
#include <istream>
#include <ostream>
#include <string>  // string
#include <vector>  // vector<>, .push_back(), .c_str()
#include <system_error>  // system_error(), system_category(), errno

extern "C" {
#include <fcntl.h>  // O_RDWR
#include <unistd.h>
    // pipe(), open(), close(), pid_t, fork(), dup2(), STD*_FILENO, execvp()
}



class process {
private:
    template <typename =void>  // bogus template for having the definition in .hpp
    static int devnull();

    void close_fds();

protected:
    pid_t pid;

    enum { READ = 0, WRITE = 1 };  // either end of pipe

    int fd_stdin[2]  = { fd_NIL, fd_NIL };
    int fd_stdout[2] = { fd_NIL, fd_NIL };
    int fd_stderr[2] = { fd_NIL, fd_NIL };

public:
    enum {
	PIPE   = -1,
	fd_NIL = PIPE,  // should equal PIPE.
	// Todo: protected: for fd_NIL
	STDIN  = STDIN_FILENO,
	STDOUT = STDOUT_FILENO,
	STDERR = STDERR_FILENO
    };

    // Only far ends of pipes from child process are provided in public. (Near ends are 
    // deleted after spawning child.)
    const int& stdin  = fd_stdin[WRITE];
    const int& stdout = fd_stdout[READ];
    const int& stderr = fd_stderr[READ];

    process( const std::vector<std::string>& argv,
	int stdin =devnull(), int stdout =devnull(), int stderr =devnull() );

    ~process() { close_fds(); }  // Todo: wait for child process?

    template <typename =void>  // bogus template for having the definition in .hpp
    static void close_fd(int& fd);

    // Todo: Provide:
    // - class pipe<bool towards_process>
    // - pipe<True> stdin; pipe<False> stdout;
    // - pipe stdin(devnull()): redirection
    // - pipe stdin(STDOUT_FILENO)
    // - pipe stdin(std::cout)
    // - pipe stdin(const pipe&): redirection
    // - pipe stdin(): creating pipe (in place) using dup2()
    // - int stdin._near_fd, stdin._far_fd
    // - void stdin.close(): close far-end fd
    // - void stdin._close_near_fd(): close near-end fd (if far-end is not Nil)
    // - void stdin._close_all_fds()
    // - ostream&& stdin.stream(size_t =BUFSIZ)
    // - istream&& stdout.stream(size_t =BUFSIZ)
};

template <typename>
int process::devnull() {
    static int fd_devnull = -1;

    if ( fd_devnull == -1 ) {
	fd_devnull = open("/dev/null", O_RDWR);
	if ( fd_devnull == -1 )
	    throw std::system_error(errno, std::system_category());
    }

    return fd_devnull;
}

template <typename>
void process::close_fd(int& fd) {
    if ( fd != fd_NIL )
	close(fd);
    fd = fd_NIL;
}

void process::close_fds()
{
    // delete only such fds created within this class
    if ( fd_stdin[WRITE] != fd_NIL ) {
	close_fd(fd_stdin[READ]);
	close_fd(fd_stdin[WRITE]);
    }
    if ( fd_stdout[READ] != fd_NIL ) {
	close_fd(fd_stdout[READ]);
	close_fd(fd_stdout[WRITE]);
    }
    if ( fd_stderr[READ] != fd_NIL ) {
	close_fd(fd_stderr[READ]);
	close_fd(fd_stderr[WRITE]);
    }
}

process::process(
    const std::vector<std::string>& argv, int stdin, int stdout, int stderr)
// Todo: parameter pack for options
{
    /* We should go:
    if stdin == STDIN_FILENO:
	- fd_stdin[WRITE] = fd_stdin[READ] = fd_NIL;
	- Parent, Child: do nothing

    if stdin == devnull() or others:
	- fd_stdin[WRITE] = fd_NIL; fd_stdin[READ] = stdin;
	- Parent: do not close fd_stdin[READ]
	- Child: dup2(fd_stdin[READ], STDIN_FILENO);
		 close(fd_stdin[READ]);

    if stdin == PIPE:
	- pipe(fd_stdin);
	- Parent: close(fd_stdin[READ]); save fd_stdin[WRITE]
	- Child: dup2(fd_stdin[READ], STDIN_FILENO);
		 close(fd_stdin[READ]); close(fd_stdin[WRITE]);
	- ...
	- Parent (when done): close(fd_stdin[WRITE]);
    */

    if ( stdin != STDIN )
	fd_stdin[READ]   = stdin;  // We optimizes here using fd_NIL == PIPE.
    if ( stdout != STDOUT )
	fd_stdout[WRITE] = stdout;
    if ( stderr != STDERR )
	fd_stderr[WRITE] = stderr;

    if ( (stdin  == PIPE && pipe(fd_stdin)  != 0)
      || (stdout == PIPE && pipe(fd_stdout) != 0)
      || (stderr == PIPE && pipe(fd_stderr) != 0)
      || (pid = fork()) == -1 )
    {
	// pipe() will not modify fds on failure.
	close_fds();
	throw std::system_error(errno, std::system_category());
    }
    // We will have cases for fd_stdin[]:
    // - fd_stdin[WRITE] == fd_NIL && fd_stdin[READ] == fd_NIL:
    //   child inherits the stdin from parent.
    // - fd_stdin[WRITE] != fd_NIL && fd_stdin[READ] == fd_NIL:
    //   child's stdin is redirected to fd_stdin[WRITE].
    // - fd_stdin[WRITE] != fd_NIL && fd_stdin[READ] != fd_NIL:
    //   child's stdin is linked to fd_stdin[WRITE] through a pipe.
    //   (fd_stdin[READ] will be substituted for child's stdin and then be closed.)

    if ( pid != 0 ) {  // in parent!
	// close unnecessary fds (near ends of pipes from child process) when piping
	if ( fd_stdin[WRITE] != fd_NIL )
	    close_fd(fd_stdin[READ]);
	if ( fd_stdout[READ] != fd_NIL )
	    close_fd(fd_stdout[WRITE]);
	if ( fd_stderr[READ] != fd_NIL )
	    close_fd(fd_stderr[WRITE]);
    }

    else {  // in child!
	// redirect stdin/stdout/stderr when redirection or piping
	if ( fd_stdin[READ]   != fd_NIL )
	    dup2(fd_stdin[READ],   STDIN);
	if ( fd_stdout[WRITE] != fd_NIL )
	    dup2(fd_stdout[WRITE], STDOUT);
	if ( fd_stderr[WRITE] != fd_NIL )
	    dup2(fd_stderr[WRITE], STDERR);

	// We don't need all fds from parent now.
	close_fds();  // Todo: need to close all

	std::vector<char*> cargv;
	for ( const auto& each: argv )
	    cargv.push_back(const_cast<char*>(each.c_str()));
	cargv.push_back(nullptr);

	if ( execvp(cargv[0], &cargv[0]) == -1 ) {
	    std::printf("Error: child execute fails\n");  // debugging
	    exit(-1);  // Todo: set error status!
	}
    }
}



template <typename CharT =char, typename Traits =std::char_traits<CharT>>
class ofdstream: public std::basic_ostream<CharT, Traits> {
protected:
    std::basic_streambuf<CharT, Traits>* rdbuf;

public:
    ofdstream(int fd, size_t bufsize =static_cast<size_t>(BUFSIZ))
    : std::basic_ostream<CharT, Traits> {
	rdbuf = ( fd == process::fd_NIL
	    ? nullptr
	    : new __gnu_cxx::stdio_filebuf<CharT, Traits>(fd, std::ios::out, bufsize) )
    } {}

    void close();  // to close the underlying fd just as ofstream does
	// Todo: How to set the associated fd to fd_NIL?
    ~ofdstream() { close(); }
};

template <typename CharT, typename Traits>
void ofdstream<CharT, Traits>::close()
{
    this->setstate(std::ios_base::eofbit);  // makes the stream unusable after .close()
    delete rdbuf;
    rdbuf = nullptr;
}



template <typename CharT =char, typename Traits =std::char_traits<CharT>>
class ifdstream: public std::basic_istream<CharT, Traits> {
protected:
    std::basic_streambuf<CharT, Traits>* rdbuf;

public:
    ifdstream(int fd, size_t bufsize =static_cast<size_t>(BUFSIZ))
    : std::basic_istream<CharT, Traits> {
	rdbuf = ( fd == process::fd_NIL
	    ? nullptr
	    : new __gnu_cxx::stdio_filebuf<CharT, Traits>(fd, std::ios::in, bufsize) )
    } {}

    void close();
    ~ifdstream() { close(); }  // Todo: works well?
};

template <typename CharT, typename Traits>
void ifdstream<CharT, Traits>::close()
{
    this->setstate(std::ios_base::eofbit);
    delete rdbuf;
    rdbuf = nullptr;
}



#include <iostream>

#if 0
int main()
{
    process proc { {"ls", "-alF"}, process::devnull(), process::PIPE };

    ifdstream<char> xxx { proc.stdout };
    while ( xxx ) {
	std::string s;
	getline(xxx, s);
	std::cout << s << "\n";
    }
}
#endif

#if 0
int main()
{
    process proc { { "tee", "/tmp/test.txt" }, process::PIPE };
    ofdstream<char> xxx { proc.stdin };
    xxx << "Hello\n";
    xxx << "World\n";
}
#endif

#if 1
int main()
{
    process proc { { "cat" }, process::PIPE, process::PIPE };
    ofdstream<char> xxx { proc.stdin, 0 };
    ifdstream<char> yyy { proc.stdout };
    //yyy.tie(&xxx);  // Todo: segmentation fault error (why?)

    xxx << "Hello\n";
    xxx << "tt";
    xxx.close();

    while ( yyy ) {
	std::string s;
	getline(yyy, s);
	std::cout << s << "\n";
    }
}
#endif
