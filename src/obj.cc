#include "obj.h"

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

} // namespace yukino