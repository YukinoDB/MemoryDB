#ifndef YUKINO_CIRCULAR_BUFFER_H_
#define YUKINO_CIRCULAR_BUFFER_H_

#include "yuki/slice.h"
#include "glog/logging.h"
#include <string>
#include <string.h>

namespace yukino {

template<int N = 1024>
class StaticCircularBuffer {
public:
    StaticCircularBuffer() = default;
    StaticCircularBuffer(const StaticCircularBuffer &) = delete;
    StaticCircularBuffer(StaticCircularBuffer &&) = delete;

    bool CopiedReadIfNeed(size_t need, yuki::Slice *output,
                          std::string *copied) {
        DCHECK_GE(wr_size_, rd_size_);

        auto remain = read_remain();
        if (need == 0 || need > remain) {
            need = remain;
        }
        if (need == 0) {
            return false;
        }

        auto once_remain = read_once_remain();
        if (need <= once_remain) {
            *output = yuki::Slice(&buf_[rd_pos_], need);
        } else {
            copied->assign(&buf_[rd_pos_], once_remain);
            copied->append(&buf_[0], need - once_remain);
            *output = yuki::Slice(*copied);
        }
        rd_size_ += need;
        rd_pos_   = rd_size_ % N;
        return true;
    }

    void Rewind(size_t n) {
        rd_size_ -= n;
        rd_pos_   = rd_size_ % N;
    }

    bool CopiedWrite(yuki::SliceRef input, size_t *written) {
        DCHECK_GE(wr_size_, rd_size_);

        if (input.Length() > N) {
            return false;
        }

        auto remain = write_remain();
        auto need = input.Length() > remain ? remain : input.Length();
        if (need == 0) {
            return false;
        }

        auto once_remain = write_once_remain();
        if (need <= once_remain) {
            memcpy(&buf_[wr_pos_], input.Bytes(), need);
        } else {
            memcpy(&buf_[wr_pos_], input.Bytes(), once_remain);
            memcpy(&buf_[0], input.Bytes() + once_remain, need - once_remain);
        }

        if (written) {
            *written = need;
        }
        wr_size_ += need;
        wr_pos_   = wr_size_ % N;
        return true;
    }

    void *OnceWriteBuffer(size_t need, size_t *size) {
        auto once_remain = write_once_remain();
        if (need <= once_remain) {
            *size = need;
        } else {
            *size = once_remain;
        }
        return &buf_[wr_pos_];
    }

    void Advance(size_t n) {
        wr_size_ += n;
        wr_pos_   = wr_size_ % N;
    }

    // [             ]
    //   r         w
    // [             ]
    //   w         r
    // [             ]
    //   w
    //   r
    size_t write_remain() const {
        if (rd_pos_ < wr_pos_) {
            return (N - wr_pos_) + rd_pos_;
        } else if (rd_pos_ > wr_pos_) {
            return rd_pos_ - wr_pos_;
        } else {
            return (rd_size_ == wr_size_) ? N : 0;
        }
    }

    size_t write_once_remain() const {
        if (rd_pos_ <= wr_pos_) {
            return N - wr_pos_;
        } else {
            return rd_pos_ - wr_pos_;
        }
    }
    
    size_t read_remain() const { return wr_size_ - rd_size_; }
    
    size_t read_once_remain() const {
        if (rd_pos_ < wr_pos_) {
            return wr_pos_ - rd_pos_;
        } else {
            return N - rd_pos_;
        }
    }
    
private:
    char buf_[N];
    size_t rd_pos_ = 0;
    size_t wr_pos_ = 0;
    size_t rd_size_ = 0;
    size_t wr_size_ = 0;
}; // class StaticCircularBuffer

} // namespace yukino

#endif // YUKINO_CIRCULAR_BUFFER_H_