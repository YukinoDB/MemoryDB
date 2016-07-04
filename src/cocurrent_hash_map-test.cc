#include "cocurrent_hash_map.h"
#include "key.h"
#include "obj.h"
#include "yuki/strings.h"
#include "gtest/gtest.h"
#include <thread>

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
    ObjRelease(str);
}

TEST_F(CocurrentHashMapTest, Deletion) {
    map_->Put(yuki::Slice("id.1000"), 0, String::New(yuki::Slice("Jake")));
    map_->Put(yuki::Slice("id.1001"), 0, String::New(yuki::Slice("Jake")));
    map_->Put(yuki::Slice("id.1002"), 0, String::New(yuki::Slice("Jake")));

    EXPECT_TRUE(map_->Delete(yuki::Slice("id.1000")));
    EXPECT_TRUE(map_->Delete(yuki::Slice("id.1002")));
    EXPECT_TRUE(map_->Delete(yuki::Slice("id.1001")));

    auto rv = map_->Get(yuki::Slice("id.1000"), nullptr, nullptr);
    EXPECT_TRUE(rv.Failed());
    EXPECT_EQ(yuki::Status::kNotFound, rv.Code());
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

    EXPECT_TRUE(map_->Exist(yuki::Slice("1")));
    EXPECT_TRUE(map_->Exist(yuki::Slice("2")));
    EXPECT_TRUE(map_->Exist(yuki::Slice("3")));
    EXPECT_TRUE(map_->Exist(yuki::Slice("4")));
}

TEST_F(CocurrentHashMapTest, LargePut) {
    const auto N = 100000;

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

TEST_F(CocurrentHashMapTest, MutliThreadPutting) {
    std::thread threads[8];

    for (int i = 0; i < arraysize(threads); i++) {
        threads[i] = std::move(std::thread([&] (int num) {
            for (int j = num * 1000; j < (num + 1) * 1000; j++) {
                auto key = yuki::Strings::Format("%d", j);
                auto val = yuki::Strings::Format("<%d>", j);
                auto rv = map_->Put(yuki::Slice(key), 0,
                                    String::New(yuki::Slice(val)));
                ASSERT_TRUE(rv.Ok()) << "key:" << key << " val:" << val;
            }
        }, i));
    }

    for (int i = 0; i < arraysize(threads); i++) {
        threads[i].join();
    }

    EXPECT_EQ(arraysize(threads) * 1000, map_->num_keys());
    EXPECT_TRUE(map_->Exist(yuki::Slice("1")));

    for (int i = 0; i < arraysize(threads) * 1000; i++) {
        auto key = yuki::Strings::Format("%d", i);
        EXPECT_TRUE(map_->Exist(yuki::Slice(key)));
    }
}

TEST_F(CocurrentHashMapTest, MutliThreadDeleting) {
    const auto N = 1000;
    std::thread threads[8];

    for (int i = 0; i < arraysize(threads) * N; i++) {
        auto key = yuki::Strings::Format("%d", i);
        auto val = yuki::Strings::Format("<%d>", i);
        auto rv = map_->Put(yuki::Slice(key), 0,
                            String::New(yuki::Slice(val)));
        ASSERT_TRUE(rv.Ok()) << "key:" << key << " val:" << val;
    }

    ASSERT_EQ(arraysize(threads) * N, map_->num_keys());

    for (int i = 0; i < arraysize(threads); i++) {
        threads[i] = std::move(std::thread([&] (int num) {
            for (int j = num * 1000; j < (num + 1) * 1000; j++) {
                auto key = yuki::Strings::Format("%d", j);
                auto rv = map_->Delete(yuki::Slice(key));
                ASSERT_TRUE(rv) << "key:" << key;
            }
        }, i));
    }
    for (int i = 0; i < arraysize(threads); i++) {
        threads[i].join();
    }

    ASSERT_EQ(0, map_->num_keys());
    ASSERT_EQ(1023, map_->num_slots());
}

TEST_F(CocurrentHashMapTest, MutliThreadGetting) {
    const int N = 1000;
    std::thread readers[8];

    std::thread writer([&] () {
        for (int i = 0; i < arraysize(readers) * N; i++) {
            auto key = yuki::Strings::Format("%d", i);
            auto val = yuki::Strings::Format("<%d>", i);
            auto rv = map_->Put(yuki::Slice(key), 0,
                                String::New(yuki::Slice(val)));
            ASSERT_TRUE(rv.Ok()) << "key:" << key << " val:" << val;
        }
    });

    std::atomic<int> hit(0);

    for (int i = 0; i < arraysize(readers); i++) {
        readers[i] = std::move(std::thread([&] () {
            for (int j = 0; j < arraysize(readers) * N; j++) {
                auto key = yuki::Strings::Format("%lu",
                                                 rand() % arraysize(readers) * N);
                if (map_->Exist(yuki::Slice(key))) {
                    hit.fetch_add(1);
                }
            }
        }));
    }

    writer.join();
    for (auto i = 0; i < arraysize(readers); i++) {
        readers[i].join();
    }
    EXPECT_GT(hit.load(), 0);

}

} // namespace yukino