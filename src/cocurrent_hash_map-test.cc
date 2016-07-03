#include "cocurrent_hash_map.h"
#include "key.h"
#include "obj.h"
#include "yuki/strings.h"
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
    ObjRelease(obj);
}

TEST_F(CocurrentHashMapTest, ResizeSlots) {
    map_->Put(yuki::Slice("1"), 0, String::New(yuki::Slice("Jake")));
    map_->Put(yuki::Slice("2"), 0, String::New(yuki::Slice("Jake")));
    map_->Put(yuki::Slice("3"), 0, String::New(yuki::Slice("Jake")));
    map_->Put(yuki::Slice("4"), 0, String::New(yuki::Slice("Jake")));
    ASSERT_EQ(4, map_->num_keys());

    map_->TEST_ResizeSlots(1023);
    ASSERT_EQ(1860, map_->num_slots());

    map_->TEST_ResizeSlots(1860);
    ASSERT_EQ(3381, map_->num_slots());

    map_->TEST_ResizeSlots(1);
    ASSERT_EQ(1023, map_->num_slots());

    auto rv = map_->Get(yuki::Slice("1"), nullptr, nullptr);
    EXPECT_TRUE(rv.Ok());
    rv = map_->Get(yuki::Slice("2"), nullptr, nullptr);
    EXPECT_TRUE(rv.Ok());
    rv = map_->Get(yuki::Slice("3"), nullptr, nullptr);
    EXPECT_TRUE(rv.Ok());
    rv = map_->Get(yuki::Slice("4"), nullptr, nullptr);
    EXPECT_TRUE(rv.Ok());
}

TEST_F(CocurrentHashMapTest, LargePut) {
    auto N = 100000;
    for (int i = 0; i < N; i++) {
        auto key = yuki::Strings::Format("%d", i);
        auto val = yuki::Strings::Format("<%d>", i);
        auto rv = map_->Put(yuki::Slice(key), 0, String::New(yuki::Slice(val)));
        ASSERT_TRUE(rv.Ok()) << "key:" << key << " val:" << val;
    }
    ASSERT_EQ(N, map_->num_keys());

    for (int i = 0; i < N; i++) {
        auto key = yuki::Strings::Format("%d", i);
        auto val = yuki::Strings::Format("<%d>", i);
        Obj *obj = nullptr;
        auto rv = map_->Get(yuki::Slice(key), nullptr, &obj);
        String *s = static_cast<String *>(obj);
        ASSERT_TRUE(rv.Ok())
            << "key:" << key << " val:" << s->data().ToString();
        ASSERT_EQ(val, s->data().ToString())
            << "key:" << key << " val:" << s->data().ToString();
    }
}

} // namespace yukino