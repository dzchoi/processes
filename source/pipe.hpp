// Reference:
// - Pipes, forks, and dups: http://www.rozmichelle.com/pipes-forks-dups/
// - Preventing file descriptor leaks to child processes:
// https://docs.fedoraproject.org/en-US/Fedora_Security_Team/1/html/Defensive_Coding/sect-Defensive_Coding-Tasks-Descriptors-Child_Processes.html



#pragma once

#include <system_error>  // system_error(), system_category(), errno

extern "C" {
#include <unistd.h>  // pipe2(), close(), dup2(), dup(), STDERR_FILENO
#include <fcntl.h>  // O_CLOEXEC
}



// _pipe<> creates a pipe/redirection that a standard IO stream (stdin/stdout/stderr) of 
// a child process can be associated with.
// - _pipe<>(-1) creates a pipe with new fds allocated by system, and _pipe<>(fd) creates 
//   a redirection to the specified fd (fd >= 0).
// - _pipe<false>() for child process's stdin, and _pipe<true>() is for child process's 
//   stdout/stderr.
// - _pipe<>::~_pipe() removes the created pipe, or does nothing for redirection.
// - _pipe<>::dup2(fd) links/redirects the given fd (of stdin/stdout/stderr) of child 
//   process to the pipe or to the redirection's fd.
// - _pipe<>::dup2() is used for duplicating a fd, when exchaning stdout and stderr of 
//   child process.

template <bool Behind>
// "Behind" is true if pipe locates behind child process, or false if ahead of child.
class _pipe {
    friend class process;

private:
    int fds[2] = { -1, -1 };  // fds[0] is reading end of pipe and fds[1] is writing end.

protected:
    int& near = fds[Behind];
	// near end of pipe from child process, or fd that a child's standard stream is 
	// redirected to
    int& far = fds[!Behind];
	// far end of pipe from child process, or -1 for redirection

    // No copy/move constructor/assignment provided.
    _pipe(int fd);  // will create a pipe if fd < 0, or a redirection otherwise.
    ~_pipe() { close(); }  // remove the pipe, or do nothing for redirection.
	// Note that pipe gets deleted discarding any remaining data in it, when all fds 
	// associated with the pipe are closed.

    // operations for parent process
    void _close_near(); // close near for the pipe created.
    void close();	// close near and far for the pipe created.

    // opeartions for child process
    void dup2(int fd);  // link/redirect the given fd to near.
    void dup2();	// duplicate near and overwrite near.
    void _close();	// close near and far forcilby (if >= 0).
};

template <bool Behind>
_pipe<Behind>::_pipe(int fd)
{
    if ( fd >= 0 )
	near = fd;

    else if ( ::pipe2(fds, O_CLOEXEC) != 0 )  // create a pipe.
	// ::pipe() will not modify fds[] on failure.
	// We create far (and near) with O_CLOEXEC on not to be inherited by child 
	// process unless specified for redirection target of child process.
	throw std::system_error(errno, std::system_category());

    // We always have near >= 0 here, but far may or may not be >= 0, and
    // near and far are unique if far >= 0.
}

template <bool Behind>
void _pipe<Behind>::_close_near()
{
    if ( far >= 0 ) {
	::close(near);
	near = -1;
    }
}

template <bool Behind>
void _pipe<Behind>::close()
{
    if ( far >= 0 ) {
	::close(far);
	far = -1;

	if ( near >= 0 ) {  // needed because near might be closed using _close_near().
	    ::close(near);
	    near = -1;
	}
    }
}

template <bool Behind>
void _pipe<Behind>::dup2(int fd)
{
    if ( ::dup2(near, fd) == -1 )
	// will yield -1 if near or fd < 0, and do nothing if near == fd.
	throw std::system_error(errno, std::system_category());
}

template <bool Behind>
void _pipe<Behind>::dup2()
{
    near = ::dup(near);
    if ( near == -1 )
	throw std::system_error(errno, std::system_category());
}

template <bool Behind>
inline void _pipe<Behind>::_close()
{
    if ( far < 0 && near > STDERR_FILENO )
	// If far >= 0, we don't need to close near and far since they are both created 
	// with O_CLOEXEC on and thus will be closed automatically on exec*().
	::close(near);
	// may cause -1 error if some nears are the same, which we can ignore safely.

    // When closing any near (or far), we do not set it here back to -1 to prevent it 
    // from closing again, because the exec*() called from child process will replace the 
    // whole parent process image with a new process image.

/* Or, we could blindly close all:
    if ( near > STDERR_FILENO )
	::close(near);
	// may cause -1 error if some nears are the same, which we can ignore safely.
    if ( far > STDERR_FILENO )
	::close(far);
*/
}
