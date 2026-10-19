// Tests for the text length that BaseChatMesh::sendGroupMessage() accepts.
//
// The companion firmware derives that length by subtracting a fixed header size
// from the length of the frame the app sent, so a frame that is too short turns
// the length negative. sendGroupMessage() hands the value to memcpy(), which
// reads it as a size_t, so a negative length asks for about 4 GB. The sibling
// sendGroupData(), added later for CMD_SEND_CHANNEL_DATA, already refuses a
// negative length; these tests pin the same behaviour for sendGroupMessage()
// and keep the lengths that must still work.
//
// They run the real BaseChatMesh.cpp, Mesh.cpp, Dispatcher.cpp and Packet.cpp
// against fakes for the radio, clock, RNG and packet manager, and against the
// link-time identity stubs in identity_stubs.cpp.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include <helpers/BaseChatMesh.h>
#include <helpers/SimpleMeshTables.h>

namespace {

class FakeRadio : public mesh::Radio {
public:
  int recvRaw(uint8_t* bytes, int sz) override { return 0; }
  uint32_t getEstAirtimeFor(int len_bytes) override { return 10; }
  float packetScore(float snr, int packet_len) override { return 0; }
  bool startSendRaw(const uint8_t* bytes, int len) override { return false; }
  bool isSendComplete() override { return true; }
  void onSendFinished() override { }
  bool isInRecvMode() const override { return true; }
};

class FakeClock : public mesh::MillisecondClock {
public:
  unsigned long getMillis() override { return 0; }
};

class FakeRTC : public mesh::RTCClock {
public:
  uint32_t getCurrentTime() override { return 0; }
  void setCurrentTime(uint32_t time) override { }
};

class ZeroRNG : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t sz) override { memset(dest, 0, sz); }
};

// Hands out packets from a small pool and keeps every packet that was queued for
// sending, so a test can tell "refused before sending" from "sent" and can read
// the payload length the chat layer produced.
class CapturingPacketManager : public mesh::PacketManager {
  static const int POOL = 4;
  mesh::Packet _pool[POOL];
  bool _in_use[POOL] = { false, false, false, false };
public:
  std::vector<mesh::Packet*> queued;

  mesh::Packet* allocNew() override {
    for (int i = 0; i < POOL; i++) {
      if (!_in_use[i]) {
        _in_use[i] = true;
        return &_pool[i];
      }
    }
    return nullptr;
  }
  void free(mesh::Packet* packet) override {
    for (int i = 0; i < POOL; i++) {
      if (&_pool[i] == packet) _in_use[i] = false;
    }
  }
  void queueOutbound(mesh::Packet* packet, uint8_t priority, uint32_t scheduled_for) override {
    queued.push_back(packet);
  }
  mesh::Packet* getNextOutbound(uint32_t now) override { return nullptr; }
  int getOutboundCount(uint32_t now) const override { return (int)queued.size(); }
  int getOutboundTotal() const override { return (int)queued.size(); }
  int getFreeCount() const override { return POOL; }
  mesh::Packet* getOutboundByIdx(int i) override { return nullptr; }
  mesh::Packet* removeOutboundByIdx(int i) override { return nullptr; }
  void queueInbound(mesh::Packet* packet, uint32_t scheduled_for) override { }
  mesh::Packet* getNextInbound(uint32_t now) override { return nullptr; }
};

// The chat layer is abstract; the callbacks are not reached by these tests.
class TestChatMesh : public BaseChatMesh {
public:
  TestChatMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc,
               mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : BaseChatMesh(radio, ms, rng, rtc, mgr, tables) { }

  void onDiscoveredContact(ContactInfo& c, bool is_new, uint8_t path_len, const uint8_t* path) override { }
  ContactInfo* processAck(const uint8_t* data) override { return nullptr; }
  void onContactPathUpdated(const ContactInfo& c) override { }
  void onMessageRecv(const ContactInfo& c, mesh::Packet* p, uint32_t ts, const char* text) override { }
  void onCommandDataRecv(const ContactInfo& c, mesh::Packet* p, uint32_t ts, const char* text) override { }
  void onCLICommandRecv(const ContactInfo& c, mesh::Packet* p, uint32_t ts, const char* text, char* reply) override { }
  void onSignedMessageRecv(const ContactInfo& c, mesh::Packet* p, uint32_t ts, const uint8_t* prefix,
                           const char* text) override { }
  uint32_t calcFloodTimeoutMillisFor(uint32_t airtime) const override { return 0; }
  uint32_t calcDirectTimeoutMillisFor(uint32_t airtime, uint8_t path_len) const override { return 0; }
  void onSendTimeout() override { }
  void onChannelMessageRecv(const mesh::GroupChannel& ch, mesh::Packet* p, uint32_t ts, const char* text) override { }
  uint8_t onContactRequest(const ContactInfo& c, uint32_t ts, const uint8_t* data, uint8_t len,
                           uint8_t* reply) override { return 0; }
  void onContactResponse(const ContactInfo& c, const uint8_t* data, uint8_t len) override { }
};

const char* SENDER = "node";          // the prefix becomes "node: ", so 6 bytes
const int PREFIX_LEN = 6;
const int HEADER_LEN = 5;             // 4 byte timestamp plus the TXT_TYPE byte

// A group datagram carries the channel hash, the MAC and the ciphertext, and the
// cipher pads to whole blocks. That makes the payload length a function of the
// text length alone, which is what these tests observe.
int expectedPayloadLen(int text_len) {
  int plain = HEADER_LEN + PREFIX_LEN + text_len;
  int blocks = (plain + CIPHER_BLOCK_SIZE - 1) / CIPHER_BLOCK_SIZE;
  return PATH_HASH_SIZE + CIPHER_MAC_SIZE + blocks * CIPHER_BLOCK_SIZE;
}

class GroupMessageTest : public ::testing::Test {
protected:
  FakeRadio radio;
  FakeClock ms;
  ZeroRNG rng;
  FakeRTC rtc;
  SimpleMeshTables tables;
  CapturingPacketManager mgr;
  TestChatMesh mesh_node { radio, ms, rng, rtc, mgr, tables };
  mesh::GroupChannel channel;
  // Long enough that a clamped length still reads inside this buffer.
  char text[256];

  void SetUp() override {
    memset(&channel, 0, sizeof(channel));
    memset(text, 'x', sizeof(text));
    text[sizeof(text) - 1] = 0;
  }
};

// A frame of 3 bytes makes the companion compute 3 - 7 = -4 (MyMesh.cpp,
// CMD_SEND_CHANNEL_TXT_MSG). Measured on a T-Echo running dev 428a95c6: the node
// stopped answering and never came back.
TEST_F(GroupMessageTest, RefusesTheLengthAThreeByteFrameProduces) {
  EXPECT_FALSE(mesh_node.sendGroupMessage(0, channel, SENDER, text, -4));
  EXPECT_TRUE(mgr.queued.empty()) << "nothing may be sent for a negative text length";
}

TEST_F(GroupMessageTest, RefusesMinusOne) {
  EXPECT_FALSE(mesh_node.sendGroupMessage(0, channel, SENDER, text, -1));
  EXPECT_TRUE(mgr.queued.empty());
}

TEST_F(GroupMessageTest, SendsEmptyText) {
  EXPECT_TRUE(mesh_node.sendGroupMessage(0, channel, SENDER, text, 0));
  ASSERT_EQ(1u, mgr.queued.size());
  EXPECT_EQ(expectedPayloadLen(0), mgr.queued[0]->payload_len);
}

TEST_F(GroupMessageTest, SendsShortText) {
  EXPECT_TRUE(mesh_node.sendGroupMessage(0, channel, SENDER, text, 5));
  ASSERT_EQ(1u, mgr.queued.size());
  EXPECT_EQ(expectedPayloadLen(5), mgr.queued[0]->payload_len);
}

// One byte past a cipher block, to catch an off-by-one in the length arithmetic.
TEST_F(GroupMessageTest, SendsTextAcrossABlockBoundary) {
  EXPECT_TRUE(mesh_node.sendGroupMessage(0, channel, SENDER, text, 6));
  ASSERT_EQ(1u, mgr.queued.size());
  EXPECT_EQ(expectedPayloadLen(6), mgr.queued[0]->payload_len);
}

// Too long is clamped, not refused: that is the behaviour the app relies on.
TEST_F(GroupMessageTest, ClampsTextThatIsTooLong) {
  EXPECT_TRUE(mesh_node.sendGroupMessage(0, channel, SENDER, text, 200));
  ASSERT_EQ(1u, mgr.queued.size());
  EXPECT_EQ(expectedPayloadLen(MAX_TEXT_LEN - PREFIX_LEN), mgr.queued[0]->payload_len);
}

// The exact boundary of the clamp still goes out unchanged.
TEST_F(GroupMessageTest, SendsTheLongestTextThatFits) {
  const int longest = MAX_TEXT_LEN - PREFIX_LEN;
  EXPECT_TRUE(mesh_node.sendGroupMessage(0, channel, SENDER, text, longest));
  ASSERT_EQ(1u, mgr.queued.size());
  EXPECT_EQ(expectedPayloadLen(longest), mgr.queued[0]->payload_len);
}

// Characterisation of the sibling this fix follows, so the symmetry stays visible.
TEST_F(GroupMessageTest, GroupDataAlreadyRefusesNegativeLength) {
  uint8_t path[1] = { 0 };
  uint8_t data[4] = { 1, 2, 3, 4 };
  EXPECT_FALSE(mesh_node.sendGroupData(channel, path, OUT_PATH_UNKNOWN, 0x0102, data, -1));
  EXPECT_TRUE(mgr.queued.empty());
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
