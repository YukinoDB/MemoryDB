#include "rw_spin_lock.h"

namespace yukino {

/*static*/ const int32_t RWSpinLock::kLockBais = 0x010000000u;
/*static*/ const int     RWSpinLock::kDefaultSpinCount = 100000;

} // namespace yukino