#include "serialized_io.h"
#include "obj.h"
#include "handle.h"
#include "gtest/gtest.h"

namespace yukino {

TEST(SerializedIOTest, Sanity) {
    std::string buf;

    SerializedOutputStream serializer(NewBufferedOutputStream(&buf), true);
    serializer.WriteByte(0xcc);

    ASSERT_EQ('\xcc', buf[0]);
}

TEST(SerializedIOTest, Int32Serialze) {
    std::string buf;

    SerializedOutputStream serializer(NewBufferedOutputStream(&buf), true);
    serializer.WriteInt32(0);
    ASSERT_EQ(1, buf.size());
    EXPECT_EQ('\x00', buf[0]);

    serializer.WriteInt32(1);
    ASSERT_EQ(2, buf.size());
    EXPECT_EQ('\x01', buf[1]);

    serializer.WriteInt32(0x80);
    ASSERT_EQ(4, buf.size());
    EXPECT_EQ('\x81', buf[2]);
    EXPECT_EQ('\x00', buf[3]);
}

TEST(SerializedIOTest, Int32Deserialize) {
    std::string buf("\x00\x01\x81\x00", 4);

    SerializedInputStream deserializer(NewBufferedInputStream(yuki::Slice(buf)),
                                       true);
    uint32_t value;
    EXPECT_TRUE(deserializer.ReadInt32(&value));
    EXPECT_EQ(0, value);

    EXPECT_TRUE(deserializer.ReadInt32(&value));
    EXPECT_EQ(1, value);

    EXPECT_TRUE(deserializer.ReadInt32(&value));
    EXPECT_EQ(0x80, value);

    EXPECT_FALSE(deserializer.ReadInt32(&value));
}

TEST(SerializedIOTest, SInt32Serialize) {
    std::string buf;

    SerializedOutputStream serializer(NewBufferedOutputStream(&buf), true);
    ASSERT_EQ(1, serializer.WriteSInt32(1));
    ASSERT_EQ(1, buf.size());
    EXPECT_EQ('\x02', buf[0]);

    ASSERT_EQ(1, serializer.WriteSInt32(-1));
    ASSERT_EQ(2, buf.size());
    EXPECT_EQ('\x03', buf[1]);
}

TEST(SerializedIOTest, SInt32Deserialize) {
    std::string buf("\x02\x03", 2);

    SerializedInputStream deserializer(NewBufferedInputStream(yuki::Slice(buf)),
                                       true);
    int32_t value;
    ASSERT_TRUE(deserializer.ReadSInt32(&value));
    EXPECT_EQ(1, value);

    ASSERT_TRUE(deserializer.ReadSInt32(&value));
    EXPECT_EQ(-1, value);
}

TEST(SerializedIOTest, StringSerialize) {
    std::string buf;

    SerializedOutputStream serializer(NewBufferedOutputStream(&buf), true);
    ASSERT_EQ(8, serializer.WriteSlice("0123456", 7));

    ASSERT_EQ(8, buf.size());
    EXPECT_EQ(7, buf[0]);
    EXPECT_STREQ("0123456", buf.c_str() + 1);
}

TEST(SerializedIOTest, StringDeserialize) {
    std::string buf("A0123456", 8);
    buf[0] = '\x07';

    SerializedInputStream deserializer(NewBufferedInputStream(yuki::Slice(buf)),
                                       true);

    yuki::Slice value;
    std::string stub;
    ASSERT_TRUE(deserializer.ReadString(&value, &stub));
    EXPECT_EQ("0123456", value.ToString());
}

TEST(SerializedIOTest, IntegerObjSerialize) {
    Handle<Integer> obj_int(Integer::New(111));

    std::string buf;
    SerializedOutputStream serializer(NewBufferedOutputStream(&buf), true);

    auto size = ObjSerialize(obj_int.get(), &serializer);
    EXPECT_EQ(3, size);
    ASSERT_EQ(3, buf.size());
    EXPECT_EQ(YKN_INTEGER, buf[0]);
    EXPECT_EQ('\x81', buf[1]);
    EXPECT_EQ('\x5e', buf[2]);
}

TEST(SerializedIOTest, IntegerObjDeserialize) {
    std::string buf("\x01\x81\x5e", 3);
    SerializedInputStream deserializer(NewBufferedInputStream(yuki::Slice(buf)),
                                       true);

    auto obj = ObjDeserialize(&deserializer);
    ASSERT_TRUE(obj != nullptr);
    EXPECT_EQ(YKN_INTEGER, obj->type());
    Handle<Integer> obj_int(static_cast<Integer*>(obj));

    EXPECT_EQ(111, obj_int->data());
}

} // namespace yukino