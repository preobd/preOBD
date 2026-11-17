/*
 * message_plane.h - Which of msg's two streams a message is on (#612 phase 2b-2)
 *
 * msg.control is the console (commands and their replies, CONSOLE TRANSPORT);
 * msg.debug is the log (LOG TRANSPORT, filtered by LOG LEVEL / LOG TAG).
 * Sensor data is the outputs' (#612), not a stream of msg.
 */
#ifndef MESSAGE_PLANE_H
#define MESSAGE_PLANE_H

enum MessagePlane {
    PLANE_CONTROL = 0,  // The console
    PLANE_DEBUG   = 1,  // The log
    NUM_PLANES    = 2
};

#endif // MESSAGE_PLANE_H
