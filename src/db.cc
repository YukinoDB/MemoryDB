#include "db.h"
#include "hash_db.h"
#include "configuration.h"

namespace yukino {

DB::DB() {
}

DB::~DB() {
}

/*static*/ DB *DB::New(const yukino::DBConf &conf, const std::string &data_dir,
                       int id) {
    switch (conf.type) {
        case DB_HASH:
            return new HashDB(conf, data_dir, id, 1023);

        case DB_ORDER:
            // TODO:
            break;

        case DB_PAGE:
            // TODO:
            break;

        default:
            DLOG(FATAL) << "noreached";
            break;
    }

    return nullptr;
}

} // namespace yukino