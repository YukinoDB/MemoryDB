#include "configuration.h"
#include "yuki/strings.h"
#include "gtest/gtest.h"

namespace yukino {

TEST(ConfigurationTest, Sanity) {
    Configuration conf;

    EXPECT_EQ("127.0.0.1", conf.address());
    EXPECT_EQ(7000, conf.port());
    EXPECT_EQ("", conf.pid_file());
}

TEST(ConfigurationTest, ProcessItem) {
    Configuration conf;

    std::vector<yuki::Slice> args;
    auto rv = yuki::Strings::Split("address 0.0.0.0", "\\s+", &args);
    ASSERT_TRUE(rv.Ok());
    rv = conf.ProcessConfItem(args);
    ASSERT_TRUE(rv.Ok());

    EXPECT_EQ("0.0.0.0", conf.address());

    args.clear();
    rv = yuki::Strings::Split("port 7777", "\\s+", &args);
    ASSERT_TRUE(rv.Ok());
    rv = conf.ProcessConfItem(args);
    ASSERT_TRUE(rv.Ok());

    EXPECT_EQ(7777, conf.port());
}

TEST(ConfigurationTest, ProcessDBItems) {
    Configuration conf;

    std::vector<yuki::Slice> args = {
        yuki::Slice("db"),
        yuki::Slice("hash"),
    };

    auto rv = conf.ProcessConfItem(args);
    ASSERT_TRUE(rv.Ok());

    EXPECT_EQ(1, conf.num_db_conf());
    EXPECT_EQ(DB_HASH, conf.db_conf(0).type);
    EXPECT_FALSE(conf.db_conf(0).persistent);
    EXPECT_EQ(0, conf.db_conf(0).memory_limit);

    args.clear();
    args.push_back(yuki::Slice("db"));
    args.push_back(yuki::Slice("order"));
    args.push_back(yuki::Slice("memory"));
    args.push_back(yuki::Slice("1024000"));

    rv = conf.ProcessConfItem(args);
    ASSERT_TRUE(rv.Ok());
    EXPECT_EQ(2, conf.num_db_conf());
    EXPECT_EQ(DB_ORDER, conf.db_conf(1).type);
    EXPECT_FALSE(conf.db_conf(1).persistent);
    EXPECT_EQ(1024000, conf.db_conf(1).memory_limit);
}

} // namespace yukino