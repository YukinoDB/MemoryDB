#include "configuration.h"
#include "yuki/strings.h"

namespace yukino {

class Configuration::InputStream {
public:
    virtual bool ReadLine(std::string *line) = 0;

    virtual yuki::Status status() const = 0;
};

class BufferedInputStream : public Configuration::InputStream {
public:
    BufferedInputStream(yuki::SliceRef input)
        : buf_(input.Data())
        , end_(input.Data() + input.Length()) {}

    virtual bool ReadLine(std::string *line) override {
        auto begin = buf_;
        const char *newline = nullptr;
        for (;;) {
            newline = strchr(buf_, '\n');
            if (!newline) {
                break;
            }
            if (*begin == '#') {
                begin = newline + 1;
            } else {
                break;
            }
        }
        if (!newline) {
            line->assign(begin, end_ - begin);
            buf_ = end_;
        } else {
            line->assign(begin, newline - begin);
            buf_ = newline + 1;
        }
        
        return buf_ < end_;
    }
    
    virtual yuki::Status status() const override {
        return yuki::Status::OK();
    }
private:
    const char *buf_;
    const char *end_;
};

template<class T>
struct ValueTraits {
    static bool Parse(yuki::SliceRef buf, T *) {
        return false;
    }

    static std::string ToString(const T &) {
        return "";
    }
};

template<>
struct ValueTraits<int> {
    static bool Parse(yuki::SliceRef buf, int *value) {
        // longest int number: -2147483648
        if (buf.Empty() || buf.Length() > sizeof("-2147483648") - 1) {
            return false;
        }
        int sign = 0;
        if (buf.Data()[0] == '-') {
            if (buf.Length() == 1) {
                return false;
            }
            sign = 1;
        }

        int n = 0;
        int pow = 1;
        for (long i = buf.Length() - 1; i >= sign; i--) {
            auto c = buf.Data()[i];

            if (c < '0' || c > '9') {
                return false;
            } else {
                n += ((c - '0') * pow);
                pow *= 10;
            }
        }
        *value = sign ? -n : n;
        return true;
    }

    static std::string ToString(const int &value) {
        return yuki::Strings::Format("%d", value);
    }
};

template<>
struct ValueTraits<long> {
    static bool Parse(yuki::SliceRef buf, long *value) {
        // longest int number: -2147483648
        if (buf.Empty() || buf.Length() > sizeof("-9223372036854775808") - 1) {
            return false;
        }
        int sign = 0;
        if (buf.Data()[0] == '-') {
            if (buf.Length() == 1) {
                return false;
            }
            sign = 1;
        }

        long n = 0;
        long pow = 1;
        for (long i = buf.Length() - 1; i >= sign; i--) {
            auto c = buf.Data()[i];

            if (c < '0' || c > '9') {
                return false;
            } else {
                n += ((c - '0') * pow);
                pow *= 10;
            }
        }
        *value = sign ? -n : n;
        return true;
    }
    
    static std::string ToString(const int &value) {
        return yuki::Strings::Format("%d", value);
    }
};

template<>
struct ValueTraits<std::string> {
    static bool Parse(yuki::SliceRef buf, std::string *value) {
        if (buf.Empty()) {
            return false;
        }
        value->assign(buf.Data(), buf.Length());
        return true;
    }

    static std::string ToString(const std::string &value) {
        return value;
    }
};


#define DEF_INIT(name, type, default_value) name##_(default_value),
Configuration::Configuration()
    : DECL_CONF_ITEMS(DEF_INIT)
    db_conf_() {
}
#undef DEF_INIT


void Configuration::Reset() {
#define DEF_RESET(name, type, default_value) name##_ = default_value;
    DECL_CONF_ITEMS(DEF_RESET)
#undef DEF_RESET
    db_conf_.clear();
}

yuki::Status Configuration::LoadBuffer(yuki::SliceRef buf) {
    std::unique_ptr<InputStream> input(new BufferedInputStream(buf));
    if (!input.get()) {
        return yuki::Status::Errorf(yuki::Status::kCorruption,
                                    "not enough memory");
    }
    auto rv = LoadStream(input.get());
    if (rv.Failed()) {
        return rv;
    }
    return input->status();
}

yuki::Status Configuration::LoadFile(FILE *fp) {

    return yuki::Status::OK();
}

yuki::Status
Configuration::ProcessConfItem(const std::vector<yuki::Slice> &args) {
    using yuki::Status;
    using yuki::Slice;

    if (args[0].Compare(Slice("db", 2)) == 0) {
        if (args.size() < 2) {
            return Status::Errorf(Status::kInvalidArgument,
                                  "[db] bad arguments number(>1)");
        }

        DBConf dbconf;

        // db hash persistent // db 0
        // db order persistent // db 0
        if (args[1].Compare(Slice("hash", 4)) == 0 ||
            args[1].Compare(Slice("order", 5)) == 0) {

            if (args[1].Compare(Slice("hash", 4)) == 0) {
                dbconf.type = DB_HASH;
            } else {
                dbconf.type = DB_ORDER;
            }
            dbconf.persistent = false;

            if (args.size() >= 3) {
                if (args[2].Compare(Slice("persistent", 10)) == 0) {
                    dbconf.persistent = true;
                } else if (args[2].Compare(Slice("memory", 6)) == 0) {
                    dbconf.persistent = false;
                } else {
                    return Status::Errorf(Status::kInvalidArgument,
                                          "actual %s, expected persistent/memory",
                                          args[2].ToString().c_str());
                }
            }

            dbconf.memory_limit = 0;
            if (args.size() >= 4) {
                //dbconf.memory_limit
                if (!ValueTraits<long>::Parse(args[3], &dbconf.memory_limit)) {
                    return Status::Errorf(Status::kCorruption,
                                          "[db] bad argument type");
                }
            }
        } else {
            return Status::Errorf(Status::kInvalidArgument,
                                  "db[%d] type %s not support",
                                  static_cast<int>(db_conf_.size()),
                                  args[1].ToString().c_str());
        }

        db_conf_.push_back(dbconf);
    }
#define DEF_PARSE(name, type, default_value) \
    else if (args[0].Compare(Slice(#name)) == 0) { \
        if (args.size() < 2) { \
            return Status::Errorf(Status::kInvalidArgument, \
                                  "[%s] bad arguments number(1)", #name); \
        } \
        if (!ValueTraits<type>::Parse(args[1], &name##_)) { \
            return Status::Errorf(Status::kCorruption, \
                                  "[%s] bad argument type", #name); \
        } \
    }
    DECL_CONF_ITEMS(DEF_PARSE)
#undef DEF_PARSE
    else {
        return Status::Errorf(Status::kInvalidArgument,
                              "%s command not support",
                              args[0].ToString().c_str());
    }

    return yuki::Status::OK();
}

yuki::Status Configuration::LoadStream(InputStream *input) {
    std::string buf;
    std::vector<yuki::Slice> parts;

    db_conf_.clear();
    while (input->ReadLine(&buf)) {

        parts.clear();
        auto rv = yuki::Strings::Split(buf.c_str(), "\\s+", &parts);
        if (rv.Failed()) {
            return rv;
        }

        rv = ProcessConfItem(parts);
        if (rv.Failed()) {
            return rv;
        }
    }
    return input->status();
}

} // namespace yukino