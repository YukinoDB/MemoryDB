#include "hash_db.h"
#include "configuration.h"

namespace yukino {

HashDB::HashDB(const DBConf &conf, const std::string &data_dir, int id,
               int initialize_size)
    : hash_map_(initialize_size)
    , data_dir_(data_dir)
    , id_(id)
    , memory_limit_(conf.memory_limit)
    , persistent_(conf.persistent) {
}

HashDB::~HashDB() {
}

yuki::Status HashDB::Open() {
    using yuki::Status;

    return Status::OK();
}

Iterator *HashDB::iterator() {
    return hash_map_.iterator();
}

int HashDB::num_keys() const {
    return hash_map_.num_keys();
}

yuki::Status HashDB::Put(yuki::SliceRef key, uint64_t version_number,
                         Obj *value) {
    using yuki::Status;

    return hash_map_.Put(key, version_number, value);
}

bool HashDB::Delete(yuki::SliceRef key) {
    return hash_map_.Delete(key);
}

yuki::Status HashDB::Get(yuki::SliceRef key, Version *ver,
                 Obj **value) {
    using yuki::Status;

    return hash_map_.Get(key, ver, value);
}

} // namespace yukino