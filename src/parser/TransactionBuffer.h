/* Header for TransactionBuffer class
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

#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

#include "../common/Ctx.h"
#include "../common/LobKey.h"
#include "../common/RedoLogRecord.h"
#include "../common/types.h"
#include "../common/typeXid.h"

#ifndef TRANSACTION_BUFFER_H_
#define TRANSACTION_BUFFER_H_

namespace OpenLogReplicator {
    class Transaction;
    class TransactionBuffer;
    class TransactionChunk;
    class XmlCtx;

    class TransactionChunkRecord {
    public:
        TransactionChunkRecord(TransactionChunk* newTc, uint8_t* newRecord, uint64_t newRecordSize);
        uint64_t size();
        typeOp2 opCode();
        RedoLogRecord* redo1();
        RedoLogRecord* redo2();
        uint8_t* redoData1();
        uint8_t* redoData2();
        void next();

    private:
        TransactionChunk* tc;
        uint8_t* record;
        uint64_t recordSize;
    };

    struct TransactionChunk {
        static constexpr uint64_t FULL_BUFFER_SIZE = 65536; // 64Kb
        static constexpr uint64_t HEADER_BUFFER_SIZE = sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint8_t*) + // 48 byte
                                                       sizeof(TransactionChunk*) + sizeof(TransactionChunk*); 
        static constexpr uint64_t DATA_BUFFER_SIZE = FULL_BUFFER_SIZE - HEADER_BUFFER_SIZE;
        static constexpr uint64_t ROW_HEADER_OP = 0;
        static constexpr uint64_t ROW_HEADER_REDO1 = sizeof(typeOp2);
        static constexpr uint64_t ROW_HEADER_REDO2 = sizeof(typeOp2) + sizeof(RedoLogRecord);
        static constexpr uint64_t ROW_HEADER_DATA = sizeof(typeOp2) + sizeof(RedoLogRecord) + sizeof(RedoLogRecord);
        static constexpr uint64_t ROW_HEADER_SIZE = sizeof(typeOp2) + sizeof(RedoLogRecord) + sizeof(RedoLogRecord);
        static constexpr uint64_t ROW_HEADER_TOTAL = sizeof(typeOp2) + sizeof(RedoLogRecord) + sizeof(RedoLogRecord) + sizeof(uint64_t);

        uint64_t elements;
        uint64_t size;
        uint64_t pos;
        uint8_t* header;
        TransactionChunk* prev;
        TransactionChunk* next;
        uint8_t buffer[DATA_BUFFER_SIZE];

        uint8_t* begin() {
            return buffer;
        }

        uint8_t* end() {
            return buffer + size;
        }

        TransactionChunkRecord firstRecord() {
            RedoLogRecord* redoLogRecord1 = reinterpret_cast<RedoLogRecord*>(begin() + ROW_HEADER_REDO1);
            RedoLogRecord* redoLogRecord2 = reinterpret_cast<RedoLogRecord*>(begin() + ROW_HEADER_REDO2);
            uint64_t lastSize = *(reinterpret_cast<uint64_t*>(begin() + ROW_HEADER_DATA + redoLogRecord1->size + redoLogRecord2->size));
            return TransactionChunkRecord {
                this,
                begin(),
                lastSize,
            };
        }

        TransactionChunkRecord lastRecord() {
            uint64_t lastSize = *(reinterpret_cast<uint64_t*>(end() - ROW_HEADER_TOTAL + ROW_HEADER_SIZE));
            return TransactionChunkRecord {
                this,
                end() - lastSize + ROW_HEADER_OP,
                lastSize,
            };
        }

        void appendTransaction(RedoLogRecord* redoLogRecord) {
            uint64_t chunkSize = redoLogRecord->size + ROW_HEADER_TOTAL;

            *(reinterpret_cast<typeOp2*>(end() + ROW_HEADER_OP)) = (redoLogRecord->opCode << 16);
            memcpy(reinterpret_cast<void*>(end() + ROW_HEADER_REDO1),
                reinterpret_cast<const void*>(redoLogRecord), sizeof(RedoLogRecord));
            memset(reinterpret_cast<void*>(end() + ROW_HEADER_REDO2), 0, sizeof(RedoLogRecord));
            memcpy(reinterpret_cast<void*>(end() + ROW_HEADER_DATA),
                reinterpret_cast<const void*>(redoLogRecord->data()), redoLogRecord->size);

            *(reinterpret_cast<uint64_t*>(end() + ROW_HEADER_SIZE + redoLogRecord->size)) = chunkSize;

            size += chunkSize;
            ++elements;
        }

        void appendTransaction(RedoLogRecord* redoLogRecord1, const RedoLogRecord* redoLogRecord2) {
            uint64_t chunkSize = redoLogRecord1->size + redoLogRecord2->size + ROW_HEADER_TOTAL;

            *(reinterpret_cast<typeOp2*>(end() + ROW_HEADER_OP)) = (redoLogRecord1->opCode << 16) | redoLogRecord2->opCode;
            memcpy(reinterpret_cast<void*>(end() + ROW_HEADER_REDO1),
                reinterpret_cast<const void*>(redoLogRecord1), sizeof(RedoLogRecord));
            memcpy(reinterpret_cast<void*>(end() + ROW_HEADER_REDO2),
                reinterpret_cast<const void*>(redoLogRecord2), sizeof(RedoLogRecord));
            memcpy(reinterpret_cast<void*>(end() + ROW_HEADER_DATA),
                reinterpret_cast<const void*>(redoLogRecord1->data()), redoLogRecord1->size);
            memcpy(reinterpret_cast<void*>(end() + ROW_HEADER_DATA + redoLogRecord1->size),
                reinterpret_cast<const void*>(redoLogRecord2->data()), redoLogRecord2->size);

            *(reinterpret_cast<uint64_t*>(end() + ROW_HEADER_SIZE + redoLogRecord1->size + redoLogRecord2->size)) = chunkSize;

            size += chunkSize;
            ++elements;
        }
    };

    class TransactionBuffer {
    public:
        static constexpr uint64_t BUFFERS_FREE_MASK = 0xFFFF;

    protected:
        Ctx* ctx;
        uint8_t buffer[TransactionChunk::DATA_BUFFER_SIZE];
        std::unordered_map<uint8_t*, uint64_t> partiallyFullChunks;

        std::mutex mtx;
        std::unordered_map<typeXidMap, Transaction*> xidTransactionMap;
        std::map<LobKey, Lob> orphanedLobs;

    public:
        std::set<typeXid> skipXidList;
        std::set<typeXid> dumpXidList;
        std::set<typeXidMap> brokenXidMapList;
        std::string dumpPath;

        explicit TransactionBuffer(Ctx* newCtx);
        virtual ~TransactionBuffer();

        void purge();

        /// @brief Find transaction or create it if `add` is true. 
        /// @param xmlCtx pointer to xml context
        /// @param xid xid
        /// @param conId container id
        /// @param old ?
        /// @param add ?
        /// @param rollback ?
        /// @return founded or new transaction
        [[nodiscard]] Transaction* findTransaction(XmlCtx* xmlCtx, typeXid xid, typeConId conId, bool old, bool add, bool rollback);
        
        /// @brief Delete transaction from buffer.
        /// @param xid xid
        /// @param conId container id
        void dropTransaction(typeXid xid, typeConId conId);
        void addTransactionChunk(Transaction* transaction, RedoLogRecord* redoLogRecord);
        void addTransactionChunk(Transaction* transaction, RedoLogRecord* redoLogRecord1, const RedoLogRecord* redoLogRecord2);
        void rollbackTransactionChunk(Transaction* transaction);
        
        /// @brief Allocate new transaction chank (64Kb) with self memory position info:
        ///
        /// header  - pointer to 1Mb memory chunk
        ///
        /// pos     - position in memory chunk (0-31)
        /// @return pointer to transaction chunk.
        [[nodiscard]] TransactionChunk* newTransactionChunk();
        
        /// @brief Delete transaction chunk. Mark the space as unusable, or after deleting 16 transaction chunks in one memory block, delete the memory block.
        /// @param tc pointer to transaction
        void deleteTransactionChunk(TransactionChunk* tc);
        
        /// @brief Delete all related transaction chunks.
        /// @param tc pointer to transaction
        void deleteTransactionChunks(TransactionChunk* tc);
        void mergeBlocks(uint8_t* mergeBuffer, RedoLogRecord* redoLogRecord1, const RedoLogRecord* redoLogRecord2);
        
        /// @brief Return information about transactions.
        /// @param minSequence minimum of read sequence 
        /// @param minOffset minimum of read block
        /// @param minXid minimum of read xid
        void checkpoint(typeSeq& minSequence, uint64_t& minOffset, typeXid& minXid);
        void addOrphanedLob(RedoLogRecord* redoLogRecord1);

        /// @brief Allocate data with lob and record data:
        /// 
        /// | lobSize (8) | log record (216) | lob data (?) |
        /// @param redoLogRecord1 log record
        /// @return allocated part of lob with log record info.
        Lob allocateLob(const RedoLogRecord* redoLogRecord1) const;
    };
}

#endif
