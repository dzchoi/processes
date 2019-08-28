// The fdstreams are just like normal fstreams, but:
// - are sibling classes (not child classes) of ifstream/ofstream.
// - do not support seek and tell operations.
// - have an additional constructor that takes an already open file descriptor.
// - provide fd() member function which returns underlying fd associated with open file.
//
// Note: When closing ofdstream that is associated with the input stream of other process 
// over pipe, the input stream will see end-of-file on read(2); and when closing 
// ifdstream, the output stream associated with the ifdstream over pipe will get SIGPIPE 
// signal on write(2).

// Reference:
// - fstream
// - mozjs-60/mozilla/FStream.h



#pragma once

//#include <string>  // char_traits<>
#include <fstream>  // basic_filebuf<>, basic_fstream<>, .rdbuf(), .is_open(), .close()
// <cstdio>: BUFSIZ
#include <string>  // string
// <memory>: unique_ptr<>, .get(), .reset()

#if __GLIBCXX__ >= 20040906  // >= GCC 3.4.2
# include <ext/stdio_filebuf.h>  // stdio_filebuf<>
#else
# error "gcc 3.4.2+ required."
#endif



template <typename CharT =char, typename Traits =std::char_traits<CharT>>
class ofdstream: public std::basic_ostream<CharT, Traits> {
private:
    std::unique_ptr<std::basic_filebuf<CharT, Traits>> filebuf;

public:
    ofdstream() {}  // to open later

    ofdstream(int fd, size_t bufsize =static_cast<size_t>(BUFSIZ))
    : filebuf { fd < 0 ? nullptr :
	new __gnu_cxx::stdio_filebuf<CharT, Traits>(fd, std::ios_base::out, bufsize) }
    // Note: __gnu_cxx::stdio_filebuf<> -> std::basic_filebuf<> -> std::basic_streambuf<>
    { this->init(filebuf.get()); }
	// this->init(p) is the same as std::basic_ostream<CharT, Traits>::rdbuf(p).

    // bufsize of 0 (or setting std::unitbuf (https://stackoverflow.com/a/26976747)) is 
    // preferred for pipes, because pipe itself has a buffer (and so does the stdin of 
    // the counterpart over the pipe) and ofdstream cannot be line-buffered. (Only 
    // std::cout and std::cerr can be line-buffered 
    // (https://stackoverflow.com/a/42431124).)

    ofdstream(const std::string& filename,
	std::ios_base::openmode mode =std::ios_base::out)
    { open(filename, mode); }

    ~ofdstream() {}  // will close the underlying fd as well

    // Todo: move constructor/operator!

    void open(const std::string& filename,
	std::ios_base::openmode mode =std::ios_base::out);

    bool is_open() const { return filebuf && filebuf->is_open(); }

    void close() {  // close the underlying fd like ofstream does
	// No need to run this->setstate(std::ios_base::eofbit) since closing does not 
	// mean EOF reached.
	if ( filebuf && !filebuf->close() )
	    this->setstate(std::ios_base::failbit);
    }

    std::basic_filebuf<CharT, Traits>* rdbuf() const { return filebuf.get(); }

    int fd() {
	struct _filebuf: std::basic_filebuf<CharT, Traits> {
	    int fd() { return this->_M_file.fd(); } };
	return filebuf ? static_cast<_filebuf*>(filebuf.get())->fd() : -1;
    }
};

template <typename CharT, typename Traits>
void ofdstream<CharT, Traits>::open(const std::string& filename,
    std::ios_base::openmode mode)
{
    filebuf.reset(new std::basic_filebuf<CharT, Traits>());
	// The size of the underlying file buffer is BUFSIZ fixed.
    if ( filebuf->open(filename, mode | std::ios_base::out) )
	this->clear();
    else {
	filebuf.reset();
	this->setstate(std::ios_base::failbit);
    }
    this->init(filebuf.get());
}



template <typename CharT =char, typename Traits =std::char_traits<CharT>>
class ifdstream: public std::basic_istream<CharT, Traits> {
private:
    std::unique_ptr<std::basic_filebuf<CharT, Traits>> filebuf;

public:
    ifdstream() {}

    ifdstream(int fd, size_t bufsize =static_cast<size_t>(BUFSIZ))
    : filebuf { fd < 0 ? nullptr :
	new __gnu_cxx::stdio_filebuf<CharT, Traits>(fd, std::ios_base::in, bufsize) }
    { this->init(filebuf.get()); }

    // bufsize of 0 is preferred for pipes, assuming that accessing pipe is efficient 
    // enough that we don't need another layer of buffering.

    ifdstream(const std::string& filename,
	std::ios_base::openmode mode =std::ios_base::in)
    { open(filename, mode); }

    ~ifdstream() {}

    void open(const std::string& filename,
	std::ios_base::openmode mode =std::ios_base::in);

    bool is_open() const { return filebuf && filebuf->is_open(); }

    void close() {
	if ( filebuf && !filebuf->close() )
	    this->setstate(std::ios_base::failbit);
    }

    std::basic_filebuf<CharT, Traits>* rdbuf() const { return filebuf.get(); }

    int fd() {
	struct _filebuf: std::basic_filebuf<CharT, Traits> {
	    int fd() { return this->_M_file.fd(); } };
	return filebuf ? static_cast<_filebuf*>(filebuf.get())->fd() : -1;
    }
};

template <typename CharT, typename Traits>
void ifdstream<CharT, Traits>::open(const std::string& filename,
    std::ios_base::openmode mode)
{
    filebuf.reset(new std::basic_filebuf<CharT, Traits>());
    if ( filebuf->open(filename, mode | std::ios_base::in) )
	this->clear();
    else {
	filebuf.reset();
	this->setstate(std::ios_base::failbit);
    }
    this->init(filebuf.get());
}



// Return fd that is associated with an open fstream/ofstream/ifstream.

template<typename CharT, typename Traits>
inline int fd(std::basic_fstream<CharT, Traits>& file)
{
    struct _filebuf : std::basic_filebuf<CharT, Traits> {
        int fd() { return this->_M_file.fd(); } };
    return static_cast<_filebuf*>(file.rdbuf())->fd();
}

template<typename CharT, typename Traits>
inline int fd(std::basic_ofstream<CharT, Traits>& file)
{
    struct _filebuf : std::basic_filebuf<CharT, Traits> {
        int fd() { return this->_M_file.fd(); } };
    return static_cast<_filebuf*>(file.rdbuf())->fd();
}

template<typename CharT, typename Traits>
inline int fd(std::basic_ifstream<CharT, Traits>& file)
{
    struct _filebuf : std::basic_filebuf<CharT, Traits> {
        int fd() { return this->_M_file.fd(); } };
    return static_cast<_filebuf*>(file.rdbuf())->fd();
}
