#ifndef YUKINO_HASH_DB_H_
#define YUKINO_HASH_DB_H_

#include "db.h"
#include "cocurrent_hash_map.h"
#include "yuki/file_path.h"
#include <atomic>
#include <string>
#include <thread>
#include <mutex>

namespace yukino {

class BackgroundWorkQueue;
class BinLogWriter;
struct DBConf;

class HashDB : public DB {
public:
    HashDB(const DBConf &conf,
           const std::string &data_dir,
           int id,
           int initialize_size,
           BackgroundWorkQueue *work_queue);
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
    yuki::Status DoOpen(size_t *be_read);
    yuki::Status DoCheckpoint(bool force);
    yuki::Status DoSave();
    yuki::Status SaveTable(int version);
    yuki::Status CreateLogFile(int version, int *fd);
    yuki::Status SaveVersion();

    CocurrentHashMap hash_map_;
    yuki::FilePath db_dir_;
    size_t memory_limit_;
    bool persistent_;
    int id_;
    BinLogWriter *log_ = nullptr;
    int log_fd_ = -1;
    int version_ = 0;
    std::atomic<bool> is_saving_;
    std::mutex mutex_;
    BackgroundWorkQueue *work_queue_;
    std::thread saving_thread_;
};

} // namespace yukino

#endif // YUKINO_HASH_DB_H_