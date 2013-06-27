/* Copyright (C) 2009 Stig Hornang
 
   This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or (at
   your option) any later version.
 
   This library is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.
 
   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307,
   USA. */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>
#include <lockdev.h>


#include "modbus.h"

#define MODBUS_MSG_QUEUE 1

unsigned char buffer[255];
unsigned char datagram[255];

int buffer_length = 0;
int datagram_length = 0;
int msgqueue;
int datagram_spacing_usec = 11000;
unsigned char modbus_slave_id = 0;
int mb_datagram_status = 0;
int mb_waiting_for_reply = 0;
int fd;

pthread_mutex_t waiting_for_reply;
pthread_cond_t datagram_start_receiving;
pthread_cond_t datagram_finished_receiving;

int crc_error_count = 0;
int timeout_error_count = 0;

struct timespec mb_last_received_data;

struct msgbuf {
    long mtype; 
    char mtext[1]; 
};

struct msgbuf ipc_message;
int receiving;


/* Table of CRC values for highorder byte */
static unsigned char auchCRCHi[] = { 
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
    0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
    0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81,
    0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
    0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
    0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
    0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
    0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
    0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
    0x40 }; 

/* Table of CRC values for loworder byte */
static char auchCRCLo[] = {
    0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 0x07, 0xC7, 0x05, 0xC5, 0xC4,
    0x04, 0xCC, 0x0C, 0x0D, 0xCD, 0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09,
    0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A, 0x1E, 0xDE, 0xDF, 0x1F, 0xDD,
    0x1D, 0x1C, 0xDC, 0x14, 0xD4, 0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
    0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 0xF2, 0x32, 0x36, 0xF6, 0xF7,
    0x37, 0xF5, 0x35, 0x34, 0xF4, 0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A,
    0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29, 0xEB, 0x2B, 0x2A, 0xEA, 0xEE,
    0x2E, 0x2F, 0xEF, 0x2D, 0xED, 0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
    0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60, 0x61, 0xA1, 0x63, 0xA3, 0xA2,
    0x62, 0x66, 0xA6, 0xA7, 0x67, 0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F,
    0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68, 0x78, 0xB8, 0xB9, 0x79, 0xBB,
    0x7B, 0x7A, 0xBA, 0xBE, 0x7E, 0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
    0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 0x70, 0xB0, 0x50, 0x90, 0x91,
    0x51, 0x93, 0x53, 0x52, 0x92, 0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C,
    0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B, 0x99, 0x59, 0x58, 0x98, 0x88,
    0x48, 0x49, 0x89, 0x4B, 0x8B, 0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
    0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 0x43, 0x83, 0x41, 0x81, 0x80,
    0x40 };


/*
 * Compute CRC-16
 */
unsigned short CRC16 ( puchMsg, usDataLen )     
    unsigned char *puchMsg;                    
    unsigned short usDataLen;                  
    {
    unsigned char uchCRCHi = 0xFF ;
    unsigned char uchCRCLo = 0xFF ;

    unsigned uIndex ;

    while (usDataLen--) {
        uIndex = uchCRCLo ^ *puchMsg++;
        uchCRCLo = uchCRCHi ^ auchCRCHi[uIndex];
        uchCRCHi = auchCRCLo[uIndex];
    }
    return (uchCRCHi << 8 | uchCRCLo);
}



/*
 * Get error statistics and reset counters
 */
 
void modbus_get_err_cnt(unsigned int * crc_errors, unsigned int * timeout_errors)
{
    *crc_errors = crc_error_count;
    crc_error_count = 0;
    *timeout_errors = timeout_error_count;
    timeout_error_count = 0;
    
}


/*
 * Get modbus slave id from datagram
 */
unsigned char modbus_read_slave_id(unsigned char * datagram)
{
    return datagram[0];
}

/*
 * Get modbus function code from datagram
 */
unsigned char modbus_read_function_code(unsigned char * datagram)
{
    return datagram[1];
}

/*
 * Get number of modbus data bytes from datagram
 */
unsigned char modbus_read_num_bytes(unsigned char * datagram)
{
    return datagram[2];
}

/*
 * Get modbus CRC from datagram
 */
unsigned short modbus_read_crc(unsigned char * datagram)
{
    int crc_pos = 3 + modbus_read_num_bytes(datagram);
    return datagram[crc_pos] + (datagram[crc_pos + 1] << 8);
}

/*
 * Check if datagram is of exception type
 */
int modbus_is_exception(unsigned char * datagram)
{
    return datagram[1] & 0x80;
}


/*
 * Thread for reading serial device and put into receive buffer
 */
void * receive_thread(void * fd_t)
{
    int fd = (int) fd_t;
    
    while(1) {
    
        // read() is a blocking call when serial buffer is empty
        int n = read(fd, buffer, 255);

        if (n == -1) {
            perror("Unable to read from serial device");
            return NULL;
        }
        
        if (datagram_length + n > 255) {
            printf("Datagram to long, truncating last part\n");
            continue;
        }
        
        /*int i;
        for(i = 0; i < n; i++) {
            printf("0x%X ", buffer[i]);
        }*/
        
        if (mb_waiting_for_reply) {
          	clock_gettime(CLOCK_REALTIME, &mb_last_received_data);
            
            if (receiving == 0) {
                memcpy(datagram, buffer, n);
                receiving = 1;
                datagram_length = n;
                
                msgsnd(msgqueue, &ipc_message, sizeof(struct msgbuf), IPC_NOWAIT);
                
            } else {
                memcpy(datagram + datagram_length, buffer, n);
                datagram_length += n;
            }

            //printf("Read %d bytes\n", n);
        } else {
            //printf("Receiving data while not been requesting anything - ignoring\n");
        }

    }
}


/*
 * Finds time interval in microseconds beteween two timespec structs
 */
int diff(struct timespec start, struct timespec end)
{
	int microseconds = (start.tv_nsec - end.tv_nsec) / 1000 + (start.tv_sec - end.tv_sec) * 1000000;
	return microseconds;
}

/*
 * Thread for handling each modbus datagram
 */
void * response_handler_thread(void * id)
{   
    struct timespec temp;    

    while(msgrcv(msgqueue, &ipc_message, sizeof(struct msgbuf), 1, 0)) {    
        if (modbus_read_slave_id(datagram) == modbus_slave_id) {
            pthread_cond_signal(&datagram_start_receiving);
        } else {
            //printf("Datagram with wrong slave id received - ignoring\n");
        }

        int diff_time = 0;
        do  {
          	clock_gettime(CLOCK_REALTIME, &temp);
            usleep(datagram_spacing_usec);
            diff_time = diff(temp, mb_last_received_data);
          	clock_gettime(CLOCK_REALTIME, &temp);
        } while(diff_time < datagram_spacing_usec);

        // Datagram fully received
        receiving = 0;

        /*printf("Datagram received:\n");
        int i;
        for(i = 0; i < datagram_length; i++) {
            printf("0x%X ", datagram[i]);
        }
        printf("\n");*/
        
        short crc, datagram_crc;     
        switch(modbus_read_function_code(datagram)) {
            case MB_READ_HOLDING_REGISTERS:           
            crc = CRC16(datagram, 3 + modbus_read_num_bytes(datagram));
            datagram_crc = modbus_read_crc(datagram);

            if (crc != datagram_crc) {
                mb_datagram_status = MB_CRC_ERROR;
                crc_error_count++;
            } else {
                mb_datagram_status = MB_OK;
            }
            break;

            case MB_WRITE_MULTIPLE_REGISTERS:
            mb_datagram_status = MB_OK;            
            break;

            default:

            if (modbus_is_exception(datagram)) {
                mb_datagram_status = MB_EXCEPTION;
            } else {
                mb_datagram_status = MB_ERROR;
            }
        }

        pthread_cond_signal(&datagram_finished_receiving);
    }
    return NULL;
}


/*
 * Init function for opening serial device and setting up thread communciation
 * within the modbus module
 */ 
int modbus_init(char * device, speed_t serial_speed)
{
    // Init
    memset(datagram, 0, 255);
    msgqueue = msgget(MODBUS_MSG_QUEUE, IPC_CREAT | 0666);    
    ipc_message.mtype = 1;
    receiving = 0;
    mb_datagram_status = MB_OK;
   
    if (dev_testlock(device)) {
        fprintf(stderr, "%s is locked by another proccess\n", device);
        return 1;
    } 

    dev_lock(device);

    fd = open(device, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        char msg[100];
        snprintf(msg, 100, "Failed to open serial device %s", device);
        perror(msg);
        return 1;
    }
    
    fcntl(fd, F_SETFL, 0);
    
    /* Set minimum buffer size and baud rate */
    struct termios options;
    tcgetattr(fd, &options);
    options.c_cc[VMIN] = 1;
    options.c_cflag = CS8 | CLOCAL | CREAD;
    options.c_iflag = 0;
    options.c_oflag = 0;
    options.c_lflag = 0;
    if (cfsetspeed(&options, serial_speed)) {
        perror("Failed to set serial speed");
        return 1;
    }
    tcsetattr(fd, TCSANOW, &options);
       
    /* Threading stuff */
    pthread_mutex_init(&waiting_for_reply, NULL);
    pthread_cond_init(&datagram_start_receiving, NULL);
    pthread_cond_init(&datagram_finished_receiving, NULL);    
        
    pthread_t receive_t;
    pthread_t response_handler_t;

    /* Start receive threads */
    pthread_create(&receive_t, NULL, receive_thread, (void *)fd);
    pthread_create(&response_handler_t, NULL, response_handler_thread, (void *)fd);
    
    return 0;
}


/*
 * Function for reading holding registers with auto retry if failed
 */
int modbus_read_holding_registers_retry(unsigned char slave_id, unsigned int start_address, unsigned int num_registers, unsigned short * modbus_registers, unsigned int num_retries)
{
    int n = 0;
    unsigned int try = 0;
    
    while(n <= 0 && n != MB_EXCEPTION && try < num_retries) {
        n = modbus_read_holding_registers(slave_id, start_address, num_registers, modbus_registers);
        try++;
    }
    
    return n;
}

/*
 * Function for reading holding registers
 */
int modbus_read_holding_registers(unsigned char slave_id, unsigned int start_address, unsigned int num_registers, unsigned short * modbus_registers)
{
    unsigned char transmit[255];

    transmit[0] = slave_id;
    transmit[1] = MB_READ_HOLDING_REGISTERS;
    transmit[2] = start_address >> 8;
    transmit[3] = start_address & 0xFF;
    transmit[4] = num_registers >> 8;
    transmit[5] = num_registers & 0xFF;
    short crc = CRC16(transmit, 6);
    transmit[6] = crc;
    transmit[7] = crc >> 8; 
    
    if (write(fd, transmit, 8) == -1) {
        perror("Unable to write to serial device");
        mb_datagram_status = MB_ERROR;
        return -1;
    }

    
    mb_waiting_for_reply = 1;
    
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    time.tv_sec += 1;
    
   
    /* Time out here if no bytes have been received within the time out period */
    if (pthread_cond_timedwait(&datagram_start_receiving, &waiting_for_reply, &time) == ETIMEDOUT) {
        mb_datagram_status = MB_TIMEOUT;
        timeout_error_count++;
        
        return -1;
    }

    pthread_cond_wait(&datagram_finished_receiving, &waiting_for_reply);

    mb_waiting_for_reply = 0;

    if (mb_datagram_status == MB_OK) {

        unsigned char byte_count = modbus_read_num_bytes(datagram);
        mb_datagram_status = MB_OK;
        
        unsigned char * datagram_data = datagram + 3;
        
        int i;
        for(i = 0; i < byte_count / 2; i++) {
            modbus_registers[i] = datagram_data[i * 2] << 8 | datagram_data[i * 2 + 1];
        }
        return byte_count / 2;
        
    } else {
        return mb_datagram_status;
    }

}

/*
 * Function for writing multiple registers
 */
int modbus_write_multiple_registers(unsigned char slave_id, unsigned int start_address, unsigned int num_registers, unsigned short * modbus_registers)
{
    unsigned char transmit[255];

    transmit[0] = slave_id;
    transmit[1] = MB_WRITE_MULTIPLE_REGISTERS;
    transmit[2] = start_address >> 8;
    transmit[3] = start_address & 0xFF;
    transmit[4] = num_registers >> 8;
    transmit[5] = num_registers & 0xFF;
    transmit[6] = num_registers * 2;
    int i;
    for (i = 0; i < num_registers; i++) {
        transmit[7 + i * 2] = modbus_registers[i] >> 8;
        transmit[8 + i * 2] = modbus_registers[i] & 0xFF;
    }

    short crc = CRC16(transmit, 11);
    transmit[7 + num_registers * 2] = crc;
    transmit[8 + num_registers * 2] = crc >> 8;

    printf("writing: \n");
    for(i = 0; i < 9 + num_registers * 2; i++) {
        printf("0x%.2X\n", transmit[i]);
    }
    
    if (write(fd, transmit, 9 + num_registers * 2) == -1) {
        perror("Unable to write to serial device");
        mb_datagram_status = MB_ERROR;
        return -1;
    }

    mb_waiting_for_reply = 1;
    
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    time.tv_sec += 1;
    
   
    /* Time out here if no bytes have been received within the time out period */
    if (pthread_cond_timedwait(&datagram_start_receiving, &waiting_for_reply, &time) == ETIMEDOUT) {
        mb_datagram_status = MB_TIMEOUT;
        timeout_error_count++;
        
        return -1;
    }

    pthread_cond_wait(&datagram_finished_receiving, &waiting_for_reply);

    mb_waiting_for_reply = 0;

    if (mb_datagram_status == MB_OK) {
        return 0;
    } else {
        return mb_datagram_status;
    }
}


void modbus_print_error_str(char * error_msg, char * msg)
{
    switch (mb_datagram_status) {
        case MB_OK:
        snprintf(error_msg, 100, "%s: success", msg);        
        break;
        
        case MB_CRC_ERROR:
        snprintf(error_msg, 100, "%s: CRC error", msg);
        break;
        
        case MB_EXCEPTION:
        snprintf(error_msg, 100, "%s: exception datagram", msg);
        break;
        
        case MB_TIMEOUT:
        snprintf(error_msg, 100, "%s: connection timeout", msg);
        break;
        
        case MB_ERROR:
        snprintf(error_msg, 100, "%s: generic error", msg);
        break;
       
    }   
}


void modbus_print_error(FILE * stream, char * msg)
{
    char error_msg[100];

    switch (mb_datagram_status) {
        case MB_OK:
        snprintf(error_msg, 100, "%s: success\n", msg);        
        break;
        
        case MB_CRC_ERROR:
        snprintf(error_msg, 100, "%s: CRC error\n", msg);
        break;
        
        case MB_EXCEPTION:
        snprintf(error_msg, 100, "%s: exception datagram\n", msg);
        break;
        
        case MB_TIMEOUT:
        snprintf(error_msg, 100, "%s: connection timeout\n", msg);
        break;
        
        case MB_ERROR:
        snprintf(error_msg, 100, "%s: generic error\n", msg);
        break;
       
    }
    
    fprintf(stream, "%s", error_msg);        
    
}



