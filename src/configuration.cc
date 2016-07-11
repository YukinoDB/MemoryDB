#include "configuration.h"
#include "basic_io.h"
#include "value_traits.h"
#include "yuki/strings.h"
#include <stdio.h>
#include <stdarg.h>

namespace yukino {

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
    std::unique_ptr<InputStream> input(NewBufferedInputStream(buf));
    if (!input.get()) {
        return yuki::Status::Errorf(yuki::Status::kCorruption,
                                    "not enough memory");
    }
    return LoadStream(input.get());
}

yuki::Status Configuration::LoadFile(FILE *fp) {
    std::unique_ptr<InputStream> input(NewFileInputStream(fp));
    if (!input.get()) {
        return yuki::Status::Errorf(yuki::Status::kCorruption,
                                    "not enough memory");
    }
    return LoadStream(input.get());
}

yuki::Status Configuration::RewriteBuffer(std::string *buf) const {
    std::unique_ptr<OutputStream> output(NewBufferedOutputStream(buf));
    if (!output.get()) {
        return yuki::Status::Errorf(yuki::Status::kCorruption,
                                    "not enough memory");
    }
    return RewriteStream(output.get());
}

yuki::Status Configuration::RewriteFile(FILE *fp) const {
    std::unique_ptr<OutputStream> output(NewFileOutputStream(fp));
    if (!output.get()) {
        return yuki::Status::Errorf(yuki::Status::kCorruption,
                                    "not enough memory");
    }
    return RewriteStream(output.get());
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
        if (buf.empty() || buf[0] == '#') {
            continue;
        }

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
    return yuki::Status::OK();
}

yuki::Status Configuration::RewriteStream(OutputStream *output) const {
    output->Fprintf("# Generated by rewriting\n");

#define DEF_REWRITE(name, type, default_value) \
    output->Write(yuki::Slice(#name, sizeof(#name) - 1)); \
    output->Write(yuki::Slice(" ", 1)); \
    output->Write(yuki::Slice(ValueTraits<type>::ToString(name##_))); \
    output->Write(yuki::Slice("\n", 1));
    DECL_CONF_ITEMS(DEF_REWRITE)
#undef DEF_REWRITE

    output->Fprintf("## DBs conf : ##\n");
    for (const auto &dbconf : db_conf_) {
        switch (dbconf.type) {
            case DB_HASH:
            case DB_ORDER:
                output->Fprintf("db %s %s %l\n",
                                dbconf.type == DB_HASH ? "hash" : "order",
                                dbconf.persistent ? "persistent" : "memory",
                                dbconf.memory_limit);
                break;

            case DB_PAGE:
                // TODO:
                break;

            default:
                break;
        }
    }
    return output->status();
}

} // namespace yukino