#ifndef YUKINO_BASIC_IO_H_
#define YUKINO_BASIC_IO_H_

#include "yuki/slice.h"
#include "yuki/strings.h"
#include "yuki/status.h"
#include <stdio.h>
#include <stdarg.h>

namespace yukino {

class InputStream {
public:
    InputStream();
    InputStream(const InputStream &) = delete;
    InputStream(InputStream &&) = delete;
    void operator = (InputStream &) = delete;

    virtual ~InputStream();

    virtual bool ReadLine(std::string *line) = 0;

    virtual bool Read(size_t size, yuki::Slice *buf, std::string *stub) = 0;

    virtual yuki::Status status() const = 0;
}; // OutputStream

class OutputStream {
public:
    OutputStream();
    OutputStream(const OutputStream &) = delete;
    OutputStream(OutputStream &&) = delete;
    void operator = (OutputStream &) = delete;

    virtual ~OutputStream();

    virtual size_t Write(yuki::SliceRef buf) = 0;
    
    inline size_t Fprintf(const char *fmt, ...);

    virtual yuki::Status status() const = 0;
}; // OutputStream

inline size_t OutputStream::Fprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string buf(yuki::Strings::Vformat(fmt, ap));
    va_end(ap);

    return Write(yuki::Slice(buf));
}

InputStream *NewBufferedInputStream(yuki::SliceRef buf);
InputStream *NewFileInputStream(FILE *fp);
OutputStream *NewBufferedOutputStream(std::string *buf);
OutputStream *NewFileOutputStream(FILE *fp);

} // namespace yukino

#endif // YUKINO_BASIC_IO_H_