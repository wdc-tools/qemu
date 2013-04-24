/*
 * pqi_constants.h
 *
 * Copyright (C) 2012-2013 HGST, Inc.
 *
 * written by:
 *  Chris Barr <Christopher.Barr@hgst.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PQI_CONSTANTS_H_
#define PQI_CONSTANTS_H_

#include <stdint.h>

//=========================================================
// Register and register value definitions

typedef struct PQIAQConfigReg {
    uint64_t function_status_code : 8;
    uint64_t rsvdz                : 56;
} PQIAQConfigReg;

// function/status code values
#define QUEUE_CONFIG_IDLE         0
#define CREATING_ADMIN_QUEUE_PAIR 1
#define DELETING_ADMIN_QUEUE_PAIR 2

typedef struct PQIDevErrReg {
    uint16_t err_code_plus_qual;
    uint8_t byte_pointer;
    uint8_t rsvdz          : 3;
    uint8_t bit_pointer    : 3;
    uint8_t bpv            : 1;
    uint8_t err_data_valid : 1;
} PQIDevErrReg;

// err_code_plus_qual values
// Defined as QUALIFIER << 16 | CODE
#define ERROR_DETECTED_DURING_POST         0x0000
#define INVALID_PARAM_IN_PQI_REG           0x0002
#define ADMIN_QUEUE_PAIR_CREATE_DELETE_ERR 0x0003
#define ADMIN_REQUEST_IU_INVALID_TYPE      0x0104
#define ADMIN_REQUEST_IU_INVALID_LENGTH    0x0204
#define ADMIN_REQUEST_IU_INVALID_OQ_ID     0x0304
#define PQI_INTERNAL_ERROR                 0x0005
#define PQI_SOFT_RESET_ERROR               0x0106
#define PQI_FIRM_RESET_ERROR               0x0206
#define PQI_HARD_RESET_ERROR               0x0306

typedef struct PQIStandardRegisters {
    //TODO: Define PQI register block
} PQIStandardRegisters;

#endif /* PQI_CONSTANTS_H_ */
