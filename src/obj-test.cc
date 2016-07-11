#include "obj.h"
#include "handle.h"
#include "gtest/gtest.h"

namespace yukino {

TEST(ObjTest, HandleRefCounting) {
    auto obj = String::New(yuki::Slice("1234"));
    EXPECT_EQ(0, obj->RefCount());

    Handle<String> str(obj);
    EXPECT_EQ(1, obj->RefCount());
    EXPECT_EQ(obj->RefCount(), str.ref_count());

    EXPECT_EQ("1234", str->data().ToString());
}

TEST(ObjTest, HandleAssign) {
    Handle<Integer> obj(Integer::New(100));
    EXPECT_EQ(1, obj.ref_count());
    EXPECT_EQ(100, obj->data());

    obj = Integer::New(99);
    EXPECT_EQ(1, obj.ref_count());
    EXPECT_EQ(99, obj->data());
}

} // namespace yukino