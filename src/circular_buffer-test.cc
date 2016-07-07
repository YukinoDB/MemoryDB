#include "circular_buffer.h"
#include "gtest/gtest.h"
#include "glog/logging.h"

namespace yukino {

TEST(StaticCircularBufferTest, Sanity) {
    StaticCircularBuffer<32> buf;

    std::string body(32, 0);
    size_t written = 0;
    ASSERT_TRUE(buf.CopiedWrite(yuki::Slice(body), &written));
    EXPECT_EQ(32, written);

    //EXPECT_EQ(0, buf.write_once_remain());
    EXPECT_EQ(0, buf.write_remain());

    std::string copied;
    yuki::Slice output;
    ASSERT_TRUE(buf.CopiedReadIfNeed(32, &output, &copied));
    EXPECT_TRUE(copied.empty());
    EXPECT_EQ(32, output.Length());
}

TEST(StaticCircularBufferTest, CircularWriteRead) {
    using yuki::Slice;

    StaticCircularBuffer<7> buf;

    size_t written = 0;
    ASSERT_TRUE(buf.CopiedWrite(Slice("01234", 5), &written));
    EXPECT_EQ(5, written);

    std::string copied;
    Slice output;
    ASSERT_TRUE(buf.CopiedReadIfNeed(7, &output, &copied));
    EXPECT_EQ(5, output.Length());
    EXPECT_TRUE(copied.empty());
    EXPECT_EQ(0, output.Compare(Slice("01234", 5)));

    //--------------------------------------------------------------------------

    ASSERT_TRUE(buf.CopiedWrite(Slice("abcd", 4), &written));
    EXPECT_EQ(4, written);

    ASSERT_TRUE(buf.CopiedReadIfNeed(7, &output, &copied));
    EXPECT_EQ(4, output.Length());
    EXPECT_EQ("abcd", copied);
    EXPECT_EQ(0, output.Compare(Slice("abcd", 4)));

    ASSERT_FALSE(buf.CopiedReadIfNeed(7, &output, &copied));
}

TEST(StaticCircularBufferTest, MutliCircularWriteRead) {
    using yuki::Slice;

    const auto N = 1000;

    StaticCircularBuffer<7> buf;
    std::string copied;
    Slice output;
    size_t written = 0;

    for (int i = 0; i < N; i++) {
        ASSERT_TRUE(buf.CopiedWrite(Slice("01234", 5), &written));
        EXPECT_EQ(5, written);

        ASSERT_TRUE(buf.CopiedReadIfNeed(7, &output, &copied));
        EXPECT_EQ(5, output.Length());
        //EXPECT_EQ("01234", copied);
        EXPECT_EQ(0, output.Compare(Slice("01234", 5)));
    }
}

TEST(StaticCircularBufferTest, AdvanceRewind) {
    using yuki::Slice;

    const auto N = 1000;

    StaticCircularBuffer<7> buf;

    size_t size;

    const char data[] = "ABCDE";
    std::string copied;
    Slice output;

    for (int i = 0; i < N; i++) {
        size_t need = 5;

        while (need != 0) {
            auto p = buf.OnceWriteBuffer(need, &size);
            ASSERT_TRUE(p != nullptr);

            memcpy(p, data + (5 - need), size);
            need -= size;
            buf.Advance(size);
        }

        ASSERT_TRUE(buf.CopiedReadIfNeed(7, &output, &copied));
        EXPECT_EQ(5, output.Length());
        EXPECT_EQ(0, output.Compare(Slice(data, 5))) << output.ToString();
    }
}

} // namespace yukino