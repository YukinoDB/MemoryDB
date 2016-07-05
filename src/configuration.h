#ifndef YUKINO_CONFIGURATION_H_
#define YUKINO_CONFIGURATION_H_

#include "yuki/status.h"
#include "yuki/slice.h"
#include <vector>
#include <string>

namespace yukino {

#define DECL_CONF_ITEMS(_)                    \
    _(address,      std::string, "127.0.0.1") \
    _(port,         int,         7000       ) \
    _(data_dir,     std::string, "."        ) \
    _(daemonize,    bool,        false      ) \
    _(pid_file,     std::string, ""         ) \
    _(num_workers,  int,         4          ) \
    _(auth,         bool,        false      ) \
    _(pass_digest,  std::string, ""         )

class InputStream;
class OutputStream;

enum DBType {
    DB_HASH,  // hash map db
    DB_ORDER, // tree map db
    DB_PAGE,  // btree based page map db
};

class Configuration {
public:
    struct DBConf {
        DBType type;
        bool   persistent;
        long   memory_limit;
    };

    Configuration();
    Configuration(const Configuration &) = delete;
    Configuration(Configuration &&) = delete;
    void operator = (const Configuration &) = delete;

    void Reset();

    yuki::Status LoadBuffer(yuki::SliceRef buf);
    yuki::Status LoadFile(FILE *fp);

    yuki::Status RewriteBuffer(std::string *buf) const;
    yuki::Status RewriteFile(FILE *fp) const;

    yuki::Status ProcessConfItem(const std::vector<yuki::Slice> &args);

#define DEF_ACCESS(name, type, defautl_value) \
    const type &name () const { return name##_; } \
    void set_##name (const type &value) { name##_ = value; }
    DECL_CONF_ITEMS(DEF_ACCESS)
#undef DEF_ACCESS

    const DBConf &db_conf(size_t index) const { return db_conf_.at(index); }
    size_t num_db_conf() const { return db_conf_.size(); }
    void set_db_conf(size_t i, const DBConf &conf) { db_conf_[i] = conf; }

private:
    yuki::Status LoadStream(InputStream *input);
    yuki::Status RewriteStream(OutputStream *output) const;

#define DEF_MEMBER(name, type, defautl_value) type name##_;
    DECL_CONF_ITEMS(DEF_MEMBER)
#undef  DEF_MEMBER
    std::vector<DBConf> db_conf_;
};

// db hash persistent // db 0
// db order           // db 1

} // namespace yukino

#endif // YUKINO_CONFIGURATION_H_