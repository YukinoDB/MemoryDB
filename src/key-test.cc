#include "key.h"
#include "gtest/gtest.h"

namespace yukino {

TEST(KeyTest, Sanity) {
    char buf[128];

    auto key_boundle = KeyBoundle::Build(yuki::Slice("name"), 0, 0, buf,
                                         arraysize(buf));
    ASSERT_EQ(4, key_boundle->key_size());
    ASSERT_EQ(0, key_boundle->version().type);
    ASSERT_EQ(0, key_boundle->version().number);
    ASSERT_EQ("name", key_boundle->key().ToString());
}

} // namespace yukino