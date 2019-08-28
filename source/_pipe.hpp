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



// _pipe<> creates a pipe/redirection that a standard stream (stdin/stdout/stderr) of a 
// child process can be associated with.

template <bool Behind>
// "Behind" is true if pipe locates behind child process, or false if ahead of child.
class _pipe {
    friend class process;

private:
    int end[2] = { -1, -1 };
    // end[0] is reading end of pipe and end[1] is writing end.

protected:
    int& far = end[!Behind];
        // far end from child process for pipe, or -1 for redirection
    int& near = end[Behind];
        // near end from child process for pipe, or fd that a child process' standard 
        // stream is redirected to

    _pipe(int fd);  // will create a pipe if fd < 0, or a redirection otherwise.

    // for use from parent process
    ~_pipe();  // close near for pipe, or do nothing for redirection.
        // Note that pipe itself gets deleted (discarding any remaining data in it), when 
        // both ends (and all other fds associated with the pipe) are closed.

    // for use from child process
    void dup2(int fd);  // link/redirect the given fd to near.
    void dup2();        // duplicate near and overwrite near.
    void close();       // close near and far (if >= 0).
};

template <bool Behind>
_pipe<Behind>::_pipe(int fd)
{
    if ( fd >= 0 )  // for redirection
        near = fd;

    else if ( ::pipe2(end, O_CLOEXEC) != 0 )  // create a pipe.
        // ::pipe() will not modify end[] on failure.
        // We basically create far and near with O_CLOEXEC set, not to be inherited by 
        // child processes unless duplicated for other non-O_CLOEXEC fds.
        throw std::system_error(errno, std::system_category());

    // Note we always have near >= 0 here, but far may or may not be >= 0, and
    // near and far are unique once far >= 0.
}

template <bool Behind>
_pipe<Behind>::~_pipe()
{
    if ( far >= 0 ) {
        ::close(near);
        near = -1;  // not necessarily needed
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
        // We overwrite near, which is assumed to have been STDIN/STDOUT/STDERR. 
        // Otherwise, the old near may not be closed on _pipe<>::close().
    if ( near == -1 )
        throw std::system_error(errno, std::system_category());
}

template <bool Behind>
void _pipe<Behind>::close()
{
    if ( far < 0 && near > STDERR_FILENO )
        // If far >= 0, we don't need to close near and far since they are both created 
        // with O_CLOEXEC set and thus will be closed automatically on exec*().
        ::close(near);
        // may cause -1 error if some nears are the same, which we can ignore safely.

/* Or, we could blindly close all:
        if ( near > STDERR_FILENO )
            ::close(near);
        if ( far > STDERR_FILENO )
            ::close(far);
*/
}
