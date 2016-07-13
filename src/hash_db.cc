#include "hash_db.h"
#include "bin_log.h"
#include "configuration.h"
#include "basic_io.h"
#include "value_traits.h"
#include "persistent.h"
#include "background.h"
#include "server.h"
#include "yuki/file_path.h"
#include "yuki/file.h"
#include "yuki/strings.h"
#include <fcntl.h>
#include <sys/stat.h>

namespace yukino {

static const size_t kLogSizeForCheckpoint = 50UL * 1024UL * 1024UL;

HashDB::HashDB(const DBConf &conf,
               const std::string &data_dir,
               int id,
               int initialize_size,
               BackgroundWorkQueue *work_queue)
    : hash_map_(initialize_size)
    , db_dir_(data_dir)
    , id_(id)
    , memory_limit_(conf.memory_limit)
    , persistent_(conf.persistent)
    , is_saving_(false)
    , work_queue_(DCHECK_NOTNULL(work_queue)) {

    // db dir: data_dir/db-<id>/
    db_dir_.Append(yuki::Strings::Format("db-%d", id_));
}

HashDB::~HashDB() {
    if (is_saving_.load()) {
        LOG(INFO) << "saving thread still running.";
    }
    if (saving_thread_.joinable()) {
        saving_thread_.join();
    }

    delete log_;

    if (log_fd_ >= 0) {
        close(log_fd_);
    }
}

yuki::Status HashDB::Open() {
    using yuki::Status;
    using yuki::FilePath;
    using yuki::Strings;

    if (!persistent_) { // in memory, do not need any files.
        return Status::OK();
    }

    bool exist;
    auto rv = db_dir_.Exist(&exist);
    if (rv.Failed()) {
        return rv;
    }

    bool is_new = false;
    if (!exist) {
        rv = yuki::File::MakeDir(db_dir_, true);
        if (rv.Failed()) {
            return rv;
        }
        LOG(INFO) << "new db: db-" << id_ << " created. " << db_dir_.Get();
        is_new = true;
    }

    // db all clean
    size_t origin_size = 0;
    if (is_new) {
        FilePath manifest_path(db_dir_);
        manifest_path.Append("MANIFEST");

        rv = Strings::ToFile(manifest_path, "0");
        if (rv.Failed()) {
            PLOG(ERROR) << "write to " << manifest_path.Get() << " fail";
            return rv;
        }

        version_ = 0;
    } else {
        rv = DoOpen(&origin_size);
        if (rv.Failed()) {
            return rv;
        }
    }

    FilePath log_path(db_dir_);
    log_path.Append(Strings::Format("log-%d", version_));
    if (is_new) {
        log_fd_ = open(log_path.Get().c_str(), O_CREAT|O_EXCL|O_WRONLY|O_APPEND,
                       0664);
    } else {
        log_fd_ = open(log_path.Get().c_str(), O_CREAT|O_WRONLY|O_APPEND, 0664);
    }
    if (log_fd_ < 0) {
        PLOG(ERROR) << "open " << log_path.Get() << " fail";
        return Status::Systemf("open %s fail", log_path.Get().c_str());
    }
    log_ = new BinLogWriter(NewPosixFileOutputStream(log_fd_), true, 512,
                            origin_size);
    return Status::OK();
}

yuki::Status HashDB::Checkpoint(bool force) {
    if (!persistent_) {
        return yuki::Status::Corruptionf("db do not need persistent");
    }

    std::unique_lock<std::mutex> lock(mutex_);
    return DoCheckpoint(force);
}

yuki::Status
HashDB::AppendLog(int code, int64_t version,
                  const std::vector<Handle<Obj>> &args) {
    using yuki::Status;

    if (!persistent_) {
        return Status::OK();
    }

    std::unique_lock<std::mutex> lock(mutex_);
    auto rv = log_->Append(static_cast<CmdCode>(code), version, args);
    if (rv.Failed()) {
        LOG(ERROR) << "write log error: " << rv.ToString();
        return rv;
    }
    work_queue_->PostSyncFile(log_fd_);

    if (is_saving_.load() || log_->written_bytes() < kLogSizeForCheckpoint) {
        return Status::OK();
    }

    if (saving_thread_.joinable()) {
        saving_thread_.join();
    }
    saving_thread_ = std::move(std::thread([&]() {
        is_saving_.store(true);

        auto jiffies = Server::current_milsces();
        auto status = DoSave();
        if (status.Failed()) {
            LOG(ERROR) << "save table fail. " << status.ToString();
        }
        LOG(INFO) << "save done, cost: " << Server::current_milsces() - jiffies
                  << " ms";

        is_saving_.store(false);
    }));
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

yuki::Status HashDB::DoOpen(size_t *be_read) {
    using yuki::Slice;
    using yuki::Status;
    using yuki::FilePath;
    using yuki::Strings;

    yuki::FilePath manifest_path(db_dir_);
    manifest_path.Append("MANIFEST");

    std::string buf;
    auto rv = yuki::Strings::FromFile(manifest_path, &buf);
    if (rv.Failed()) {
        return rv;
    }

    int version;
    if (!ValueTraits<int>::Parse(yuki::Slice(buf), &version)) {
        LOG(ERROR) << "bad MANIFEST file: " << buf;
        return Status::Corruptionf("bad MANIFEST file: %s", buf.c_str());
    }

    yuki::FilePath table_path(db_dir_);
    table_path.Append(yuki::Strings::Format("table-%d", version));
    bool exist;
    rv = table_path.Exist(&exist);
    if (rv.Failed()) {
        PLOG(ERROR) << rv.ToString();
        return rv;
    }

    if (exist) {
        TableOptions options;
        options.file_name = Slice(table_path.Get());

        rv = LoadTable(options, this);
        if (rv.Failed()) {
            LOG(ERROR) << "load table [" << id_ << "] fail: " <<
            rv.ToString();
            return rv;
        }
    }

    FilePath log_path(db_dir_);
    log_path.Append(yuki::Strings::Format("log-%d", version));
    rv = DBRedo(yuki::Slice(log_path.Get()), this, be_read);
    if (rv.Failed()) {
        LOG(ERROR) << "redo fail, from file: " << log_path.Get();
        return rv;
    }

    version_ = version;
    return Status::OK();
}

yuki::Status HashDB::DoCheckpoint(bool force) {
    using yuki::Status;
    using yuki::Slice;
    using yuki::Strings;

    // 10mb
    if (is_saving_.load()) {
        return Status::Corruptionf("checkpoint in progress...");
    }
    if (!force && log_->written_bytes() < kLogSizeForCheckpoint) {
        return Status::OK();
    }
    is_saving_.store(true);

    auto new_version = version_ + 1;
    auto rv = SaveTable(new_version);
    if (rv.Failed()) {
        is_saving_.store(false);
        return rv;
    }

    rv = CreateLogFile(new_version, &log_fd_);
    log_->Reset(NewPosixFileOutputStream(log_fd_));
    version_ = new_version;

    if (rv.Failed()) {
        is_saving_.store(false);
        return rv;
    }

    rv = SaveVersion();
    is_saving_.store(false);
    return rv;
}

yuki::Status HashDB::DoSave() {
    using yuki::Status;
    using yuki::Slice;
    using yuki::Strings;

    // 10mb
    mutex_.lock();
    auto new_version = version_ + 1;
    mutex_.unlock();

    auto rv = SaveTable(new_version);
    if (rv.Failed()) {
        return rv;
    }

    mutex_.lock();
    rv = CreateLogFile(new_version, &log_fd_);
    log_->Reset(NewPosixFileOutputStream(log_fd_));
    version_ = new_version;
    mutex_.unlock();

    if (rv.Failed()) {
        return rv;
    }

    return SaveVersion();
}

yuki::Status HashDB::SaveTable(int version) {
    using yuki::Status;
    using yuki::Slice;
    using yuki::Strings;

    yuki::FilePath table_path(db_dir_);
    table_path.Append(yuki::Strings::Format("table-%d", version));

    TableOptions options;
    options.file_name = Slice(table_path.Get());
    options.overwrite = false;

    auto rv = DumpTable(&options, this);
    if (rv.Failed()) {
        LOG(ERROR) << "save table fail" << rv.ToString();
        return rv;
    }
    work_queue_->PostCloseFile(options.fd);

    return Status::OK();
}

yuki::Status HashDB::CreateLogFile(int version, int *fd) {
    using yuki::Status;
    using yuki::Slice;
    using yuki::Strings;

    yuki::FilePath log_path(db_dir_);
    log_path.Append(yuki::Strings::Format("log-%d", version));

    work_queue_->PostCloseFile(log_fd_);

    *fd = open(log_path.Get().c_str(), O_CREAT|O_WRONLY|O_APPEND, 0664);
    if (*fd < 0) {
        PLOG(ERROR) << "create log file fail.";
        return Status::Systemf("open %s fail", log_path.Get().c_str());
    }

    return Status::OK();
}

yuki::Status HashDB::SaveVersion() {
    using yuki::Strings;

    yuki::FilePath manifest_path(db_dir_);
    manifest_path.Append("MANIFEST");

    auto rv = Strings::ToFile(manifest_path, Strings::Format("%d", version_));
    if (rv.Failed()) {
        PLOG(ERROR) << "write MANIFEST file fail." << rv.ToString();
    }
    return rv;
}

} // namespace yukino