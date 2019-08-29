// Reference:
// - Pipes, forks, and dups: http://www.rozmichelle.com/pipes-forks-dups/
// - Preventing file descriptor leaks to child processes:
// https://docs.fedoraproject.org/en-US/Fedora_Security_Team/1/html/Defensive_Coding/sect-Defensive_Coding-Tasks-Descriptors-Child_Processes.html



#pragma once

#include <cassert>  // assert()
#include <system_error>  // system_error(), system_category(), errno

extern "C" {
#include <fcntl.h>  // O_CLOEXEC
#include <unistd.h>  // dup(), dup2(), pipe2(), close(), STD*_FILENO
}



// _pipe<> creates a pipe/redirection that a standard stream (stdin/stdout/stderr) of a 
// child process can be associated with.

template <bool Back>
// "Back" is true if pipe locates behind a child process, or false if ahead of the child.
class _pipe {
    friend class process;

private:
    int end[2] = { -1, -1 };
        // end[0] is reading end of pipe and end[1] is writing end.

protected:
    int& near = end[Back];
        // near end to child process for pipe, or fd for redirection that a stream of 
        // child is redirected to
    int& far = end[!Back];
        // far end from child process for pipe, or -1 for redirection

    _pipe(int fd);  // will create a pipe if fd < 0, or a redirection otherwise.

    // For use only from parent process
    ~_pipe();  // closes near for pipe, or does nothing for redirection.
        // Note that pipe itself gets deleted (discarding any remaining data in it), when 
        // both ends (and all other fds associated with the pipe) are closed.

    // For use only from child process
    void dup2(int fd);  // links/redirects the given fd to near.
    void dup2();        // duplicates near and overwrites near with the new fd.
    void close();       // closes near and far (if >= 0).
};

template <bool Back>
_pipe<Back>::_pipe(int fd)
{
    if ( fd >= 0 )  // for redirection
        near = fd;

    else if ( ::pipe2(end, O_CLOEXEC) != 0 )  // creates a pipe.
        // ::pipe2() will not modify end[] if fails.
        // We here create far and near fds with O_CLOEXEC set, to not be inherited by 
        // child process (unless associated with other non-O_CLOEXEC fds).
        throw std::system_error(errno, std::system_category());

    // Note we always have near >= 0 here but far may or may not be >= 0, and near and 
    // far are unique once far >= 0.
}

template <bool Back>
_pipe<Back>::~_pipe()
{
    if ( far >= 0 ) {
        ::close(near);
        near = -1;  // not needed necessarily
    }
}

template <bool Back>
void _pipe<Back>::dup2(int fd)
{
    if ( ::dup2(near, fd) == -1 )
        // will yield -1 if near or fd < 0, and does nothing if near == fd.
        throw std::system_error(errno, std::system_category());
}

template <bool Back>
void _pipe<Back>::dup2()
{
    assert( near == STDIN_FILENO || near == STDOUT_FILENO || near == STDERR_FILENO );
        // Otherwise, the old near may not be closed on _pipe<>::close().
    near = ::dup(near);
    if ( near == -1 )
        throw std::system_error(errno, std::system_category());
}

template <bool Back>
void _pipe<Back>::close()
{
    if ( far < 0 && near > STDERR_FILENO )
        // If far >= 0, we don't need to close near and far since they are both created 
        // with O_CLOEXEC set and thus will be closed automatically on exec*().
        ::close(near);
        // may cause -1 error if some nears are the same, which we can ignore safely.

/* Or, we could close all blindly:
    if ( near > STDERR_FILENO )
        ::close(near);
    if ( far > STDERR_FILENO )
        ::close(far);
*/
}
