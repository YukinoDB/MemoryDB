#ifndef YUKINO_PERSISTENT_H_
#define YUKINO_PERSISTENT_H_

#include "yuki/status.h"
#include "yuki/slice.h"
#include <string>

namespace yukino {

class DB;
class InputStream;
class OutputStream;

struct TableOptions {
    yuki::Slice   file_name;
    bool          overwrite;
    int           fd;

    TableOptions();
};

yuki::Status DumpTable(TableOptions *options, DB *db);
yuki::Status LoadTable(TableOptions options, DB *db);

yuki::Status DBRedo(yuki::SliceRef file_name, DB *db);

} // namespace yukino

#endif // YUKINO_PERSISTENT_H_