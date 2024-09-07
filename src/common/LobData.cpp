/* Definition of LobData
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

#include "LobData.h"
#include "RedoLogRecord.h"

namespace OpenLogReplicator {

    Lob::Lob() : 
            data(nullptr) {
    }

    Lob::Lob(uint8_t * newData) : 
            data(newData) {
    }

    Lob::Lob(const RedoLogRecord* redoLogRecord1) {
        const uint64_t lobSize = sizeof(uint64_t) + sizeof(RedoLogRecord) + redoLogRecord1->size;
        data = new uint8_t[lobSize];

        *(reinterpret_cast<uint64_t*>(data)) = lobSize;

        RedoLogRecord* redoLogRecord1new = reinterpret_cast<RedoLogRecord*>(data + sizeof(uint64_t));
        
        memcpy(reinterpret_cast<void*>(redoLogRecord1new),
               reinterpret_cast<const void*>(redoLogRecord1), sizeof(RedoLogRecord));

        redoLogRecord1new->dataExt = data + sizeof(uint64_t) + sizeof(RedoLogRecord);

        memcpy(reinterpret_cast<void*>(redoLogRecord1new->dataExt),
               reinterpret_cast<const void*>(redoLogRecord1->data()), redoLogRecord1->size);
    }

    Lob::Lob(Lob&& newLob) : 
            data(nullptr) {
        *this = std::move(newLob);
    }
    
    Lob& Lob::operator=(Lob&& newLob) {
        if (this != &newLob) {
            clear();
            data = newLob.data;
            newLob.data = nullptr;
        }

        return *this;
    }

    Lob::~Lob() {
        clear();
    }

    void Lob::clear() {
        delete[] data;
        data = nullptr;
    }

    uint64_t Lob::lobSize() const { 
        return *(reinterpret_cast<uint64_t*>(data)); 
    }

    RedoLogRecord* Lob::redoLogRecord() const { 
        return reinterpret_cast<RedoLogRecord*>(data + sizeof(uint64_t)); 
    }

    uint8_t* Lob::lobData() const { 
        return data + sizeof(uint64_t) + sizeof(RedoLogRecord); 
    }

    LobDataElement::LobDataElement() :
            dba(0),
            offset(0) {
    }

    LobDataElement::LobDataElement(typeDba newDba, uint32_t newOffset) :
            dba(newDba),
            offset(newOffset) {
    }

    LobDataElement::~LobDataElement() {

    }

    bool LobDataElement::operator<(const LobDataElement& other) const {
        if (dba < other.dba)
            return true;

        if (dba > other.dba)
            return false;

        if (offset < other.offset)
            return true;

        return false;
    }


    LobData::LobData() :
            pageSize(0),
            sizePages(0),
            sizeRest(0) {
    }

    LobData::~LobData() {
        dataMap.clear();
        indexMap.clear();
    }
}
