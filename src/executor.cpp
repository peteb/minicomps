#include <minicomps/executor.h>

namespace mc {
std::atomic_int executor::num_lock_failures_(0);
}
