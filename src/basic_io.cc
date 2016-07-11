#include "basic_io.h"

namespace yukino {

InputStream::InputStream() {
}

InputStream::~InputStream() {
}

OutputStream::OutputStream() {
}

OutputStream::~OutputStream() {
}

class BufferedInputStream : public InputStream {
public:
    BufferedInputStream(yuki::SliceRef input)
        : buf_(input.Data())
        , end_(input.Data() + input.Length()) {}

    virtual ~BufferedInputStream() override {}

    virtual bool ReadLine(std::string *line) override {
        if (buf_ >= end_) {
            return false;
        }

        auto begin = buf_;
        const char *newline = strchr(buf_, '\n');
        line->clear();
        if (!newline) {
            line->assign(begin, end_ - begin);
            buf_ = end_;
        } else {
            line->assign(begin, newline - begin);
            buf_ = newline + 1;
        }

        return true;
    }

    virtual bool Read(size_t need, yuki::Slice *buf, std::string */*stub*/)
        override {
        if (buf_ >= end_) {
            return false;
        }
        need = std::min(static_cast<size_t>(end_ - buf_), need);
        *buf = yuki::Slice(buf_, need);
        buf_ += need;

        return true;
    }

    virtual yuki::Status status() const override {
        return yuki::Status::OK();
    }
private:
    const char *buf_;
    const char *end_;
}; // BufferedInputStream

class FileInputStream : public InputStream {
public:
    FileInputStream(FILE *fp)
        : fp_(DCHECK_NOTNULL(fp)) {
    }

    virtual ~FileInputStream() override {}

    virtual bool ReadLine(std::string *line) override {
        line->clear();

        int ch = getc(fp_);
        while (ch != EOF && ch != '\n') {
            line->append(1, ch);
            ch = getc(fp_);
        }
        return ch != EOF;
    }

    virtual bool Read(size_t need, yuki::Slice *buf,
                      std::string *stub) override {
        if (feof(fp_) || ferror(fp_)) {
            return false;
        }

        stub->resize(need);
        auto rv = fread(&((*stub)[0]), need, 1, fp_);
        *buf = yuki::Slice(&((*stub)[0]), rv);
        return true;
    }

    virtual yuki::Status status() const override {
        using yuki::Status;
        if (ferror(fp_) && !feof(fp_)) {
            return Status::Systemf("io error");
        } else {
            return Status::OK();
        }
    }

private:
    FILE *fp_ = nullptr;
}; // FileInputStream

class BufferedOutputStream : public OutputStream {
public:
    BufferedOutputStream(std::string *buf) : buf_(buf) {}

    virtual ~BufferedOutputStream() {}

    virtual size_t Write(const void *buf, size_t size) override {
        buf_->append(static_cast<const char *>(buf), size);
        return size;
    }

    virtual yuki::Status status() const override {
        return yuki::Status::OK();
    }

private:
    std::string *buf_;
}; // BufferedOutputStream

class FileOutputStream : public OutputStream {
public:
    FileOutputStream(FILE *fp) : fp_(DCHECK_NOTNULL(fp)) {}

    virtual ~FileOutputStream() {}

    virtual size_t Write(const void *buf, size_t size) override {
        return fwrite(buf, 1, size, fp_);
    }

    virtual yuki::Status status() const override {
        using yuki::Status;
        return ferror(fp_) ? Status::Systemf("io error") : Status::OK();
    }
    
private:
    FILE *fp_;
}; // FileOutputStream

class PosixFileOutputStream : public OutputStream {
public:
    PosixFileOutputStream(int fd) : fd_(fd) {}

    virtual ~PosixFileOutputStream() {
    }

    virtual size_t Write(const void *buf, size_t size) override {
        ssize_t rv = write(fd_, buf, size);
        if (rv < 0) {
            status_ = yuki::Status::Systemf("write fail");
            return 0;
        } else {
            if (status_.Failed()) {
                status_ = yuki::Status::OK();
            }
            return static_cast<size_t>(rv);
        }

    }

    virtual yuki::Status status() const override { return status_; }

private:
    int fd_;
    yuki::Status status_;
}; // class PosixFileOutputStream

InputStream *NewBufferedInputStream(yuki::SliceRef buf) {
    return new BufferedInputStream(buf);
}

InputStream *NewFileInputStream(FILE *fp) {
    return new FileInputStream(fp);
}

OutputStream *NewBufferedOutputStream(std::string *buf) {
    return new BufferedOutputStream(buf);
}

OutputStream *NewFileOutputStream(FILE *fp) {
    return new FileOutputStream(fp);
}

OutputStream *NewPosixFileOutputStream(int fd) {
    DCHECK_GE(fd, 0);
    return new PosixFileOutputStream(fd);
}

} // namespace yukino