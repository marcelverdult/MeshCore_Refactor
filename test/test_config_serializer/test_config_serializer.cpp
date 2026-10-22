#include <gtest/gtest.h>
#include "helpers/ConfigSerializer.h"
#include "helpers/DynamicConfigSerializer.h"

#define TEST_INT_S  "56"
#define TEST_INT     56
#define TEST_FLOAT_S  "-6.123"
#define TEST_FLOAT     -6.1230f
#define TEST_DOUBLE_S "12.123456"
#define TEST_DOUBLE    12.123456

class MockInputStream : public Stream {
    const char* _text;
    int pos, len;
public:
    MockInputStream(const char* text) : _text(text) { pos = 0; len = strlen(text); }
    int available() override { return len - pos; }
    int read() override { if (pos < len) { return _text[pos++]; } return -1; }
    int peek() override { if (pos < len) { return _text[pos]; } return -1; }
};

class MockPrintStream : public Stream {
    int len = 0;
    uint8_t _buf[1024];

    size_t printSigned(long long value) {
        char text[24];
        snprintf(text, sizeof(text), "%lld", value);
        return Print::print(text);
    }

    size_t printUnsigned(unsigned long long value) {
        char text[24];
        snprintf(text, sizeof(text), "%llu", value);
        return Print::print(text);
    }

public:
    size_t write(uint8_t b) override {
        if (len < sizeof(_buf)) {
            _buf[len++] = b;
            return 1;
        }
        return 0;
    }

    size_t print(unsigned char v, int r) override { return printUnsigned(v); }
    size_t print(int v, int r) override { return printSigned(v); }
    size_t print(unsigned int v, int r) override { return printUnsigned(v); }
    size_t print(long v, int r) override { return printSigned(v); }
    size_t print(unsigned long v, int r) override { return printUnsigned(v); }
    size_t print(long long v, int r) override { return printSigned(v); }
    size_t print(unsigned long long v, int r) override { return printUnsigned(v); }
    size_t print(double v, int p = 2) override {
        char text[32];
        snprintf(text, sizeof(text), "%.*f", p, v);
        return Print::print(text);
    }

    int getLength() const { return len; }
    const uint8_t* getBytes() const { return _buf; }
};

class TestStruct : public ConfigSerializer {
  protected:
    void structure() override {
        def("age", age);
        def("flags", flags);
        def("name", name, sizeof(name));
    }
  public:
    int32_t age;
    char    name[16];
    uint8_t flags;
};

class TestNumbers : public ConfigSerializer {
  protected:
    void structure() override {
        def("ratio", ratio);
        def("lat", lat);
    }
  public:
    float  ratio = 1.5f;
    double lat   = 2.5;
};

// ── saveSerial: basic ───────────────────────────────────────────────────────

TEST(ConfigSerializer, SaveSerial_Basic) {
    MockPrintStream s;
    TestStruct data;

    data.age = TEST_INT;
    data.flags = TEST_INT;
    strcpy(data.name, "Scott");

    bool success = data.saveSerial(s);
    EXPECT_TRUE(success);

    auto l = s.getLength();
    const char* expect = "{age:" TEST_INT_S ",flags:" TEST_INT_S ",name:\"Scott\"}";
    EXPECT_EQ(strlen(expect), l);

    bool match = memcmp(s.getBytes(), expect, l) == 0;
    EXPECT_TRUE(match);
}


TEST(ConfigSerializer, SaveSerial_EscChars) {
    MockPrintStream s;
    TestStruct data;

    data.age = TEST_INT;
    data.flags = TEST_INT;
    strcpy(data.name, "\"Scott\"\n");

    bool success = data.saveSerial(s);
    EXPECT_TRUE(success);

    auto l = s.getLength();
    const char* expect = "{age:" TEST_INT_S ",flags:" TEST_INT_S ",name:\"\\\"Scott\\\"\\n\"}";
    EXPECT_EQ(strlen(expect), l);

    bool match = memcmp(s.getBytes(), expect, l) == 0;
    EXPECT_TRUE(match);
}

// ── loadSerial: basic ───────────────────────────────────────────────────────

TEST(ConfigSerializer, LoadSerial_Basic) {
    MockInputStream s("{age:" TEST_INT_S ",flags:" TEST_INT_S ",name:\"Scott\"}");
    TestStruct data;

    bool success = data.loadSerial(s);
    EXPECT_TRUE(success);

    EXPECT_EQ(TEST_INT, data.age);
    EXPECT_EQ(TEST_INT, data.flags);
    bool match = strcmp("Scott", data.name) == 0;
    EXPECT_TRUE(match);
}

TEST(ConfigSerializer, LoadSerial_HandleWhitespace) {
    MockInputStream s("  { age:  " TEST_INT_S " ,  flags:  " TEST_INT_S " ,  name:  \"Scott\" }  ");
    TestStruct data;

    bool success = data.loadSerial(s);
    EXPECT_TRUE(success);

    EXPECT_EQ(TEST_INT, data.age);
    EXPECT_EQ(TEST_INT, data.flags);
    bool match = strcmp("Scott", data.name) == 0;
    EXPECT_TRUE(match);
}

TEST(ConfigSerializer, LoadSerial_EscChars) {
    MockInputStream s("{age:" TEST_INT_S ",flags:" TEST_INT_S ",name:\"\\\"Scott\\\"\\n\"}");
    TestStruct data;

    bool success = data.loadSerial(s);
    EXPECT_TRUE(success);

    bool match = strcmp("\"Scott\"\n", data.name) == 0;
    EXPECT_TRUE(match);
}

TEST(ConfigSerializer, LoadSerial_UnmatchedBraces) {
    MockInputStream s("{age:" TEST_INT_S ",flags:" TEST_INT_S ",name:\"Scott\"");
    TestStruct data;

    bool success = data.loadSerial(s);
    EXPECT_FALSE(success);
}

TEST(ConfigSerializer, LoadSerial_MissingCommas) {
    MockInputStream s("{age:" TEST_INT_S " flags:" TEST_INT_S " name:\"Scott\"}");
    TestStruct data;

    bool success = data.loadSerial(s);
    EXPECT_FALSE(success);
}

TEST(ConfigSerializer, LoadSerial_IgnoreUnknowns) {
    MockInputStream s("{age:" TEST_INT_S ",xxx:" TEST_INT_S ",name:\"Scott\"}");
    TestStruct data;
    data.flags = 1;

    // should ignore the 'xxx' property
    bool success = data.loadSerial(s);
    EXPECT_TRUE(success);

    EXPECT_EQ(TEST_INT, data.age);
    EXPECT_EQ(1, data.flags);   // flags should be unmodified
    bool match = strcmp("Scott", data.name) == 0;
    EXPECT_TRUE(match);
}

class TestNested : public ConfigSerializer {
    class Inner : public ConfigSerializer {
      protected:
        void structure() override { }   // no properties, so it writes as '{}'
    };
    Inner inner;
  protected:
    void structure() override {
        def("age", age);
        def("inner", inner);
        def("name", name, sizeof(name));   // comes *after* the empty sub-object
    }
  public:
    int32_t age;
    char    name[16];
};

TEST(ConfigSerializer, LoadSerial_EmptyObject) {
    MockInputStream s("{age:" TEST_INT_S ",inner:{},name:\"Scott\"}");
    TestNested data;
    data.name[0] = 0;

    bool success = data.loadSerial(s);
    EXPECT_TRUE(success);

    EXPECT_EQ(TEST_INT, data.age);
    bool match = strcmp("Scott", data.name) == 0;
    EXPECT_TRUE(match);   // properties after an empty object must still load
}

TEST(ConfigSerializer, LoadSerial_EmptyObjectWithWhitespace) {
    MockInputStream s("{age:" TEST_INT_S ",inner:{  },name:\"Scott\"}");
    TestNested data;
    data.name[0] = 0;

    bool success = data.loadSerial(s);
    EXPECT_TRUE(success);
    bool match = strcmp("Scott", data.name) == 0;
    EXPECT_TRUE(match);
}

TEST(DynamicConfigSerializer, GetSet_Basic) {
    DynamicConfigSerializer data;

    bool s1 = data.setByKey("age", "11");
    bool s2 = data.setByKey("name", "Scott");
    EXPECT_TRUE(s1 && s2);

    char tmp[32];
    bool g1 = data.getByKey("age", tmp, 31);
    EXPECT_TRUE(g1);
    EXPECT_STREQ("11", tmp);

    bool g2 = data.getByKey("name", tmp, 31);
    EXPECT_TRUE(g2);
    EXPECT_STREQ("Scott", tmp);
}

TEST(DynamicConfigSerializer, Set_Replaces) {
    DynamicConfigSerializer data;

    bool s1 = data.setByKey("age", "11");
    bool s2 = data.setByKey("name", "Scott");
    EXPECT_TRUE(s1 && s2);

    bool s3 = data.setByKey("age", "333");
    EXPECT_TRUE(s3);

    char tmp[32];
    bool g1 = data.getByKey("age", tmp, 31);
    EXPECT_TRUE(g1);
    EXPECT_STREQ("333", tmp);

    bool g2 = data.getByKey("name", tmp, 31);
    EXPECT_TRUE(g2);
    EXPECT_STREQ("Scott", tmp);
}

TEST(DynamicConfigSerializer, GetUnknown_Fail) {
    DynamicConfigSerializer data;

    bool s1 = data.setByKey("age", "11");
    EXPECT_TRUE(s1);

    char tmp[32];
    bool g2 = data.getByKey("name", tmp, 31);
    EXPECT_FALSE(g2);
}

TEST(DynamicConfigSerializer, SaveCustom_Basic) {
    MockPrintStream s;
    DynamicConfigSerializer data;

    bool s1 = data.setByKey("age", "11");
    bool s2 = data.setByKey("name", "Scott");
    EXPECT_TRUE(s1 && s2);

    bool success = data.saveSerial(s);
    EXPECT_TRUE(success);

    auto l = s.getLength();
    char tmp[128];
    memcpy(tmp, s.getBytes(), l);
    tmp[l] = 0;

    const char* expect = "{age:\"11\",name:\"Scott\"}";
    EXPECT_STREQ(expect, tmp);
}

TEST(DynamicConfigSerializer, LoadCustom_Basic) {
    MockInputStream s("{age:\"" TEST_INT_S "\",name:\"Scott\"}");
    DynamicConfigSerializer data;

    bool success = data.loadSerial(s);
    EXPECT_TRUE(success);

    char tmp[32];
    bool g1 = data.getByKey("age", tmp, 31);
    EXPECT_TRUE(g1);
    EXPECT_STREQ(TEST_INT_S, tmp);

    bool g2 = data.getByKey("name", tmp, 31);
    EXPECT_TRUE(g2);
    EXPECT_STREQ("Scott", tmp);
}

// ── main ───────────────────────────────────────────────────────

// ── loadSerial: numbers that are not finite ────────────────────────────────

// The values a node runs on are set up before /prefs.json is read, so a token
// that is not a finite number has to leave the value that is already there.
// Clamping it into range is no help: there is no sensible nearest value for a
// NaN, and for a frequency the nearest bound would put the radio on 150 MHz.

TEST(ConfigSerializer, LoadSerial_KeepsDefaultOnNotANumber) {
    MockInputStream s("{ratio:nan,lat:nan}");

    TestNumbers data;
    EXPECT_TRUE(data.loadSerial(s));

    EXPECT_FLOAT_EQ(1.5f, data.ratio);
    EXPECT_DOUBLE_EQ(2.5, data.lat);
}

TEST(ConfigSerializer, LoadSerial_KeepsDefaultOnInfinity) {
    MockInputStream s("{ratio:inf,lat:-inf}");

    TestNumbers data;
    EXPECT_TRUE(data.loadSerial(s));

    EXPECT_FLOAT_EQ(1.5f, data.ratio);
    EXPECT_DOUBLE_EQ(2.5, data.lat);
}

// 1e40 is finite as a double and overflows to infinity as a float, so the
// check has to look at the value that is about to be stored.
TEST(ConfigSerializer, LoadSerial_KeepsDefaultWhenFloatOverflows) {
    MockInputStream s("{ratio:1e40}");

    TestNumbers data;
    EXPECT_TRUE(data.loadSerial(s));

    EXPECT_FLOAT_EQ(1.5f, data.ratio);
}

TEST(ConfigSerializer, LoadSerial_AcceptsFiniteNumbers) {
    MockInputStream s("{ratio:-6.125,lat:12.5}");

    TestNumbers data;
    EXPECT_TRUE(data.loadSerial(s));

    EXPECT_FLOAT_EQ(-6.125f, data.ratio);
    EXPECT_DOUBLE_EQ(12.5, data.lat);
}

// Garbage parses as zero and is stored, which is what the old code did.
TEST(ConfigSerializer, LoadSerial_StoresZeroForGarbage) {
    MockInputStream s("{ratio:abc}");

    TestNumbers data;
    EXPECT_TRUE(data.loadSerial(s));

    EXPECT_FLOAT_EQ(0.0f, data.ratio);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
