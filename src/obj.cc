#include "obj.h"
#include "value_traits.h"

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

} // namespace yukino