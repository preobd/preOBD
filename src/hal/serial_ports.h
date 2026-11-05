/*
 * serial_ports.h - preOBD serial port number -> the core's UART object
 *
 * preOBD numbers its hardware ports Serial1..SerialN, and on Teensy, ESP32
 * and AVR port n is the core's SerialN. The STM32 core names each object by
 * its USART instead, so the Feather F405's two broken-out UARTs (USART3 and
 * USART6) become ports 1 and 2 here. Everything that opens a hardware port
 * goes through HW_SERIAL<n>, never a SerialN name.
 */

#ifndef HAL_SERIAL_PORTS_H
#define HAL_SERIAL_PORTS_H

#if defined(ARDUINO_FEATHER_F405)
    #define HW_SERIAL1 Serial3   // USART3, D0/D1 (enabled by the variant's SERIAL_UART_INSTANCE)
    #define HW_SERIAL2 Serial6   // USART6, D5/D6 (ENABLE_HWSERIAL6 in board.ini)
#else
    #define HW_SERIAL1 Serial1
    #define HW_SERIAL2 Serial2
    #define HW_SERIAL3 Serial3
    #define HW_SERIAL4 Serial4
    #define HW_SERIAL5 Serial5
    #define HW_SERIAL6 Serial6
    #define HW_SERIAL7 Serial7
    #define HW_SERIAL8 Serial8
#endif

#endif // HAL_SERIAL_PORTS_H
