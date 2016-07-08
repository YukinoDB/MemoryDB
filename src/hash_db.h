#ifndef YUKINO_HASH_DB_H_
#define YUKINO_HASH_DB_H_

#include "db.h"
#include "cocurrent_hash_map.h"
#include <string>

namespace yukino {

struct DBConf;

class HashDB : public DB {
public:
    HashDB(const DBConf &conf, const std::string &data_dir, int id,
           int initialize_size);
    virtual ~HashDB() override;

    virtual yuki::Status Open() override;
    virtual yuki::Status
    AppendLog(int code, const std::vector<Handle<Obj>> &args) override;
    virtual Iterator *iterator() override;
    virtual int num_keys() const override;
    virtual yuki::Status Put(yuki::SliceRef key, uint64_t version_number,
                             Obj *value) override;
    virtual bool Delete(yuki::SliceRef key) override;
    virtual yuki::Status Get(yuki::SliceRef key, Version *ver,
                             Obj **value) override;
private:
    CocurrentHashMap hash_map_;
    const std::string data_dir_;
    size_t memory_limit_;
    bool persistent_;
    int id_;
};

} // namespace yukino

#endif // YUKINO_HASH_DB_H_