// Native unit test: controllerIndexForBus (#556).
//
// The MCP2515 driver numbers its chips 0 and 1; hybrid boards put their
// MCP2515 on a later bus (bus 1 on esp32s3_hybrid, bus 3 on teensy41_hybrid).
// The dispatcher hands the driver the chip's index, not the bus number. This
// layout mixes both shapes: a native controller on bus 0, an MCP2515 on
// bus 1, FlexCAN on bus 2 and a second MCP2515 on bus 3.
#include <unity.h>

#define CAN_BUS_0_TYPE CanControllerType::TWAI
#define CAN_BUS_1_TYPE CanControllerType::MCP2515
#define CAN_BUS_2_TYPE CanControllerType::FLEXCAN
#define CAN_BUS_3_TYPE CanControllerType::MCP2515

#include "../../src/hal/can_controller_types.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_first_mcp2515_is_chip_0_whatever_bus_it_is_on(void) {
    TEST_ASSERT_EQUAL_UINT8(0, controllerIndexForBus(1));
}

static void test_the_second_mcp2515_is_chip_1(void) {
    TEST_ASSERT_EQUAL_UINT8(1, controllerIndexForBus(3));
}

static void test_other_controllers_count_only_their_own_type(void) {
    TEST_ASSERT_EQUAL_UINT8(0, controllerIndexForBus(0));   // the only TWAI
    TEST_ASSERT_EQUAL_UINT8(0, controllerIndexForBus(2));   // the only FlexCAN
}

// constexpr, so a board.h layout can be checked at compile time too
static_assert(controllerIndexForBus(3) == 1, "usable in a constant expression");

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_the_first_mcp2515_is_chip_0_whatever_bus_it_is_on);
    RUN_TEST(test_the_second_mcp2515_is_chip_1);
    RUN_TEST(test_other_controllers_count_only_their_own_type);
    return UNITY_END();
}
