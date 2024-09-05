/* Definition of types and macros
   Copyright (C) 2018-2024 Adam Leszczynski (aleszczynski@bersler.com)

This file is part of OpenLogReplicator.

OpenLogReplicator is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 3, or (at your option)
any later version.

OpenLogReplicator is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenLogReplicator; see the file LICENSE;  If not see
<http://www.gnu.org/licenses/>.  */

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "../../config.h"

#ifndef TYPES_H_
#define TYPES_H_

typedef uint32_t typeResetlogs;
typedef uint32_t typeActivation;
/*
   typeSum - is a type for checksum.
*/
typedef uint16_t typeSum;
/*
   TODO: ?????
*/
typedef uint16_t typeOp1;
/*
   TODO: ?????
*/
typedef uint32_t typeOp2;
/*
   ConId - is a container id for the database. This is used for multi-tenant databases.
   -1 is the default value and means that the database is single-tenant.
*/
typedef int16_t typeConId;
typedef uint64_t typeUba;
typedef uint32_t typeSeq;
/*
   SCN - is a huge 6 byte (48 bits) number with two components to it: 
   - SCN Base is a 32 bit (4 Bytes) number.
   - SCB Wrap is a 16 bit (2 Bytes) number.

   Both BASE & WRAP are used to control the SCN’s increment and to ensure that the database won’t run out of it. 
   SCN_WRAP is incremented by 1 when SCN_BASE reaches the value of 4 Billion and SCN_BASE becomes 0.
   From Oracle Version 12c, the SCN number is an 8 byte number.
*/
typedef uint64_t typeScn;
typedef uint16_t typeSubScn;
/* 
   Some 64bit index 
*/
typedef uint64_t typeIdx;
/*
   SLT - is a slot number. Part of XID.
*/
typedef uint16_t typeSlt;
/*
   SQN - is a sequence number. Part of XID.
*/
typedef uint32_t typeSqn;
typedef uint8_t typeRci;
/*
   USN - is a undo segment number. Part of XID.
*/
typedef int16_t typeUsn;
typedef uint64_t typeXidMap;
typedef uint16_t typeAfn;
typedef uint32_t typeDba;
typedef uint16_t typeSlot;
typedef uint32_t typeBlk;
typedef uint32_t typeObj;
typedef uint32_t typeDataObj;
typedef uint64_t typeObj2;
typedef int16_t typeCol;
typedef uint16_t typeType;
typedef uint32_t typeCon;
typedef uint32_t typeTs;
typedef uint32_t typeUser;
typedef uint8_t typeOptions;
typedef uint16_t typeField;
typedef uint16_t typePos;
typedef uint16_t typeSize;
typedef uint8_t typeCC;
typedef uint16_t typeCCExt;

typedef uint16_t typeUnicode16;
typedef uint32_t typeUnicode32;
typedef uint64_t typeUnicode;
typedef int64_t time_ut;

#define likely(x)                               __builtin_expect(!!(x),1)
#define unlikely(x)                             __builtin_expect(!!(x),0)

#define BLOCK(__uba)                            (static_cast<uint32_t>((__uba)&0xFFFFFFFF))
#define SEQUENCE(__uba)                         (static_cast<uint16_t>(((static_cast<uint64_t>(__uba))>>32)&0xFFFF))
#define RECORD(__uba)                           (static_cast<uint8_t>(((static_cast<uint64_t>(__uba))>>48)&0xFF))
#define PRINTUBA(__uba)                         "0x"<<std::setfill('0')<<std::setw(8)<<std::hex<<BLOCK(__uba)<<"."<<std::setfill('0')<<std::setw(4)<<std::hex<<SEQUENCE(__uba)<<"."<<std::setfill('0')<<std::setw(2)<<std::hex<<static_cast<uint32_t>RECORD(__uba)

#define SCN(__scn1, __scn2)                      (((static_cast<uint64_t>(__scn1))<<32)|(__scn2))
#define PRINTSCN48(__scn)                       "0x"<<std::setfill('0')<<std::setw(4)<<std::hex<<(static_cast<uint32_t>((__scn)>>32)&0xFFFF)<<"."<<std::setw(8)<<((__scn)&0xFFFFFFFF)<<std::dec<<" ("<<__scn<<")"<<std::hex
#define PRINTSCN64(__scn)                       "0x"<<std::setfill('0')<<std::setw(16)<<std::hex<<(__scn)<<std::dec<<" ("<<__scn<<")"<<std::hex
#define PRINTSCN64D(__scn)                      "0x"<<std::setfill('0')<<std::setw(4)<<std::hex<<(static_cast<uint32_t>((__scn)>>48)&0xFFFF)<<"."<<std::setw(4)<<(static_cast<uint32_t>((__scn)>>32)&0xFFFF)<<"."<<std::setw(8)<<((__scn)&0xFFFFFFFF)<<std::dec<<" ("<<__scn<<")"<<std::hex
 
#endif
