#include <gtest/gtest.h>

#include "target.h"

// The display and user button globals of a variant's target.cpp, pinned on the
// host: which constructor the display is built with, and the pin, polarity,
// pull resistor, long press time and click detection of user_btn. One
// environment per board description that occurs among the variants of cut E
// (TEST_DISPLAY_VARIANT): 1 heltec_v3 (display without argument), 2 heltec_t1
// (display on the board's peripheral power), 3 heltec_v4 (display(NULL)),
// 4 promicro (button with the pin's pull resistor). See
// docs/refactoring/0011-variant-target.md, "Cut E".

TestBoard board;

// The variant's block, copied verbatim from its target.cpp.
#if TEST_DISPLAY_VARIANT == 1
#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif
#elif TEST_DISPLAY_VARIANT == 2
#ifdef DISPLAY_CLASS
DISPLAY_CLASS display(&board.periph_power);
MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif
#elif TEST_DISPLAY_VARIANT == 3
#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display(NULL);
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif
#elif TEST_DISPLAY_VARIANT == 4
#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
#endif
#endif

// The same block once more, as the reference the cases are checked against on
// every commit, whatever defines the globals above.
namespace copy {
#if TEST_DISPLAY_VARIANT == 1
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#elif TEST_DISPLAY_VARIANT == 2
  DISPLAY_CLASS display(&board.periph_power);
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#elif TEST_DISPLAY_VARIANT == 3
  DISPLAY_CLASS display(NULL);
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#elif TEST_DISPLAY_VARIANT == 4
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
#endif
}  // namespace copy

namespace {

struct Globals {
  const char* name;
  TestDisplay* display;
  MomentaryButton* user_btn;
};

std::string name_of(const ::testing::TestParamInfo<Globals>& info) { return info.param.name; }

class DisplayGlobals : public ::testing::TestWithParam<Globals> {
protected:
  void SetUp() override {
    g_mock_pin_modes.clear();
    g_mock_pin_levels.clear();
    g_mock_millis = 10000;  // MomentaryButton takes a press at millis() == 0 for none
    btn().cancelClick();
    release();
    btn().check();
  }
  MomentaryButton& btn() { return *GetParam().user_btn; }
  void press() { g_mock_pin_levels[PIN_USER_BTN] = LOW; }
  void release() { g_mock_pin_levels[PIN_USER_BTN] = HIGH; }
};

TEST_P(DisplayGlobals, DisplayIsBuiltWithTheBoardsDescription) {
  const TestDisplay& d = *GetParam().display;
#if TEST_DISPLAY_VARIANT == 2
  EXPECT_FALSE(d.default_constructed);
  EXPECT_EQ(&board.periph_power, d.periph_power);
#elif TEST_DISPLAY_VARIANT == 3
  EXPECT_FALSE(d.default_constructed);
  EXPECT_EQ(nullptr, d.periph_power);
#else
  EXPECT_TRUE(d.default_constructed);
  EXPECT_EQ(nullptr, d.periph_power);
#endif
}

TEST_P(DisplayGlobals, ButtonIsOnTheUserButtonPin) {
  EXPECT_EQ(PIN_USER_BTN, btn().getPin());
}

TEST_P(DisplayGlobals, ButtonSetsUpThePinWithOrWithoutPullResistor) {
  btn().begin();
  ASSERT_EQ(1u, g_mock_pin_modes.size());
  EXPECT_EQ(PIN_USER_BTN, g_mock_pin_modes[0].first);
#if TEST_DISPLAY_VARIANT == 4
  EXPECT_EQ(INPUT_PULLUP, g_mock_pin_modes[0].second);
#else
  EXPECT_EQ(INPUT, g_mock_pin_modes[0].second);
#endif
}

TEST_P(DisplayGlobals, ButtonIsActiveLow) {
  press();
  EXPECT_TRUE(btn().isPressed());
  release();
  EXPECT_FALSE(btn().isPressed());
}

TEST_P(DisplayGlobals, LongPressAfterOneSecond) {
  press();
  EXPECT_EQ(BUTTON_EVENT_NONE, btn().check());
  g_mock_millis += 999;
  EXPECT_EQ(BUTTON_EVENT_NONE, btn().check());
  g_mock_millis += 1;
  EXPECT_EQ(BUTTON_EVENT_LONG_PRESS, btn().check());
}

TEST_P(DisplayGlobals, ClickWaitsForTheMultiClickWindow) {
  press();
  btn().check();
  g_mock_millis += 100;
  release();
  EXPECT_EQ(BUTTON_EVENT_NONE, btn().check());
  g_mock_millis += 279;
  EXPECT_EQ(BUTTON_EVENT_NONE, btn().check());
  g_mock_millis += 1;
  EXPECT_EQ(BUTTON_EVENT_CLICK, btn().check());
}

INSTANTIATE_TEST_SUITE_P(Variant, DisplayGlobals,
                         ::testing::Values(Globals{"globals", &display, &user_btn},
                                           Globals{"copy", &copy::display, &copy::user_btn}),
                         name_of);

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
