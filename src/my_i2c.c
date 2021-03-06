#include "maindefs.h"
#ifndef __XC8
#include <i2c.h>
#include <timer0_thread.h>
#include <stdio.h>
#include<string.h>
#else
#include <plib/i2c.h>
#endif
#include "my_i2c.h"
#include "my_uart.h"
#include <string.h>

static i2c_comm *ic_ptr;
//  char c;
signed char length;
unsigned char msgtype;
unsigned char last_reg_recvd;
signed char status;
int sensor_buffer_status = 0;
int motor_buffer_status = 0;
//timer0_thread_struct t0thread_data; // info for timer0_lthread

//uart_comm uc;
//i2c_comm ic;
unsigned char msgbuffer[20];
//bufferFlag = 0x0;
//unsigned char i;
//uart_thread_struct uthread_data; // info for uart_lthread
//timer1_thread_struct t1thread_data; // info for timer1_lthread
//timer0_thread_struct t0thread_data; // info for timer0_lthread
// Configure for I2C Master mode -- the variable "slave_addr" should be stored in
//   i2c_comm (as pointed to by ic_ptr) for later use.

void start_i2c_slave_reply(unsigned char length, unsigned char *msg) {

    for (ic_ptr->outbuflen = 0; ic_ptr->outbuflen < length; ic_ptr->outbuflen++) {
        ic_ptr->outbuffer[ic_ptr->outbuflen] = msg[ic_ptr->outbuflen];
    }
    ic_ptr->outbuflen = length;
    ic_ptr->outbufind = 1; // point to the second byte to be sent

    // put the first byte into the I2C peripheral
    SSPBUF = ic_ptr->outbuffer[0];
    // we must be ready to go at this point, because we'll be releasing the I2C
    // peripheral which will soon trigger an interrupt
    SSPCON1bits.CKP = 1;

}

// an internal subroutine used in the slave version of the i2c_int_handler

void handle_start(unsigned char data_read) {
    ic_ptr->event_count = 1;
    ic_ptr->buflen = 0;
    // check to see if we also got the address
    if (data_read) {
        if (SSPSTATbits.D_A == 1) {
            // this is bad because we got data and
            // we wanted an address
            ic_ptr->status = I2C_IDLE;
            ic_ptr->error_count++;
            ic_ptr->error_code = I2C_ERR_NOADDR;
        } else {
            if (SSPSTATbits.R_W == 1) {
                ic_ptr->status = I2C_SLAVE_SEND;
            } else {
                ic_ptr->status = I2C_RCV_DATA;
            }
        }
    } else {
        ic_ptr->status = I2C_STARTED;
    }
}

// this is the interrupt handler for i2c -- it is currently built for slave mode
// -- to add master mode, you should determine (at the top of the interrupt handler)
//    which mode you are in and call the appropriate subroutine.  The existing code
//    below should be moved into its own "i2c_slave_handler()" routine and the new
//    master code should be in a subroutine called "i2c_master_handler()"

void i2c_int_handler() {
    unsigned char i2c_data;
    unsigned char data_read = 0;
    unsigned char data_written = 0;
    unsigned char msg_ready = 0;
    unsigned char msg_to_send = 0;
    unsigned char overrun_error = 0;
    unsigned char error_buf[3];

    // clear SSPOV
    if (SSPCON1bits.SSPOV == 1) {
        SSPCON1bits.SSPOV = 0;
        // we failed to read the buffer in time, so we know we
        // can't properly receive this message, just put us in the
        // a state where we are looking for a new message
        ic_ptr->status = I2C_IDLE;
        overrun_error = 1;
        ic_ptr->error_count++;
        ic_ptr->error_code = I2C_ERR_OVERRUN;
    }
    // read something if it is there
    if (SSPSTATbits.BF == 1) {
        i2c_data = SSPBUF;
        data_read = 1;
    }

    if (!overrun_error) {
        switch (ic_ptr->status) {
            case I2C_IDLE:
            {
                // ignore anything except a start
                if (SSPSTATbits.S == 1) {
                    handle_start(data_read);
                    // if we see a slave read, then we need to handle it here
                    if (ic_ptr->status == I2C_SLAVE_SEND) {
                        data_read = 0;
                        msg_to_send = 1;
                    }
                }
                break;
            }
            case I2C_STARTED:
            {
                // in this case, we expect either an address or a stop bit
                if (SSPSTATbits.P == 1) {
                    // we need to check to see if we also read an
                    // address (a message of length 0)
                    ic_ptr->event_count++;
                    if (data_read) {
                        if (SSPSTATbits.D_A == 0) {
                            msg_ready = 1;
                        } else {
                            ic_ptr->error_count++;
                            ic_ptr->error_code = I2C_ERR_NODATA;
                        }
                    }
                    ic_ptr->status = I2C_IDLE;
                } else if (data_read) {
                    ic_ptr->event_count++;
                    if (SSPSTATbits.D_A == 0) {
                        if (SSPSTATbits.R_W == 0) { // slave write
                            ic_ptr->status = I2C_RCV_DATA;
                        } else { // slave read
                            ic_ptr->status = I2C_SLAVE_SEND;
                            msg_to_send = 1;
                            // don't let the clock stretching bit be let go
                            data_read = 0;
                        }
                    } else {
                        ic_ptr->error_count++;
                        ic_ptr->status = I2C_IDLE;
                        ic_ptr->error_code = I2C_ERR_NODATA;
                    }
                }
                break;
            }
            case I2C_SLAVE_SEND:
            {
                if (ic_ptr->outbufind < ic_ptr->outbuflen) {
                    SSPBUF = ic_ptr->outbuffer[ic_ptr->outbufind];
                    ic_ptr->outbufind++;
                    data_written = 1;
                } else {
                    // we have nothing left to send
                    ic_ptr->status = I2C_IDLE;
                }
                break;
            }
            case I2C_RCV_DATA:
            {
                // we expect either data or a stop bit or a (if a restart, an addr)
                if (SSPSTATbits.P == 1) {
                    // we need to check to see if we also read data
                    ic_ptr->event_count++;
                    if (data_read) {
                        if (SSPSTATbits.D_A == 1) {
                            ic_ptr->buffer[ic_ptr->buflen] = i2c_data;
                            ic_ptr->buflen++;
                            msg_ready = 1;
                        } else {
                            ic_ptr->error_count++;
                            ic_ptr->error_code = I2C_ERR_NODATA;
                            ic_ptr->status = I2C_IDLE;
                        }
                    } else {
                        msg_ready = 1;
                    }
                    ic_ptr->status = I2C_IDLE;
                } else if (data_read) {
                    ic_ptr->event_count++;
                    if (SSPSTATbits.D_A == 1) {
                        ic_ptr->buffer[ic_ptr->buflen] = i2c_data;
                        ic_ptr->buflen++;
                    } else /* a restart */ {
                        if (SSPSTATbits.R_W == 1) {
                            ic_ptr->status = I2C_SLAVE_SEND;
                            msg_ready = 1;
                            msg_to_send = 1;
                            // don't let the clock stretching bit be let go
                            data_read = 0;
                        } else { /* bad to recv an address again, we aren't ready */
                            ic_ptr->error_count++;
                            ic_ptr->error_code = I2C_ERR_NODATA;
                            ic_ptr->status = I2C_IDLE;
                        }
                    }
                }
                break;
            }
        }
    }

    // release the clock stretching bit (if we should)
    if (data_read || data_written) {
        // release the clock
        if (SSPCON1bits.CKP == 0) {
            SSPCON1bits.CKP = 1;
        }
    }

    // must check if the message is too long, if
    if ((ic_ptr->buflen > MAXI2CBUF - 2) && (!msg_ready)) {
        ic_ptr->status = I2C_IDLE;
        ic_ptr->error_count++;
        ic_ptr->error_code = I2C_ERR_MSGTOOLONG;
    }

    if (msg_ready) {
        ic_ptr->buffer[ic_ptr->buflen] = ic_ptr->event_count;
        // Send Motor command to the Master PIC
        ToMainHigh_sendmsg(ic_ptr->buflen + 1, MSGT_UART_SEND, (void *) ic_ptr->buffer);
        ic_ptr->buflen = 0;
    } else if (ic_ptr->error_count >= I2C_ERR_THRESHOLD) {
        error_buf[0] = ic_ptr->error_count;
        error_buf[1] = ic_ptr->error_code;
        error_buf[2] = ic_ptr->event_count;
        ic_ptr->error_count = 0;
    }
    if (msg_to_send) {
        // send to the queue to *ask* for the data to be sent out
        readMessages();
    }
}


// set up the data structures for this i2c code
// should be called once before any i2c routines are called

void init_i2c(i2c_comm * ic) {
    ic_ptr = ic;
    ic_ptr->buflen = 0;
    ic_ptr->event_count = 0;
    ic_ptr->status = I2C_IDLE;
    ic_ptr->error_count = 0;

    // Intialize CMD ACK response.
    ic_ptr->cmd_ack_buf[0] = 0x10;
    ic_ptr->cmd_ack_buf[1] = 0x01;
    ic_ptr->cmd_ack_buf[2] = 0x02;
    ic_ptr->cmd_ack_buf[3] = 0x02;

    // Intialize CMD ACK response.
    ic_ptr->cmd_nack_buf[0] = 0x11;
    ic_ptr->cmd_nack_buf[1] = 0x01;
    ic_ptr->cmd_nack_buf[2] = 0x02;
    ic_ptr->cmd_nack_buf[3] = 0x02;

    // Initialize No Encoder response
    ic_ptr->no_encoder_buffer[0] = 0x09;
    ic_ptr->no_encoder_buffer[1] = 0x00;
    ic_ptr->no_encoder_buffer[2] = 0x00;
    ic_ptr->no_encoder_buffer[3] = 0x00;
    ic_ptr->no_encoder_buffer[4] = 0x00;

    // Initialize No Sensor Response.
    ic_ptr->no_sensor_buffer[0] = 0x02;
    ic_ptr->no_sensor_buffer[1] = 0x00;
    ic_ptr->no_sensor_buffer[2] = 0x00;
    ic_ptr->no_sensor_buffer[3] = 0x00;
    ic_ptr->no_sensor_buffer[4] = 0x00;
    ic_ptr->no_sensor_buffer[5] = 0x00;
}

// setup the PIC to operate as a slave
// the address must include the R/W bit

void i2c_configure_slave(unsigned char addr) {

    // ensure the two lines are set for input (we are a slave)
#ifdef __USE18F26J50
    //THIS CODE LOOKS WRONG, SHOULDN'T IT BE USING THE TRIS BITS???
    PORTBbits.SCL1 = 1;
    PORTBbits.SDA1 = 1;
#else
#ifdef __USE18F46J50
    TRISBbits.TRISB4 = 1; //RB4 = SCL1
    TRISBbits.TRISB5 = 1; //RB5 = SDA1
#else
    TRISCbits.TRISC3 = 1;
    TRISCbits.TRISC4 = 1;
#endif
#endif

    // set the address
    SSPADD = addr;
    //OpenI2C(SLAVE_7,SLEW_OFF); // replaced w/ code below
    SSPSTAT = 0x0;
    SSPCON1 = 0x0;
    SSPCON2 = 0x0;
    SSPCON1 |= 0x0E; // enable Slave 7-bit w/ start/stop interrupts
    SSPSTAT |= SLEW_OFF;

#ifdef I2C_V3
    I2C1_SCL = 1;
    I2C1_SDA = 1;
#else
#ifdef I2C_V1
    I2C_SCL = 1;
    I2C_SDA = 1;
#else
#ifdef __USE18F26J50
    PORTBbits.SCL1 = 1;
    PORTBbits.SDA1 = 1;
#else
#ifdef __USE18F46J50
    PORTBbits.SCL1 = 1;
    PORTBbits.SDA1 = 1;
#else
    __dummyXY = 35; // Something is messed up with the #ifdefs; this line is designed to invoke a compiler error
#endif
#endif
#endif
#endif

    // enable clock-stretching
    SSPCON2bits.SEN = 1;
    SSPCON1 |= SSPENB;
    // end of i2c configure
}

void pass_sensor_values_to_i2c(unsigned char* msgbuffer, unsigned char length) {
    int i;
    for (i = 0; i < length; i++) {
        ic_ptr->sensor_buffer[i] = msgbuffer[i];
    }
    validSensorFlag = 1;
}

void pass_motor_values_to_i2c(unsigned char* msgbuffer, unsigned char length) {
    int i;
    for (i = 0; i < length; i++) {
        ic_ptr->motor_buffer[i] = msgbuffer[i];
    }
    validMotorFlag = 1;
}

void readMessages() {

    switch (ic_ptr->buffer[0]) {
        case MOTOR_COMMAND:
        {
            // Waiting for Master to ACK message
            start_i2c_slave_reply(CMDNACKLEN, ic_ptr->cmd_nack_buf);
            break;
        }
        case ARM_POLL:
        {
            if (motorCommandSent) {
                motorCommandSent = 0;
                // Motorcontroller Move Command was succesfully sent to Master PIC.
                start_i2c_slave_reply(CMDACKLEN, ic_ptr->cmd_ack_buf);
            } else {
                // Waiting for Master to ACK message
                start_i2c_slave_reply(CMDNACKLEN, ic_ptr->cmd_nack_buf);
            }
            break;
        }
        case STOP:
        {
            // Motorcontroller Stop Command respond with Command ACK
            start_i2c_slave_reply(CMDACKLEN, ic_ptr->cmd_ack_buf);
            break;
        }
        case ENCODER_REQUEST:
        {
            // Motor Encoder Request
            if (validMotorFlag) {
                validMotorFlag = 0;
                start_i2c_slave_reply(MOTORLEN, ic_ptr->motor_buffer);
            } else {
                start_i2c_slave_reply(MOTORLEN, ic_ptr->no_encoder_buffer);
            }
            break;
        }
        case SENSOR_REQUEST:
        {
            if (validSensorFlag) {
                // ARM Polling for sensor data to determine if sensors are out of ranges
                validSensorFlag = 0;
                start_i2c_slave_reply(SENSORLEN, ic_ptr->sensor_buffer);
            } else {
                start_i2c_slave_reply(SENSORLEN, ic_ptr->no_sensor_buffer);
            }
        }
    };
}