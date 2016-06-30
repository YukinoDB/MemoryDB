#include "glog/logging.h"
#include "gtest/gtest.h"
#include "yuki/at_exit.h"

int main(int argc, char *argv[]) {
    google::InitGoogleLogging(argv[0]);
    testing::InitGoogleTest(&argc, argv);
    yuki::AtExit scope(yuki::Linker::INITIALIZER);
    return RUN_ALL_TESTS();
}