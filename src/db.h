#ifndef YUKINO_DB_H_
#define YUKINO_DB_H_

#include "handle.h"
#include "yuki/slice.h"
#include "yuki/status.h"
#include <stdint.h>

namespace yukino {

struct Obj;
struct Version;
struct DBConf;
class Iterator;

class DB {
public:
    DB();
    DB(const DB &) = delete;
    DB(DB &&) = delete;
    void operator = (const DB &) = delete;
    virtual ~DB();

    virtual yuki::Status Open() = 0;

    // Write-Ahead-Log
    virtual yuki::Status AppendLog(int code,
                                   const std::vector<Handle<Obj>> &args) = 0;

    virtual Iterator *iterator() = 0;

    virtual int num_keys() const = 0;

    virtual yuki::Status Put(yuki::SliceRef key, uint64_t version_number,
                             Obj *value) = 0;

    virtual bool Delete(yuki::SliceRef key) = 0;

    virtual yuki::Status Get(yuki::SliceRef key, Version *ver, Obj **value) = 0;

    static DB *New(const DBConf &conf, const std::string &data_dir, int id);
}; // class DB

} // namespace yukino

#endif // YUKINO_DB_H_
