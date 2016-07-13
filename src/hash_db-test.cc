#include "hash_db.h"
#include "background.h"
#include "configuration.h"
#include "key.h"
#include "obj.h"
#include "protocol.h"
#include "yuki/strings.h"
#include "yuki/file.h"
#include "yuki/file_path.h"
#include "gtest/gtest.h"

namespace yukino {

class HashDBTest : public ::testing::Test {
public:
    virtual void SetUp() override {
        queue_ = new BackgroundWorkQueue;
        background_ = new Background;
        background_->set_queue(queue_);

        background_->AsyncRun();
    }

    virtual void TearDown() override {
        queue_->PostShutdown();
        background_->WaitForShutdown();

        delete background_;
        delete queue_;

        yuki::FilePath db_path(kDataDir);
        db_path.Append("db-0");

        yuki::File::Remove(db_path, true);
    }

protected:
    BackgroundWorkQueue *queue_;
    Background *background_;

    static const char kDataDir[];
};

const char HashDBTest::kDataDir[] = "tests";

TEST_F(HashDBTest, Sanity) {
    using yuki::Slice;

    DBConf conf;

    conf.type = DB_HASH;
    conf.persistent = false;
    conf.memory_limit = 0;

    std::unique_ptr<DB> db(new HashDB(conf, kDataDir, 0, 1023, queue_));
    auto rv = db->Put(Slice("key"), 0, String::New(Slice("obj")));
    ASSERT_TRUE(rv.Ok());

    rv = db->Get(Slice("key"), nullptr, nullptr);
    ASSERT_TRUE(rv.Ok());
}

TEST_F(HashDBTest, Persistent) {
    using yuki::Slice;

    DBConf conf;

    conf.type = DB_HASH;
    conf.persistent = true;
    conf.memory_limit = 0;

    std::unique_ptr<DB> db(new HashDB(conf, kDataDir, 0, 1023, queue_));
    auto rv = db->Open();
    ASSERT_TRUE(rv.Ok()) << rv.ToString();

    std::vector<Handle<Obj>> args;

    args.emplace_back(String::New("key"));
    args.emplace_back(String::New("obj"));

    rv = db->AppendLog(CMD_SET, 0, args);
    ASSERT_TRUE(rv.Ok()) << rv.ToString();

    rv = db->Put(Slice("key"), 0, args[1].get());
    ASSERT_TRUE(rv.Ok()) << rv.ToString();

    rv = db->Checkpoint(true);
    ASSERT_TRUE(rv.Ok()) << rv.ToString();
}

// in-memory:  251193.16 QPS
// 8 threads:  363967.24 QPS
// 16 threads: 332709.50 QPS
// persistent:   8327.71 QPS
// 8 threads:    8914.74 QPS
TEST_F(HashDBTest, AsyncPersistent) {
    using yuki::Slice;

    static const int N = 100000;
    DBConf conf;

    conf.type = DB_HASH;
    conf.persistent = true;
    conf.memory_limit = 0;

    std::unique_ptr<DB> db(new HashDB(conf, kDataDir, 0, 1023, queue_));
    auto rv = db->Open();
    ASSERT_TRUE(rv.Ok()) << rv.ToString();

    std::vector<Handle<Obj>> args;
    std::string value(128, 0);
    for (int i = 0; i < N; i++) {
        auto key = yuki::Strings::Format("[key]-%d--------", i);

        args.emplace_back(String::New(key.data(), key.size()));
        args.emplace_back(String::New(value.data(), value.size()));

        rv = db->AppendLog(CMD_SET, 0, args);
        ASSERT_TRUE(rv.Ok()) << rv.ToString();

        rv = db->Put(Slice(key), 0, args[1].get());
        ASSERT_TRUE(rv.Ok()) << rv.ToString();

        args.clear();
    }
}

TEST_F(HashDBTest, MutliThreadingPersistent) {
    using yuki::Slice;

    static const int N = 10000;

    DBConf conf;

    conf.type = DB_HASH;
    conf.persistent = true;
    conf.memory_limit = 0;

    std::unique_ptr<DB> db(new HashDB(conf, kDataDir, 0, 102300, queue_));
    auto rv = db->Open();
    ASSERT_TRUE(rv.Ok()) << rv.ToString();

    std::thread workers[4];

    for (int i = 0; i < arraysize(workers); i++) {
        workers[i] = std::move(std::thread([&] (int n) {
            std::vector<Handle<Obj>> args;
            std::string value(128, 0);
            for (int i = n * N; i < (n + 1) * N; i++) {
                auto key = yuki::Strings::Format("[key]-%d--------", i);

                args.emplace_back(String::New(key.data(), key.size()));
                args.emplace_back(String::New(value.data(), value.size()));

                rv = db->AppendLog(CMD_SET, 0, args);
                ASSERT_TRUE(rv.Ok()) << rv.ToString();

                rv = db->Put(Slice(key), 0, args[1].get());
                ASSERT_TRUE(rv.Ok()) << rv.ToString();
                
                args.clear();
            }
        }, i));
    }

    int n = arraysize(workers);
    while (n--) {
        workers[n].join();
    }
}

} // namespace yukino