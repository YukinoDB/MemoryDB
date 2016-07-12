#include "hash_db.h"
#include "bin_log.h"
#include "configuration.h"
#include "basic_io.h"
#include "value_traits.h"
#include "persistent.h"
#include "yuki/file_path.h"
#include "yuki/file.h"
#include "yuki/strings.h"
#include <fcntl.h>
#include <sys/stat.h>

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

    // db dir: data_dir/db-<id>/
    FilePath db_dir(data_dir_);
    db_dir.Append(yuki::Strings::Format("db-%d", id_));

    bool exist;
    auto rv = db_dir.Exist(&exist);
    if (rv.Failed()) {
        return rv;
    }

    bool is_new = false;
    if (!exist) {
        rv = yuki::File::MakeDir(db_dir, true);
        if (rv.Failed()) {
            return rv;
        }
        LOG(INFO) << "new db: db-" << id_ << " created. " << db_dir.Get();
        is_new = true;
    }

    // db all clean
    if (is_new) {
        FilePath manifest_path(db_dir);
        manifest_path.Append("MANIFEST");

        rv = Strings::ToFile(manifest_path, "0");
        if (rv.Failed()) {
            PLOG(ERROR) << "write to " << manifest_path.Get() << " fail";
            return rv;
        }

        version_ = 0;
    } else {
        rv = DoOpen();
        if (rv.Failed()) {
            return rv;
        }
    }

    FilePath log_path(db_dir);
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
    log_ = new BinLogWriter(NewPosixFileOutputStream(log_fd_), true, 512);
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

    if (persistent_) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto rv = log_->Append(static_cast<CmdCode>(code), version, args);
        if (rv.Failed()) {
            LOG(ERROR) << "write log error: " << rv.ToString();
            return rv;
        }

        // TODO: post fsync syscall

        rv = DoCheckpoint(false);
        if (rv.Failed()) {
            LOG(ERROR) << "checkpoint fail: " << rv.ToString();
            return rv;
        }
    }
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

yuki::Status HashDB::DoOpen() {
    using yuki::Slice;
    using yuki::Status;
    using yuki::FilePath;
    using yuki::Strings;

    FilePath db_dir(data_dir_);
    db_dir.Append(Strings::Format("db-%d", id_));

    yuki::FilePath manifest_path(db_dir);
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

    yuki::FilePath table_path(db_dir);
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

    FilePath log_path(db_dir);
    log_path.Append(yuki::Strings::Format("log-%d", version));
    rv = DBRedo(yuki::Slice(log_path.Get()), this);
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
    if (is_dumpping_) {
        return Status::Corruptionf("checkpoint in progress...");
    }
    if (!force && log_->written_bytes() < 10UL * 1024UL * 1024UL) {
        return Status::OK();
    }
    is_dumpping_ = true;

    auto new_version = version_ + 1;

    // db dir: data_dir/db-<id>/
    yuki::FilePath db_dir(data_dir_);
    db_dir.Append(yuki::Strings::Format("db-%d", id_));

    yuki::FilePath table_path(db_dir);
    table_path.Append(yuki::Strings::Format("table-%d", new_version));

    TableOptions options;
    options.file_name = Slice(table_path.Get());
    options.overwrite = false;

    auto rv = DumpTable(&options, this);
    if (rv.Failed()) {
        is_dumpping_ = false;
        return rv;
    }

    // TODO: post close fd
    close(options.fd);

    yuki::FilePath log_path(db_dir);
    log_path.Append(yuki::Strings::Format("log-%d", new_version));

    // TODO: post close fd
    close(log_fd_);

    log_fd_ = open(log_path.Get().c_str(), O_CREAT|O_WRONLY|O_APPEND, 0664);
    if (log_fd_ < 0) {
        is_dumpping_ = false;

        PLOG(ERROR) << "open " << log_path.Get() << " fail";
        return Status::Systemf("open %s fail", log_path.Get().c_str());
    }
    log_->Reset(NewPosixFileOutputStream(log_fd_));

    version_ = new_version;

    yuki::FilePath manifest_path(db_dir);
    manifest_path.Append("MANIFEST");
    rv = Strings::ToFile(manifest_path, Strings::Format("%d", version_));
    if (rv.Failed()) {
        is_dumpping_ = false;

        PLOG(ERROR) << "write MANIFEST file fail." << rv.ToString();
        return rv;
    }

    is_dumpping_ = false;
    return Status::OK();
}

} // namespace yukino