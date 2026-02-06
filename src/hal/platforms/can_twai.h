/*
 * can_twai.h - ESP32 TWAI (CAN) driver
 * Part of the preOBD Hardware Abstraction Layer
 * Note: External CAN transceiver required (MCP2551, TJA1050, SN65HVD230, etc.)
 *
 * ESP32 supports only a single CAN bus (bus 0).
 */

#ifndef HAL_CAN_TWAI_H
#define HAL_CAN_TWAI_H

#include <ESP32-TWAI-CAN.hpp>

namespace hal { namespace can {

#ifdef ENABLE_CAN_HYBRID
// In hybrid mode, wrap in twai namespace for dispatcher
namespace twai {
#endif

inline bool begin(uint32_t baudrate, uint8_t bus = 0, bool listenOnly = false) {
    // ESP32 only supports a single CAN bus
    if (bus != 0) return false;

    // Select pins based on ESP32 variant
    int8_t txPin, rxPin;
    #if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
        txPin = GPIO_NUM_20; rxPin = GPIO_NUM_21;
    #else
        txPin = GPIO_NUM_21; rxPin = GPIO_NUM_22;
    #endif
    ESP32Can.setPins(txPin, rxPin);

    // Convert baudrate to TWAI speed setting (library expects kbps)
    TwaiSpeed speed = ESP32Can.convertSpeed(baudrate / 1000);
    ESP32Can.setSpeed(speed);

    if (listenOnly) {
        // Pass custom general config with listen-only mode to begin()
        // No ACK bits, no error frames, no TX of any kind
        twai_general_config_t g_config = {
            .mode           = TWAI_MODE_LISTEN_ONLY,
            .tx_io          = (gpio_num_t)txPin,
            .rx_io          = (gpio_num_t)rxPin,
            .clkout_io      = TWAI_IO_UNUSED,
            .bus_off_io     = TWAI_IO_UNUSED,
            .tx_queue_len   = 0,        // No TX in listen-only
            .rx_queue_len   = 5,
            .alerts_enabled = TWAI_ALERT_NONE,
            .clkout_divider = 0,
            .intr_flags     = ESP_INTR_FLAG_LEVEL1
        };
        return ESP32Can.begin(speed, -1, -1, 0xFFFF, 0xFFFF,
                              nullptr, &g_config, nullptr);
    }

    return ESP32Can.begin();
}

inline bool write(uint32_t id, const uint8_t* data, uint8_t len, bool extended, uint8_t bus = 0) {
    // ESP32 only supports a single CAN bus
    if (bus != 0) return false;

    CanFrame frame;
    frame.identifier = id;
    frame.extd = extended ? 1 : 0;
    frame.data_length_code = len;
    memcpy(frame.data, data, len);
    return ESP32Can.writeFrame(frame);
}

inline bool read(uint32_t& id, uint8_t* data, uint8_t& len, bool& extended, uint8_t bus = 0) {
    // ESP32 only supports a single CAN bus
    if (bus != 0) return false;

    CanFrame frame;
    if (ESP32Can.readFrame(frame, 0)) {  // Non-blocking read
        id = frame.identifier;
        len = frame.data_length_code;
        extended = frame.extd;
        memcpy(data, frame.data, frame.data_length_code);
        return true;
    }
    return false;
}

inline void setFilters(uint32_t filter1, uint32_t filter2, uint8_t bus = 0) {
    // ESP32 only supports a single CAN bus
    if (bus != 0) return;

    // ESP32 TWAI filtering is more complex - software filtering recommended
    // Hardware acceptance filter can be configured via ESP32Can if needed
    (void)filter1;
    (void)filter2;
}

#ifdef ENABLE_CAN_HYBRID
} // namespace twai
#endif

}} // namespace hal::can

#endif // HAL_CAN_TWAI_H
