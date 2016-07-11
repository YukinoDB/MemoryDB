#include "lockfree_list.h"
#include "yuki/utils.h"
#include "gtest/gtest.h"
#include <thread>

namespace yukino {

TEST(LockFreeListTest, Sanity) {
    LockFreeList<int> list;
    LockFreeList<int>::Node *tmp;

    list.InsertHead(0);
    list.InsertHead(1);
    list.InsertHead(2);

    EXPECT_EQ(2, list.Take(0, &tmp)->value);
    EXPECT_EQ(1, list.Take(1, &tmp)->value);
    EXPECT_EQ(0, list.Take(2, &tmp)->value);
}

TEST(LockFreeListTest, OrderedInsert) {
    LockFreeList<int> list;
    LockFreeList<int>::Node *tmp;

    list.InsertTail(0);
    list.InsertTail(1);
    list.InsertTail(2);

    EXPECT_EQ(0, list.Take(0, &tmp)->value);
    EXPECT_EQ(1, list.Take(1, &tmp)->value);
    EXPECT_EQ(2, list.Take(2, &tmp)->value);
    EXPECT_EQ(0, list.Take(-1, &tmp)->value);
    EXPECT_EQ(2, tmp->value);
}

TEST(LockFreeListTest, MutliThreadInsertion) {
    LockFreeList<int> list;
    const auto N = 1000;

    std::thread writers[4];
    for (int i = 0; i < arraysize(writers); i++) {
        writers[i] = std::move(std::thread([&] (int id) {
            for (int j = id * N; j < (id + 1) * N; j++) {
                //list.InsertHead(j);
                list.InsertTail(j);
            }
        }, i));
    }

    for (int i = 0; i < arraysize(writers); i++) {
        writers[i].join();
    }

    EXPECT_EQ(N * arraysize(writers), list.size());
}

TEST(LockFreeListTest, MutliThreadPop) {
    LockFreeList<int> list;

    const auto N = 100000;

    std::thread writer([&] () {
        for (int i = 0; i < N; i++) {
            list.InsertHead(i);
        }
    });

    std::thread deleter([&] () {
        int i = N;
        while (i--) {
            int value;
            while (!list.PopHead(&value)) {
            }
        }
    });

    writer.join(); deleter.join();
    ASSERT_EQ(0, list.size());
}

TEST(LockFreeListTest, NoBlockingQueue) {
    LockFreeList<int> list;

    auto const N = 1000;

    std::thread producers[7];
    for (int i = 0; i < arraysize(producers); i++) {
        producers[i] = std::move(std::thread([&] (int id) {
            for (int j = N * id; j < N * (id + 1); j++) {
                list.InsertHead(j);
            }
        }, i));
    }

    int counter = 0;
    int spin = 0;
    std::thread consumer([&] () {
        int value = 0;
        do {
            while (!list.PopTail(&value)) {
                spin++;
            }
            counter++;
        } while (value >= 0);
    });

    for (int i = 0; i < arraysize(producers); i++) {
        producers[i].join();
    }
    list.InsertHead(-1);
    consumer.join();

    EXPECT_EQ(N * arraysize(producers) + 1, counter);
}

} // namespace yukino