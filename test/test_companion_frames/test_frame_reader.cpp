// FrameReader (src/helpers/FrameReader.h) on its own: each read at the end of a
// frame and past it, the cursor, ok() staying false, and the terminator.

#include <gtest/gtest.h>

#include <helpers/FrameReader.h>

namespace {

TEST(FrameReader, ReadsFieldsInOrder) {
  uint8_t buf[16] = {0x16, 0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0xFE, 0xFF, 0xFF, 0xFF, 0x77};
  FrameReader r(buf, 13, 1);
  EXPECT_EQ(r.u8(), 0xAA);
  uint32_t expected32;
  memcpy(&expected32, &buf[2], 4);
  EXPECT_EQ(r.u32(), expected32);
  uint16_t expected16;
  memcpy(&expected16, &buf[6], 2);
  EXPECT_EQ(r.u16(), expected16);
  int32_t expected_i;
  memcpy(&expected_i, &buf[8], 4);
  EXPECT_EQ(r.i32(), expected_i);
  EXPECT_EQ(r.pos(), 12u);
  EXPECT_EQ(r.remaining(), 1u);
  EXPECT_EQ(r.bytes(1), &buf[12]);
  EXPECT_EQ(r.remaining(), 0u);
  EXPECT_TRUE(r.ok());
}

TEST(FrameReader, AReadAtTheEndFitsExactly) {
  uint8_t buf[8] = {1, 2, 3, 4, 5};
  FrameReader r(buf, 5, 1);
  EXPECT_TRUE(r.has(4));
  EXPECT_FALSE(r.has(5));
  r.u32();
  EXPECT_TRUE(r.ok());
  EXPECT_FALSE(r.has(1));
  EXPECT_TRUE(r.has(0));
  EXPECT_NE(r.bytes(0), nullptr);
}

TEST(FrameReader, AReadPastTheEndYieldsZeroAndStops) {
  uint8_t buf[8] = {9, 0x11, 0x22, 0x33, 0x5A, 0x5A, 0x5A, 0x5A};   // a stale tail behind len 4
  {
    FrameReader r(buf, 4, 1);
    EXPECT_EQ(r.u32(), 0u);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.pos(), 1u);           // the cursor did not move
    EXPECT_EQ(r.u8(), 0);             // and stays failed, even for a byte that is there
    EXPECT_FALSE(r.ok());
  }
  {
    FrameReader r(buf, 4, 1);
    EXPECT_EQ(r.bytes(4), nullptr);
    EXPECT_FALSE(r.has(0));
  }
  {
    FrameReader r(buf, 4, 1);
    r.skip(4);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.remaining(), 3u);
  }
  {
    FrameReader r(buf, 4, 3);
    EXPECT_EQ(r.u16(), 0);
    EXPECT_EQ(r.i32(), 0);
    EXPECT_FALSE(r.ok());
  }
}

TEST(FrameReader, AStartBeyondTheFrameIsAnOverrun) {
  uint8_t buf[4] = {1, 2, 3, 4};
  FrameReader r(buf, 2, 3);
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.remaining(), 0u);
}

TEST(FrameReader, RestAsCStringWritesTheTerminatorAtLen) {
  uint8_t buf[8] = {66, 'v', 'e', 'r', 'X', 'Y', 'Z', 'W'};
  FrameReader r(buf, 4, 1);
  char* s = r.restAsCString();
  EXPECT_STREQ(s, "ver");
  EXPECT_EQ(buf[4], 0);
  EXPECT_EQ(buf[5], 'Y');            // nothing behind it is touched
  EXPECT_EQ(r.remaining(), 0u);
  EXPECT_TRUE(r.ok());

  FrameReader empty(buf, 1, 1);
  EXPECT_STREQ(empty.restAsCString(), "");
  EXPECT_EQ(buf[1], 0);
}

TEST(FrameReader, CountsOverruns) {
  uint8_t buf[4] = {1, 2, 3, 4};
  int before = frame_reader_overruns;
  FrameReader r(buf, 2, 1);
  r.u8();
  EXPECT_EQ(frame_reader_overruns, before);
  r.u8();
  EXPECT_EQ(frame_reader_overruns, before + 1);
}

}  // namespace
