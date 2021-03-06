#include "maindefs.h"
#include <stdio.h>
#include "uart_thread.h"
#include "messages.h"
#include "debug.h"
#include "my_uart.h"
#include <plib/usart.h>
#include "my_i2c.h"


// This is a "logical" thread that processes messages from the UART
// It is not a "real" thread because there is only the single main thread
// of execution on the PIC because we are not using an RTOS.

int uart_lthread(uart_thread_struct *uptr, int msgtype, int length, unsigned char *msgbuffer) {

    if (msgtype == MSGT_UART_SEND) {
        DEBUG_ON(UART_DBG);
        DEBUG_OFF(UART_DBG);
        // Send Motor Command
        if (msgbuffer[0] == MOTOR_COMMAND) {
            // Send Motor Command to Master PIC.
            uart_retrieve_buffer(MOTORCMDLEN, msgbuffer);
        } else if (msgbuffer[0] == STOP) {
            // Send Stop Command to Master PIC.
            uart_retrieve_buffer(STOPLEN, msgbuffer);
        } else if (msgbuffer[0] == COMMAND_ACK || msgbuffer[0] == COMMAND_NACK) {
            // Send NACK or ACK command
            uart_retrieve_buffer(CMDACKLEN, msgbuffer);
        }
    } else if (msgtype == MSGT_UART_RCV) {
        DEBUG_ON(UART_DBG);
        DEBUG_OFF(UART_DBG);
        DEBUG_ON(UART_DBG);
        DEBUG_OFF(UART_DBG);
        // Pass sensor message to i2c.
        if (msgbuffer[0] == SENSOR_OUT_OF_RANGE) {
            pass_sensor_values_to_i2c(msgbuffer, length);
        } else if (msgbuffer[0] == MOTOR_ENCODER) {
            pass_motor_values_to_i2c(msgbuffer, length);
        }
    }
}