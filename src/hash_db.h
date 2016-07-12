#ifndef YUKINO_HASH_DB_H_
#define YUKINO_HASH_DB_H_

#include "db.h"
#include "cocurrent_hash_map.h"
#include <string>
#include <mutex>

namespace yukino {

class BinLogWriter;
struct DBConf;

class HashDB : public DB {
public:
    HashDB(const DBConf &conf, const std::string &data_dir, int id,
           int initialize_size);
    virtual ~HashDB() override;

    virtual yuki::Status Open() override;
    virtual yuki::Status Checkpoint(bool force) override;
    virtual yuki::Status
    AppendLog(int code, int64_t version,
              const std::vector<Handle<Obj>> &args) override;
    virtual Iterator *iterator() override;
    virtual int num_keys() const override;
    virtual yuki::Status Put(yuki::SliceRef key, uint64_t version_number,
                             Obj *value) override;
    virtual bool Delete(yuki::SliceRef key) override;
    virtual yuki::Status Get(yuki::SliceRef key, Version *ver,
                             Obj **value) override;
private:
    yuki::Status DoOpen();
    yuki::Status DoCheckpoint(bool force);

    CocurrentHashMap hash_map_;
    const std::string data_dir_;
    size_t memory_limit_;
    bool persistent_;
    int id_;
    BinLogWriter *log_ = nullptr;
    int log_fd_ = -1;
    int version_ = 0;
    bool is_dumpping_ = false;
    std::mutex mutex_;
};

} // namespace yukino

#endif // YUKINO_HASH_DB_H_