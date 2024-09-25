/* Definition of type typeXid
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

#include <cstring>
#include <iomanip>
#include <ostream>

#include "types.h"
#include "exception/DataException.h"

#ifndef TYPE_XID_H_
#define TYPE_XID_H_

namespace OpenLogReplicator {
    /*
        Xid - is a transaction ID, a number that uniquely identifies the transaction.
        Xid consists of three parts: 
        - XIDUSN is a Undo Segment Number.
        - XIDSLOT is a Slot Number.
        - XIDSQN is a Sequence Number.
        
        |Usn|Slt|Sqn|
        |-|-|-|
        |16|16|32|
    */
    class typeXid final {
        uint64_t data;
    public:
        static constexpr uint64_t XID_FORMAT_TEXT_HEX = 0;
        static constexpr uint64_t XID_FORMAT_TEXT_DEC = 1;
        static constexpr uint64_t XID_FORMAT_NUMERIC = 2;
        static constexpr uint64_t XID_FORMAT_TEXT_ONLY_HEX = 3;

        typeXid() : data(0) {
        };

        explicit typeXid(uint64_t newData) : data(newData) {
        }

        typeXid(typeUsn usn, typeSlt slt, typeSqn sqn) {
            data = (static_cast<uint64_t>(usn) << 48) | (static_cast<uint64_t>(slt) << 32) | static_cast<uint64_t>(sqn);
        }

        explicit typeXid(const char* str) {
            std::string usn_;
            std::string slt_;
            std::string sqn_;

            uint64_t length = strnlen(str, 25);
            // UUUUSSSSQQQQQQQQ
            if (length == 16) {
                for (uint64_t i = 0; i < 16; ++i)
                    if (unlikely(!isxdigit(str[i])))
                        throw DataException(20002, "bad XID value: " + std::string(str));
                usn_.assign(str, 4);
                slt_.assign(str + 4, 4);
                sqn_.assign(str + 8, 8);
            } else if (length == 17) {
                // UUUU.SSS.QQQQQQQQ
                for (uint64_t i = 0; i < 17; ++i)
                    if (unlikely(!isxdigit(str[i]) && i != 4 && i != 8))
                        throw DataException(20002, "bad XID value: " + std::string(str));
                if (unlikely(str[4] != '.' || str[8] != '.'))
                    throw DataException(20002, "bad XID value: " + std::string(str));
                usn_.assign(str, 4);
                slt_.assign(str + 5, 3);
                sqn_.assign(str + 9, 8);
            } else if (length == 18) {
                // UUUU.SSSS.QQQQQQQQ
                for (uint64_t i = 0; i < 18; ++i)
                    if (unlikely(!isxdigit(str[i]) && i != 4 && i != 9))
                        throw DataException(20002, "bad XID value: " + std::string(str));
                if (unlikely(str[4] != '.' || str[9] != '.'))
                    throw DataException(20002, "bad XID value: " + std::string(str));
                usn_.assign(str, 4);
                slt_.assign(str + 5, 4);
                sqn_.assign(str + 10, 8);
            } else if (length == 19) {
                // 0xUUUU.SSS.QQQQQQQQ
                for (uint64_t i = 2; i < 19; ++i)
                    if (unlikely(!isxdigit(str[i]) && i != 6 && i != 10))
                        throw DataException(20002, "bad XID value: " + std::string(str));
                if (unlikely(str[0] != '0' || str[1] != 'x' || str[6] != '.' || str[10] != '.'))
                    throw DataException(20002, "bad XID value: " + std::string(str));
                usn_.assign(str + 2, 4);
                slt_.assign(str + 7, 3);
                sqn_.assign(str + 11, 8);
            } else if (length == 20) {
                // 0xUUUU.SSSS.QQQQQQQQ
                for (uint64_t i = 2; i < 20; ++i)
                    if (unlikely(!isxdigit(str[i]) && i != 6 && i != 11))
                        throw DataException(20002, "bad XID value: " + std::string(str));
                if (unlikely(str[0] != '0' || str[1] != 'x' || str[6] != '.' || str[11] != '.'))
                    throw DataException(20002, "bad XID value: " + std::string(str));
                usn_.assign(str + 2, 4);
                slt_.assign(str + 7, 4);
                sqn_.assign(str + 12, 8);
            } else
                throw DataException(20002, "bad XID value: " + std::string(str));

            data = (static_cast<uint64_t>(stoul(usn_, nullptr, 16)) << 48) |
                   (static_cast<uint64_t>(stoul(slt_, nullptr, 16)) << 32) |
                   static_cast<uint64_t>(stoul(sqn_, nullptr, 16));
        }

        uint64_t getData() const {
            return data;
        }

        bool isEmpty() {
            return (data == 0);
        }

        typeUsn usn() const {
            return static_cast<typeUsn>(data >> 48);
        }

        typeSlt slt() const {
            return static_cast<typeSlt>((data >> 32) & 0xFFFF);
        }

        typeSqn sqn() const {
            return static_cast<typeSqn>(data & 0xFFFFFFFF);
        }

        bool operator!=(typeXid other) const {
            return data != other.data;
        }

        bool operator<(typeXid other) const {
            return data < other.data;
        }

        bool operator==(typeXid other) const {
            return data == other.data;
        }

        typeXid& operator=(uint64_t newData) {
            data = newData;
            return *this;
        }

        uint64_t toUint() const {
            return data;
        }

        std::string toString(uint64_t format = 0) const {
            std::ostringstream ss;

            if (format == XID_FORMAT_TEXT_HEX) {
                ss << "0x" << std::setfill('0') << std::setw(4) << std::hex << usn() << "." << std::setw(3) <<
                        slt() << "." << std::setw(8) << sqn();
            } else if (format == XID_FORMAT_TEXT_DEC) {
                ss << usn() << "." << slt() << "." << sqn();
            } else if (format == XID_FORMAT_NUMERIC) {
                ss << getData();
            } else if (format == XID_FORMAT_TEXT_ONLY_HEX) {
                ss << std::hex << std::setfill('0') << std::setw(4) << __builtin_bswap16(usn()) << std::setw(4) <<
                        __builtin_bswap16(slt()) << std::setw(8) << __builtin_bswap32(sqn());
            }

            return ss.str();
        }
    };
}

#endif
