#include <gtest/gtest.h>

#include <helpers/CommonNodePrefs.h>

// extra_sf and discovery_mod_timestamp live in NodePrefs but were missing from
// its serialisation, so a server role lost both on every reboot. The binary
// loader still read discovery_mod_timestamp (CommonCLI.cpp, offset 162), which
// is why the loss only shows on nodes that boot from /prefs.json, meaning every
// node since 1.17.

namespace {

class StringInput : public Stream {
  const char* _text;
  size_t _pos, _len;
public:
  explicit StringInput(const char* text) : _text(text), _pos(0), _len(strlen(text)) { }
  int available() override { return (int) (_len - _pos); }
  int read() override { return _pos < _len ? _text[_pos++] : -1; }
  int peek() override { return _pos < _len ? _text[_pos] : -1; }
};

class StringOutput : public Stream {
  std::string _text;
public:
  using Print::write;
  size_t write(uint8_t b) override { _text.push_back((char) b); return 1; }
  size_t print(double v, int places = 2) override {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", places, v);
    return Print::print(buf);
  }
  size_t print(unsigned int v, int base = 10) override { return printNumber((unsigned long long) v); }
  size_t print(unsigned long v, int base = 10) override { return printNumber((unsigned long long) v); }
  size_t print(int v, int base = 10) override { return printSigned(v); }
  size_t print(long v, int base = 10) override { return printSigned(v); }
  const std::string& text() const { return _text; }
private:
  size_t printNumber(unsigned long long v) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", v);
    return Print::print(buf);
  }
  size_t printSigned(long long v) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%lld", v);
    return Print::print(buf);
  }
};

std::string save(NodePrefs& p) {
  StringOutput out;
  EXPECT_TRUE(p.saveSerial(out));
  return out.text();
}

bool contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

class NodePrefsSerialisation : public ::testing::Test {
protected:
  NodePrefs p;
};

// A fresh NodePrefs has to start from a defined extra_sf, because it is now
// written out and "get extra.sf" reads it.
TEST_F(NodePrefsSerialisation, ExtraSfStartsZeroed) {
  for (size_t i = 0; i < sizeof(p.extra_sf); i++) {
    EXPECT_EQ(0, (int) p.extra_sf[i]) << "index " << i;
  }
}

TEST_F(NodePrefsSerialisation, SaveWritesTheDiscoveryTimestamp) {
  p.discovery_mod_timestamp = 1760000000;
  EXPECT_TRUE(contains(save(p), "disc_mod:1760000000"));
}

TEST_F(NodePrefsSerialisation, SaveWritesTheExtraSpreadFactors) {
  p.extra_sf[0] = 9; p.extra_sf[1] = 10; p.extra_sf[2] = 11; p.extra_sf[3] = 0;
  EXPECT_TRUE(contains(save(p), "extra_sf:\"090A0B00\""));
}

TEST_F(NodePrefsSerialisation, LoadRestoresTheDiscoveryTimestamp) {
  StringInput in("{disc_mod:1760000000}");
  EXPECT_TRUE(p.loadSerial(in));
  EXPECT_EQ(1760000000u, p.discovery_mod_timestamp);
}

TEST_F(NodePrefsSerialisation, LoadRestoresTheExtraSpreadFactors) {
  StringInput in("{extra_sf:\"090A0B00\"}");
  EXPECT_TRUE(p.loadSerial(in));
  EXPECT_EQ(9, (int) p.extra_sf[0]);
  EXPECT_EQ(10, (int) p.extra_sf[1]);
  EXPECT_EQ(11, (int) p.extra_sf[2]);
  EXPECT_EQ(0, (int) p.extra_sf[3]);
}

TEST_F(NodePrefsSerialisation, RoundTripKeepsBothFields) {
  p.discovery_mod_timestamp = 1234567890;
  p.extra_sf[0] = 12; p.extra_sf[1] = 0;
  std::string text = save(p);

  NodePrefs loaded;
  StringInput in(text.c_str());
  EXPECT_TRUE(loaded.loadSerial(in));
  EXPECT_EQ(1234567890u, loaded.discovery_mod_timestamp);
  EXPECT_EQ(12, (int) loaded.extra_sf[0]);
  EXPECT_EQ(0, (int) loaded.extra_sf[1]);
}

// Upgrade: a file written by an older firmware has neither key. Loading it must
// leave both fields alone instead of zeroing them.
TEST_F(NodePrefsSerialisation, LoadWithoutTheNewKeysLeavesThemAlone) {
  p.discovery_mod_timestamp = 42;
  p.extra_sf[0] = 7;

  StringInput in("{name:\"node\",adv_int:1,radio:{sf:11,cr:5}}");
  EXPECT_TRUE(p.loadSerial(in));

  EXPECT_EQ(42u, p.discovery_mod_timestamp);
  EXPECT_EQ(7, (int) p.extra_sf[0]);
  EXPECT_EQ(11, (int) p.sf);   // the keys that are there still arrive
}

// Downgrade: a firmware that does not know the keys has to skip them, which is
// what the serializer does with any unknown key.
TEST_F(NodePrefsSerialisation, UnknownKeysAreSkipped) {
  StringInput in("{disc_mod:5,whats_this:9,extra_sf:\"0A000000\",radio:{nope:1,sf:10}}");
  EXPECT_TRUE(p.loadSerial(in));
  EXPECT_EQ(5u, p.discovery_mod_timestamp);
  EXPECT_EQ(10, (int) p.extra_sf[0]);
  EXPECT_EQ(10, (int) p.sf);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
