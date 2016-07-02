#include "cocurrent_hash_map.h"
#include "key.h"
#include "obj.h"
#include "gtest/gtest.h"

namespace yukino {

class CocurrentHashMapTest : public ::testing::Test {
public:
    virtual void SetUp() override {
        ASSERT_EQ(nullptr, map_);
        map_ = new CocurrentHashMap(1023);
    }

    virtual void TearDown() override {
        ASSERT_NE(nullptr, map_);
        delete map_;
        map_ = nullptr;
    }

protected:
    CocurrentHashMap *map_ = nullptr;
};

TEST_F(CocurrentHashMapTest, Sanity) {
    auto rv = map_->Put(yuki::Slice("name"), 0, String::New(yuki::Slice("Jake")));
    ASSERT_TRUE(rv.Ok());
    rv = map_->Put(yuki::Slice("age"), 0, String::New(yuki::Slice("100")));
    ASSERT_TRUE(rv.Ok());

    Obj *obj = nullptr;
    rv = map_->Get(yuki::Slice("name"), nullptr, &obj);
    ASSERT_TRUE(rv.Ok());

    ASSERT_EQ(YKN_STRING, obj->type());
    auto str = static_cast<String *>(obj);
    ASSERT_EQ("Jake", str->data().ToString());
}

} // namespace yukino