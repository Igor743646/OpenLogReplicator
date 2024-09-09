/* Header for Parser class
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

#include "../common/Ctx.h"
#include "../common/RedoLogRecord.h"
#include "../common/types.h"
#include "../common/typeTime.h"
#include "../common/typeXid.h"

#ifndef PARSER_H_
#define PARSER_H_

namespace OpenLogReplicator {
    class Builder;
    class Reader;
    class Metadata;
    class Transaction;
    class TransactionBuffer;
    class XmlCtx;

    /*
        LWN Member - Log Writer Number Member.
    */
    struct LwnMember {
        uint64_t offset;    // offset of block
        uint32_t size;      // size of LWN
        typeScn scn;        // current SCN
        typeSubScn subScn;  // current subSCN
        typeBlk block;      // number of block

        bool operator<(const LwnMember& other) const {
            if (scn < other.scn)
                return true;
            if (other.scn < scn)
                return false;
            if (subScn < other.subScn)
                return true;
            if (other.subScn < subScn)
                return false;
            if (block < other.block)
                return true;
            if (block > other.block)
                return false;
            return (offset < other.offset);
        }
    };

    class LwnMembersManager {
        static constexpr uint64_t MAX_LWN_CHUNKS = 512 * 2 / Ctx::MEMORY_CHUNK_SIZE_MB;
        static constexpr uint64_t MAX_RECORDS_IN_LWN = 1048576;

    public:
        LwnMembersManager(Ctx* newCtx):
                ctx(newCtx),
                lwnAllocated(0),
                lwnAllocatedMax(0),
                lwnRecords(0) {
            lwnChunks[0] = ctx->getMemoryChunk(Ctx::MEMORY_MODULE_PARSER, false);
            auto size = reinterpret_cast<uint64_t*>(lwnChunks[0]);
            *size = sizeof(uint64_t);
            lwnAllocated = 1;
            lwnAllocatedMax = 1;
            lwnMembers[0] = 0;
        }

        void freeLwnMembers() {
            while (lwnAllocated > 1) {
                ctx->freeMemoryChunk(Ctx::MEMORY_MODULE_PARSER, lwnChunks[--lwnAllocated], false);
            }

            auto size = reinterpret_cast<uint64_t*>(lwnChunks[0]);
            *size = sizeof(uint64_t);
        }

        ~LwnMembersManager() {
            while (lwnAllocated > 0) {
                ctx->freeMemoryChunk(Ctx::MEMORY_MODULE_PARSER, lwnChunks[--lwnAllocated], false);
            }
        }

        LwnMember* allocateLwnMember(uint64_t recordSize4) {
            uint64_t* recordSize = reinterpret_cast<uint64_t*>(lwnChunks[lwnAllocated - 1]);

            if (((*recordSize + sizeof(struct LwnMember) + recordSize4 + 7) & 0xFFFFFFF8) > Ctx::MEMORY_CHUNK_SIZE) {
                if (unlikely(lwnAllocated == MAX_LWN_CHUNKS))
                    throw RedoLogException(50052, "all " + std::to_string(MAX_LWN_CHUNKS) + " lwn buffers allocated");

                allocateChunk();
                recordSize = reinterpret_cast<uint64_t*>(lwnChunks[lwnAllocated - 1]);
                *recordSize = sizeof(uint64_t);
            }

            if (unlikely(((*recordSize + sizeof(struct LwnMember) + recordSize4 + 7) & 0xFFFFFFF8) > Ctx::MEMORY_CHUNK_SIZE))
                throw RedoLogException(50053, "too big redo log record, size: " + std::to_string(recordSize4));

            LwnMember* result = reinterpret_cast<struct LwnMember*>(lwnChunks[lwnAllocated - 1] + *recordSize);
            *recordSize += (sizeof(struct LwnMember) + recordSize4 + 7) & 0xFFFFFFF8;

            return result;
        }

        void addLwnMember(LwnMember* lwnMember) {
            uint64_t lwnPos = ++lwnRecords;
            if (unlikely(lwnPos >= MAX_RECORDS_IN_LWN))
                throw RedoLogException(50054, "all " + std::to_string(lwnPos) + " records in lwn were used");

            // make heap
            while (lwnPos > 1 && *lwnMember < *lwnMembers[lwnPos / 2]) { 
                lwnMembers[lwnPos] = lwnMembers[lwnPos / 2];
                lwnPos = lwnPos / 2;
            }
            lwnMembers[lwnPos] = lwnMember;
        }

        void dropMin() {
            uint64_t lwnPos = 1;
            while (true) {
                uint64_t left = lwnPos * 2, right = lwnPos * 2 + 1;
                if (left < lwnRecords && *lwnMembers[left] < *lwnMembers[lwnRecords]) {
                    if (right < lwnRecords && *lwnMembers[right] < *lwnMembers[left]) {
                        lwnMembers[lwnPos] = lwnMembers[right];
                        lwnPos *= 2;
                        ++lwnPos;
                    } else {
                        lwnMembers[lwnPos] = lwnMembers[left];
                        lwnPos *= 2;
                    }
                } else if (right < lwnRecords && *lwnMembers[right] < *lwnMembers[lwnRecords]) {
                    lwnMembers[lwnPos] = lwnMembers[right];
                    lwnPos *= 2;
                    ++lwnPos;
                } else
                    break;
            }

            lwnMembers[lwnPos] = lwnMembers[lwnRecords];
            --lwnRecords;
        }

        void reset() {
            lwnRecords = 0;
        }

        uint64_t maxAllocated() const {
            return lwnAllocatedMax;
        }

        uint64_t records() const {
            return lwnRecords;
        }

        LwnMember* getMinLwnMember() {
            return lwnMembers[1];
        }
    
    private:

        void allocateChunk() {
            lwnChunks[lwnAllocated] = ctx->getMemoryChunk(Ctx::MEMORY_MODULE_PARSER, false);
            lwnAllocated++;
            lwnAllocatedMax = std::max(lwnAllocatedMax, lwnAllocated);
        }

        Ctx* ctx;
        uint8_t* lwnChunks[MAX_LWN_CHUNKS];
        LwnMember* lwnMembers[MAX_RECORDS_IN_LWN + 1];
        uint64_t lwnAllocated;
        uint64_t lwnAllocatedMax;
        uint64_t lwnRecords;
    };

    class Parser final {
    protected:
        Ctx* ctx;
        Builder* builder;
        Metadata* metadata;
        TransactionBuffer* transactionBuffer;
        RedoLogRecord zero;
        Transaction* lastTransaction;

        LwnMembersManager lwnManager;
        typeTime lwnTimestamp;
        typeScn lwnScn;
        typeBlk lwnCheckpointBlock;

        void freeLwn();
        void analyzeLwn(LwnMember* lwnMember);
        void appendToTransactionDdl(RedoLogRecord* redoLogRecord1);
        void appendToTransactionBegin(RedoLogRecord* redoLogRecord1);
        void appendToTransactionCommit(RedoLogRecord* redoLogRecord1);
        void appendToTransactionLob(RedoLogRecord* redoLogRecord1);
        void appendToTransactionIndex(RedoLogRecord* redoLogRecord1, RedoLogRecord* redoLogRecord2);
        void appendToTransaction(RedoLogRecord* redoLogRecord1);
        void appendToTransactionRollback(RedoLogRecord* redoLogRecord1);
        void appendToTransaction(RedoLogRecord* redoLogRecord1, RedoLogRecord* redoLogRecord2);
        void appendToTransactionRollback(RedoLogRecord* redoLogRecord1, RedoLogRecord* redoLogRecord2);
        void dumpRedoVector(const uint8_t* data, typeSize recordSize) const;

    public:
        int64_t group;
        std::string path;
        typeSeq sequence;
        typeScn firstScn;
        typeScn nextScn;
        Reader* reader;

        Parser(Ctx* newCtx, Builder* newBuilder, Metadata* newMetadata, TransactionBuffer* newTransactionBuffer, int64_t newGroup, const std::string& newPath);
        virtual ~Parser();

        uint64_t parse();
        std::string toString() const;
    };
}

#endif
