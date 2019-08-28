// Basics of pipe(), fork(), dup2(), and waitpid()
// from http://www.rozmichelle.com/pipes-forks-dups/

// File descriptor is:
// - a unique id (handle) associated with an open file or either end of an open pipe.
//   = available when open().
//   = disassociated with the file/pipe when close().
//   = reassociated (i.e, duplicated) with existing file/pipe via dup2().
// - local to each process.
//   = child process inherits all of parent's file descriptors when starting.
// - for either reading or writing (but not both*).
// - not buffered without wrapping in FILE*.
// - fileno(): FILE* -> fd, fdopen(): fd -> FILE*
// - for IPC.

// Pipe is:
// - unidirectional.
// - with both ends, one for read and the other for write.
// - created using pipe().
// - for a binary (not text) stream flowing.
// - for interprocess communication channel.
//   = able to block/resume system calls, read(2) and write(3).
//   = If all file descriptors referring to the write end of a pipe have been closed, 
//     then an attempt to read(2) from the pipe will see end-of-file (read(2) will return 
//     0).
//   = If all file descriptors referring to the read end of a pipe have been closed, then 
//     a write(2) will cause a SIGPIPE signal to be generated for the calling process. If 
//     the calling process is ignoring this signal, then write(2) fails with the error 
//     EPIPE.
//   = It is not possible to apply lseek(2) to a pipe.
// - possible to have multiple writers and/or multiple readers.
// - having a capacity determined by OS.



#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    int fds[2];		 // an array that will hold two file descriptors
    pipe(fds);		 // populates fds with two file descriptors

    pid_t pid = fork();  // create child process that is a clone of the parent
    if ( pid == 0 ) {  // if pid == 0, then this is the child process
	dup2(fds[0], STDIN_FILENO);
	    // closes its stdin and associates its stdin with fds[0] (the read end of 
	    // pipe)
	close(fds[0]);
	    // file descriptor no longer needed in child since its stdin gets a copy
	close(fds[1]);
	    // file descriptor unused in child
	char *argv[] = { "sort", NULL };  // create argument vector
	execvp(argv[0], argv);		  // run sort command
	// The process inherits the parent's file descriptor table!
    }

    else {  // in parent process
	close(fds[0]);	// file descriptor unused in parent
	const char *words[] = { "pear", "peach", "apple" };
	size_t numwords = sizeof(words)/sizeof(words[0]);

	// write input to the writable file descriptor so it can be read in from child:
	for (size_t i = 0; i < numwords; i++) {
	  dprintf(fds[1], "%s\n", words[i]); 
	}

	// send EOF so child can continue (child blocks until all input has been 
	// processed):
	close(fds[1]); 
    }

    // wait for child to finish before exiting
    int status;
    pid_t wpid = waitpid(pid, &status, 0);

    return wpid == pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
