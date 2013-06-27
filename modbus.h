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

#ifndef _MODBUS_H 
#define _MODBUS_H

#include <termios.h>

unsigned short CRC16 (unsigned char *puchMsg, unsigned short usDataLen);
int modbus_init(char * device, speed_t serial_speed);
int modbus_read_holding_registers(unsigned char slave_id, unsigned int start_address, unsigned int num_registers, unsigned short * modbus_registers);
int modbus_read_holding_registers_retry(unsigned char slave_id, unsigned int start_address, unsigned int num_registers, unsigned short * modbus_registers, unsigned int num_retries);
int modbus_write_multiple_registers(unsigned char slave_id, unsigned int start_address, unsigned int num_registers, unsigned short * modbus_registers);
void modbus_print_error(FILE * stream, char * msg);
void modbus_get_err_cnt(unsigned int * crc_errors, unsigned int * timeout_errors);
void modbus_print_error_str(char * error_msg, char * msg);

#define MB_READ_HOLDING_REGISTERS 0x03
#define MB_WRITE_MULTIPLE_REGISTERS 0x10
#define MB_OK 0
#define MB_CRC_ERROR -1
#define MB_EXCEPTION -2
#define MB_ERROR -3
#define MB_TIMEOUT -4

#endif
