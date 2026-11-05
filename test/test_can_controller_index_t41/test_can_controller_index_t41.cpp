// Native unit test: controllerIndexForBus (#556) on teensy41_hybrid's layout
// (boards/teensy41_hybrid/board.h): FlexCAN on buses 0-2, the one MCP2515 on
// bus 3, wired as CAN_CS_0. One layout per test program, since the layout
// is a set of macros.
#include <unity.h>

#define CAN_BUS_0_TYPE CanControllerType::FLEXCAN
#define CAN_BUS_1_TYPE CanControllerType::FLEXCAN
#define CAN_BUS_2_TYPE CanControllerType::FLEXCAN
#define CAN_BUS_3_TYPE CanControllerType::MCP2515

#include "../../src/hal/can_controller_types.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_mcp2515_on_bus_3_is_chip_0(void) {
    TEST_ASSERT_EQUAL_UINT8(0, controllerIndexForBus(3));
}

static void test_flexcan_buses_keep_their_bus_number(void) {
    for (uint8_t bus = 0; bus < 3; bus++) {
        TEST_ASSERT_EQUAL_UINT8(bus, controllerIndexForBus(bus));
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_the_mcp2515_on_bus_3_is_chip_0);
    RUN_TEST(test_flexcan_buses_keep_their_bus_number);
    return UNITY_END();
}
