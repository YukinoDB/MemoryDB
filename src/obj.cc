#include "obj.h"
#include "value_traits.h"
#include "serialized_io.h"

namespace yukino {

void ObjRelease(Obj *ob) {
    if (!ob) {
        return;
    }

    switch (ob->type()) {
    case YKN_STRING:
    case YKN_INTEGER:
        ob->Release();
        break;

    case YKN_LIST:
        static_cast<List *>(ob)->Release();
        break;

    default:
        DLOG(FATAL) << "noreached";
        break;
    }
}

bool ObjCastIntIf(Obj *ob, int64_t *value) {
    if (!ob) {
        return false;
    }

    switch (ob->type()) {
    case YKN_STRING:
        return ValueTraits<int64_t>::Parse(static_cast<String *>(ob)->data(),
                                           value);

    case YKN_INTEGER:
        *value = static_cast<Integer*>(ob)->data();
        return true;

    default:
        break;
    }
    return false;
}

size_t ObjSerialize(Obj *ob, SerializedOutputStream *serializer) {
    auto size = serializer->WriteByte(ob->raw);

    switch (ob->type()) {
        case YKN_INTEGER: {
            auto *bytes = &ob->raw + 1;
            int num_bytes = 0;
            for (;;) {
                ++num_bytes;
                size += serializer->WriteByte(*bytes);
                if (*bytes < 0x80) {
                    break;
                }
                ++bytes;
                DCHECK_LE(num_bytes, yuki::Varint::kMax64Len);
            }
        } break;

        case YKN_STRING:
            size += serializer->WriteSlice(static_cast<String*>(ob)->data());
            break;

        case YKN_LIST: {
            auto list = static_cast<List *>(ob)->stub();

            uint32_t n = static_cast<uint32_t>(list->size());
            size += serializer->WriteInt32(n);
            List::Stub::Iterator iter(list);
            for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
                if (n-- == 0) {
                    break;
                }
                size += ObjSerialize(ObjAddRef(iter.value()), serializer);
                ObjRelease(iter.value());
            }
        } break;

        default:
            DLOG(FATAL) << "noreached";
            return 0;
    }

    return size;
}

#define CALL(expr) if (!(expr)) { return 0; } (void)0

Obj *ObjDeserialize(SerializedInputStream *deserializer) {
    uint8_t type;
    CALL(deserializer->ReadByte(&type));

    switch (static_cast<ObjTy>(type)) {
        case YKN_INTEGER: {
            int64_t value;
            CALL(deserializer->ReadSInt64(&value));
            return Integer::New(value);
        } break;

        case YKN_STRING: {
            yuki::Slice value;
            std::string stub;
            CALL(deserializer->ReadString(&value, &stub));
            return String::New(value);
        }break;

        case YKN_LIST: {
            std::unique_ptr<List> list(List::New());
            uint32_t n;
            CALL(deserializer->ReadInt32(&n));
            while (n--) {
                auto elem = ObjDeserialize(deserializer);
                if (!elem) {
                    return nullptr;
                }
                list->stub()->InsertTail(elem);
            }
            return list.release();
        }break;

        default:
            break;
    }
    return nullptr;
}

} // namespace yukino