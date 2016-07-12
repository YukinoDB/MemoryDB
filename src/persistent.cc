#include "persistent.h"
#include "key.h"
#include "obj.h"
#include "db.h"
#include "iterator.h"
#include "protocol.h"
#include "bin_log.h"
#include "basic_io.h"
#include "handle.h"
#include "basic_io.h"
#include "serialized_io.h"
#include <fcntl.h>
#include <unistd.h>

namespace yukino {

yuki::Status RedoCommand(const Command &cmd,
                         const std::vector<Handle<Obj>> &args,
                         int64_t version,
                         DB *db);

yuki::Status DumpKeyValuePair(KeyBoundle *key, Obj *value,
                              SerializedOutputStream *serializer);

TableOptions::TableOptions()
    : overwrite(false)
    , fd(-1) {
}

yuki::Status DumpTable(TableOptions *options, DB *db) {
    using yuki::Slice;
    using yuki::Status;

    int fd = -1;
    if (options->overwrite) {
        fd = open(options->file_name.ToString().c_str(),
                  O_CREAT|O_TRUNC|O_WRONLY, 0644);
    } else {
        fd = open(options->file_name.ToString().c_str(),
                  O_CREAT|O_EXCL|O_WRONLY, 0644);
    }
    if (fd < 0) {
        PLOG(ERROR) << "can not open table file: " <<
                       options->file_name.ToString();
        return Status::Systemf("can not open table file: %s",
                               options->file_name.ToString().c_str());
    }

    auto output = NewPosixFileOutputStream(fd);
    // write 16 bytes header
    output->Write(Slice("*YKN\x00\x00\x00\x00\x01\0x01\x00\x00\x00\x00\x00\x00",
                        16));
    options->fd = fd;

    VerifiedOutputStreamProxy proxy(output, true, 0);
    SerializedOutputStream serializer(&proxy, false);
    std::unique_ptr<Iterator> iter(db->iterator());
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
        auto rv = DumpKeyValuePair(iter->key(), iter->value(), &serializer);
        if (rv.Failed()) {
            return rv;
        }
    }

    if (lseek(options->fd, 4, SEEK_SET) < 0) {
        PLOG(ERROR) << "seek fail";

        close(options->fd);
        options->fd = -1;
        return Status::Systemf("seek fail");
    }
    serializer.WriteFixed32(proxy.crc32_checksum());
    return Status::OK();
}

yuki::Status LoadTable(const TableOptions &options, DB *db) {
    using yuki::Slice;
    using yuki::Status;

    FILE *fp = fopen(options.file_name.ToString().c_str(), "r");
    if (!fp) {
        PLOG(ERROR) << "can not open file: " << options.file_name.ToString();
        return Status::Systemf("can not open file: %s",
                               options.file_name.ToString().c_str());
    }

    char magic[4];
    if (fread(magic, 1, sizeof(magic), fp) != sizeof(magic) ||
        memcmp(magic, "*YKN", 4) != 0) {
        fclose(fp);
        return Status::Corruptionf("bad table file header. "
                                   "Is not yukino db table file?");
    }

    uint32_t checksum = 0;
    if (fread(&checksum, 1, sizeof(checksum), fp) != sizeof(checksum)) {
        fclose(fp);
        return Status::Corruptionf("bad table file header. "
                                   "Is not yukino db table file?");
    }

    if (fseek(fp, 16, SEEK_SET) != 0) {
        PLOG(ERROR) << "can not seek file: " << options.file_name.ToString();
        fclose(fp);
        return Status::Systemf("can not seek file: %s",
                               options.file_name.ToString().c_str());
    }

    VerifiedInputStreamProxy proxy(NewFileInputStream(fp), true, 0);
    SerializedInputStream deserializer(&proxy, false);

    uint32_t key_size;
    std::string stub;
    while (deserializer.ReadInt32(&key_size)) {
        // key:
        yuki::Slice key;
        if (!deserializer.stub()->Read(key_size, &key, &stub)) {
            fclose(fp);
            return Status::Corruptionf("bad table file format.");
        }

        uint8_t type;
        if (!deserializer.ReadByte(&type)) {
            fclose(fp);
            return Status::Corruptionf("bad table file format.");
        }

        uint64_t version;
        if (!deserializer.ReadInt64(&version)) {
            fclose(fp);
            return Status::Corruptionf("bad table file format.");
        }

        // value:
        auto obj = ObjDeserialize(&deserializer);
        if (!obj) {
            return deserializer.status();
        }
        db->Put(key, version, obj);
    }

    fclose(fp);
    if (checksum != proxy.crc32_checksum()) {
        return Status::Corruptionf("crc32 checksum fail %u vs %u", checksum,
                                   proxy.crc32_checksum());
    }
    return deserializer.status();
}

yuki::Status DBRedo(yuki::SliceRef file_name, DB *db, size_t *be_read) {
    using yuki::Slice;
    using yuki::Status;

    FILE *fp = fopen(file_name.ToString().c_str(), "r");
    if (!fp) {
        PLOG(ERROR) << "can not open: " << file_name.ToString();
        return Status::Systemf("can not open: %s", file_name.ToString().c_str());
    }

    BinLogReader reader(NewFileInputStream(fp), true, 512);

    Operator op;
    yuki::Status status;
    while (reader.Read(&op, &status)) {
        if (status.Failed()) {
            break;
        }

        if (op.cmd < 0 || op.cmd >= MAX_COMMANDS) {
            status = Status::Corruptionf("bad command code %d", op.cmd);
            break;
        }
        status = RedoCommand(kCommands[op.cmd], op.args,op. version, db);
        if (status.Failed()) {
            break;
        }

        status = Status::OK();
    }

final:
    if (be_read) {
        *be_read = ftell(fp);
    }
    fclose(fp);
    return status;
}

#define GET_KEY(key, index) \
    String *key = nullptr; \
    do { \
        if (args[index]->type() != YKN_STRING) { \
            return Status::Corruptionf("bad key type"); \
        } \
        key = static_cast<String *>(args[0].get()); \
    } while (0)

yuki::Status RedoCommand(const Command &cmd,
                         const std::vector<Handle<Obj>> &args,
                         int64_t version,
                         DB *db) {
    using yuki::Slice;
    using yuki::Status;

    switch (cmd.code) {
        case CMD_SET: {
            GET_KEY(key, 0);
            db->Put(key->data(), version, args[1].get());
        }break;

        case CMD_DEL: {
            GET_KEY(key, 0);
            db->Delete(key->data());
        } break;

        case CMD_LIST: {
            GET_KEY(key, 0);
            Handle<List> list(List::New());
            for (size_t i = 1; i < args.size(); i++) {
                list->stub()->InsertTail(args[i].get());
            }
            db->Put(key->data(), version, list.get());
        } break;

        case CMD_LPUSH:
        case CMD_RPUSH: {
            GET_KEY(key, 0);
            Obj *obj;
            auto rv = db->Get(key->data(), nullptr, &obj);
            if (rv.Failed()) {
                return Status::Corruptionf("list: %s not exist",
                                           key->data().ToString().c_str());
            }
            if (obj->type() != YKN_LIST) {
                return Status::Corruptionf("%s: not a list", cmd.z);
            }

            List *list = static_cast<List *>(obj);
            if (cmd.code == CMD_LPUSH) {
                for (size_t i = 1; i < args.size(); i++) {
                    list->stub()->InsertHead(args[i].get());
                }
            } else {
                for (size_t i = 1; i < args.size(); i++) {
                    list->stub()->InsertTail(args[i].get());
                }
            }
            ObjRelease(list);
        } break;

        case CMD_LPOP:
        case CMD_RPOP: {
            GET_KEY(key, 0);
            Obj *obj;
            auto rv = db->Get(key->data(), nullptr, &obj);
            if (rv.Failed()) {
                return Status::Corruptionf("list: %s not exist",
                                           key->data().ToString().c_str());
            }
            if (obj->type() != YKN_LIST) {
                return Status::Corruptionf("%s: not a list", cmd.z);
            }

            List *list = static_cast<List *>(obj);
            Obj *stub;
            if (cmd.code == CMD_LPOP) {
                list->stub()->PopHead(&stub);
            } else {
                list->stub()->PopTail(&stub);
            }
            ObjRelease(stub);
            ObjRelease(obj);
        } break;

        default:
            break;
    }
    return Status::OK();
}

yuki::Status DumpKeyValuePair(KeyBoundle *key, Obj *value,
                              SerializedOutputStream *serializer) {
    using yuki::Slice;
    using yuki::Status;

    uint8_t *raw = &key->raw;
    size_t len;
    auto key_size = yuki::Varint::Decode32(raw, &len);
    auto total_size = key_size + len + 1 +
                      yuki::Varint::Sizeof64(key->version().number);
    if (serializer->stub()->Write(raw, total_size) != total_size) {
        return serializer->status();
    }

    ObjSerialize(value, serializer);
    return serializer->status();
}

} // namespace yukino