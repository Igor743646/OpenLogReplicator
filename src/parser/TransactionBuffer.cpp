/* Buffer to handle transactions
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

#include "../common/RedoLogRecord.h"
#include "../common/exception/RedoLogException.h"
#include "OpCode0501.h"
#include "OpCode050B.h"
#include "Transaction.h"
#include "TransactionBuffer.h"

namespace OpenLogReplicator {
    TransactionBuffer::TransactionBuffer(Ctx* newCtx) :
            ctx(newCtx) {
        buffer[0] = 0;
    }

    TransactionBuffer::~TransactionBuffer() {
        if (!partiallyFullChunks.empty())
            ctx->error(50062, "non-free blocks in transaction buffer: " + std::to_string(partiallyFullChunks.size()));

        skipXidList.clear();
        dumpXidList.clear();
        brokenXidMapList.clear();
        orphanedLobs.clear();
    }

    void TransactionBuffer::purge() {
        for (auto xidTransactionMapIt: xidTransactionMap) {
            Transaction* transaction = xidTransactionMapIt.second;
            transaction->purge(this);
            delete transaction;
        }
        xidTransactionMap.clear();
    }

    Transaction* TransactionBuffer::findTransaction(XmlCtx* xmlCtx, typeXid xid, typeConId conId, bool old, bool add, bool rollback) {
        // xidMap = | 0 (16) | Container id (16) | Undo segment number (16) | Slot number (16) |
        typeXidMap xidMap = (xid.getData() >> 32) | ((static_cast<uint64_t>(conId)) << 32);
        Transaction* transaction;

        auto xidTransactionMapIt = xidTransactionMap.find(xidMap);          // Find transaction.
        if (xidTransactionMapIt != xidTransactionMap.end()) {               // If there is it
            transaction = xidTransactionMapIt->second;                      // return it,
            if (unlikely(!rollback && (!old || transaction->xid != xid)))   // ??
                throw RedoLogException(50039, "transaction " + xid.toString() + " conflicts with " + transaction->xid.toString());
        } else {                                                            // else create transaction
            if (!add)                                                       // if add is true.
                return nullptr;

            transaction = new Transaction(xid, &orphanedLobs, xmlCtx);      // Create new transaction
            {
                std::unique_lock<std::mutex> lck(mtx);
                xidTransactionMap.insert_or_assign(xidMap, transaction);    // Save it by xidMap
            }

            if (dumpXidList.find(xid) != dumpXidList.end())                 // If transaction in dump ?, mark it
                transaction->dump = true;
        }

        return transaction;
    }

    void TransactionBuffer::dropTransaction(typeXid xid, typeConId conId) {
        typeXidMap xidMap = (xid.getData() >> 32) | (static_cast<uint64_t>(conId) << 32);
        {
            std::unique_lock<std::mutex> lck(mtx);
            xidTransactionMap.erase(xidMap);
        }
    }

    TransactionChunk* TransactionBuffer::newTransactionChunk() {
        uint8_t* chunk;
        TransactionChunk* tc;
        uint64_t pos;
        uint64_t freeMap;
        if (!partiallyFullChunks.empty()) {
            auto partiallyFullChunksIt = partiallyFullChunks.cbegin();  // get random chunk
            chunk = partiallyFullChunksIt->first;                       // exp address: 0xAF78FSF4
            freeMap = partiallyFullChunksIt->second;                    // exp: 0b1111111111110010
            pos = ffs(freeMap) - 1;                                     // exp: 2 - 1 = 1       ^  position in 1Mb memory chunk
            freeMap &= ~(1 << pos);                                     // exp: 0b1111111111110000
            if (freeMap == 0)
                partiallyFullChunks.erase(chunk);                       // delete if chunk is full
            else
                partiallyFullChunks.insert_or_assign(chunk, freeMap);   // else update chunk info
        } else {
            chunk = ctx->getMemoryChunk(Ctx::MEMORY_MODULE_TRANSACTIONS, false);    // get chunk 1Mb
            pos = 0;                                                                // pos = 0
            freeMap = BUFFERS_FREE_MASK & (~1);                                     // 0b1111111111111110
            partiallyFullChunks.insert_or_assign(chunk, freeMap);                   // mark 1 chunk full
        }

        tc = reinterpret_cast<TransactionChunk*>(chunk + TransactionChunk::FULL_BUFFER_SIZE * pos);
        memset(reinterpret_cast<void*>(tc), 0, TransactionChunk::HEADER_BUFFER_SIZE);               // Clear header 
        // write self position
        tc->header = chunk; 
        tc->pos = pos;
        return tc;
    }

    void TransactionBuffer::deleteTransactionChunk(TransactionChunk* tc) {
        // get self position
        uint8_t* chunk = tc->header;
        typePos pos = tc->pos;
        uint64_t freeMap = partiallyFullChunks[chunk];

        freeMap |= (1 << pos);                          // exp: 0b1111111111110000 | 0b10 = 0b1111111111110010

        if (freeMap == BUFFERS_FREE_MASK) {
            ctx->freeMemoryChunk(Ctx::MEMORY_MODULE_TRANSACTIONS, chunk, false);    // delete 1Mb free memory chunk
            partiallyFullChunks.erase(chunk);
        } else
            partiallyFullChunks.insert_or_assign(chunk, freeMap);                   // update chunk info
    }

    void TransactionBuffer::deleteTransactionChunks(TransactionChunk* tc) {
        // ? assert tc->prev == nullptr
        TransactionChunk* nextTc;
        while (tc != nullptr) {
            nextTc = tc->next;
            deleteTransactionChunk(tc);
            tc = nextTc;
        }
    }

    void TransactionBuffer::addTransactionChunk(Transaction* transaction, RedoLogRecord* redoLogRecord) {
        uint64_t chunkSize = redoLogRecord->size + ROW_HEADER_TOTAL;

        if (unlikely(chunkSize > TransactionChunk::DATA_BUFFER_SIZE))
            throw RedoLogException(50040, "block size (" + std::to_string(chunkSize) + ") exceeding max block size (" +
                                          std::to_string(TransactionChunk::FULL_BUFFER_SIZE) + "), try increasing the FULL_BUFFER_SIZE parameter");

        if (transaction->lastSplit) {
            if (unlikely((redoLogRecord->flg & OpCode::FLG_MULTIBLOCKUNDOMID) == 0))
                throw RedoLogException(50041, "bad split offset: " + std::to_string(redoLogRecord->dataOffset) + " xid: " +
                                              transaction->xid.toString());

            uint64_t lastSize = *(reinterpret_cast<uint64_t*>(transaction->lastTc->buffer + transaction->lastTc->size - ROW_HEADER_TOTAL + ROW_HEADER_SIZE));
            RedoLogRecord* last501 = reinterpret_cast<RedoLogRecord*>(transaction->lastTc->buffer + transaction->lastTc->size - lastSize + ROW_HEADER_REDO1);
            last501->dataExt = transaction->lastTc->buffer + transaction->lastTc->size - lastSize + ROW_HEADER_DATA;

            uint64_t mergeSize = last501->size + redoLogRecord->size;
            transaction->mergeBuffer = new uint8_t[mergeSize];
            mergeBlocks(transaction->mergeBuffer, redoLogRecord, last501);
            rollbackTransactionChunk(transaction);
        }
        if ((redoLogRecord->flg & (OpCode::FLG_MULTIBLOCKUNDOTAIL | OpCode::FLG_MULTIBLOCKUNDOMID)) != 0)
            transaction->lastSplit = true;
        else
            transaction->lastSplit = false;

        // Empty list
        if (transaction->lastTc == nullptr) {
            transaction->lastTc = newTransactionChunk();
            transaction->firstTc = transaction->lastTc;
        }

        // New block needed
        if (transaction->lastTc->size + chunkSize > TransactionChunk::DATA_BUFFER_SIZE) {
            TransactionChunk* tcNew = newTransactionChunk();
            tcNew->prev = transaction->lastTc;
            transaction->lastTc->next = tcNew;
            transaction->lastTc = tcNew;
        }

        // Append to the chunk at the end
        TransactionChunk* tc = transaction->lastTc;
        *(reinterpret_cast<typeOp2*>(tc->buffer + tc->size + ROW_HEADER_OP)) = (redoLogRecord->opCode << 16);
        memcpy(reinterpret_cast<void*>(tc->buffer + tc->size + ROW_HEADER_REDO1),
               reinterpret_cast<const void*>(redoLogRecord), sizeof(RedoLogRecord));
        memset(reinterpret_cast<void*>(tc->buffer + tc->size + ROW_HEADER_REDO2), 0, sizeof(RedoLogRecord));
        memcpy(reinterpret_cast<void*>(tc->buffer + tc->size + ROW_HEADER_DATA),
               reinterpret_cast<const void*>(redoLogRecord->data()), redoLogRecord->size);

        *(reinterpret_cast<uint64_t*>(tc->buffer + tc->size + ROW_HEADER_SIZE + redoLogRecord->size)) = chunkSize;

        tc->size += chunkSize;
        ++tc->elements;
        transaction->size += chunkSize;

        if (transaction->mergeBuffer != nullptr) {
            delete[] transaction->mergeBuffer;
            transaction->mergeBuffer = nullptr;
        }
    }

    void TransactionBuffer::addTransactionChunk(Transaction* transaction, RedoLogRecord* redoLogRecord1, const RedoLogRecord* redoLogRecord2) {
        uint64_t chunkSize = redoLogRecord1->size + redoLogRecord2->size + ROW_HEADER_TOTAL;

        if (unlikely(chunkSize > TransactionChunk::DATA_BUFFER_SIZE))
            throw RedoLogException(50040, "block size (" + std::to_string(chunkSize) + ") exceeding max block size (" +
                                          std::to_string(TransactionChunk::DATA_BUFFER_SIZE) + "), try increasing the FULL_BUFFER_SIZE parameter");

        if (transaction->lastSplit) {
            if (unlikely((redoLogRecord1->opCode) != 0x0501))
                throw RedoLogException(50042, "split undo HEAD no 5.1 offset: " + std::to_string(redoLogRecord1->dataOffset));

            if (unlikely((redoLogRecord1->flg & OpCode::FLG_MULTIBLOCKUNDOHEAD) == 0))
                throw RedoLogException(50043, "bad split offset: " + std::to_string(redoLogRecord1->dataOffset) + " xid: " +
                                              transaction->xid.toString() + " second position");

            uint64_t lastSize = *(reinterpret_cast<uint64_t*>(transaction->lastTc->buffer + transaction->lastTc->size - ROW_HEADER_TOTAL + ROW_HEADER_SIZE));
            RedoLogRecord* last501 = reinterpret_cast<RedoLogRecord*>(transaction->lastTc->buffer + transaction->lastTc->size - lastSize + ROW_HEADER_REDO1);
            last501->dataExt = transaction->lastTc->buffer + transaction->lastTc->size - lastSize + ROW_HEADER_DATA;

            uint32_t mergeSize = last501->size + redoLogRecord1->size;
            transaction->mergeBuffer = new uint8_t[mergeSize];
            mergeBlocks(transaction->mergeBuffer, redoLogRecord1, last501);

            typePos fieldPos = redoLogRecord1->fieldPos;
            typeSize fieldSize = ctx->read16(redoLogRecord1->data() + redoLogRecord1->fieldSizesDelta + 1 * 2);
            fieldPos += (fieldSize + 3) & 0xFFFC;

            ctx->write16(redoLogRecord1->data() + fieldPos + 20, redoLogRecord1->flg);
            OpCode0501::process0501(ctx, redoLogRecord1);
            chunkSize = redoLogRecord1->size + redoLogRecord2->size + ROW_HEADER_TOTAL;

            rollbackTransactionChunk(transaction);
            transaction->lastSplit = false;
        }

        // Empty list
        if (transaction->lastTc == nullptr) {
            transaction->lastTc = newTransactionChunk();
            transaction->firstTc = transaction->lastTc;
        }

        // New block needed
        if (unlikely(transaction->lastTc->size + chunkSize > TransactionChunk::DATA_BUFFER_SIZE)) {
            TransactionChunk* tcNew = newTransactionChunk();
            tcNew->prev = transaction->lastTc;
            transaction->lastTc->next = tcNew;
            transaction->lastTc = tcNew;
        }

        // Append to the chunk at the end
        TransactionChunk* tc = transaction->lastTc;
        *(reinterpret_cast<typeOp2*>(tc->buffer + tc->size + ROW_HEADER_OP)) = (redoLogRecord1->opCode << 16) | redoLogRecord2->opCode;
        memcpy(reinterpret_cast<void*>(tc->buffer + tc->size + ROW_HEADER_REDO1),
               reinterpret_cast<const void*>(redoLogRecord1), sizeof(RedoLogRecord));
        memcpy(reinterpret_cast<void*>(tc->buffer + tc->size + ROW_HEADER_REDO2),
               reinterpret_cast<const void*>(redoLogRecord2), sizeof(RedoLogRecord));
        memcpy(reinterpret_cast<void*>(tc->buffer + tc->size + ROW_HEADER_DATA),
               reinterpret_cast<const void*>(redoLogRecord1->data()), redoLogRecord1->size);
        memcpy(reinterpret_cast<void*>(tc->buffer + tc->size + ROW_HEADER_DATA + redoLogRecord1->size),
               reinterpret_cast<const void*>(redoLogRecord2->data()), redoLogRecord2->size);

        *(reinterpret_cast<uint64_t*>(tc->buffer + tc->size + ROW_HEADER_SIZE + redoLogRecord1->size + redoLogRecord2->size)) = chunkSize;

        tc->size += chunkSize;
        ++tc->elements;
        transaction->size += chunkSize;

        if (transaction->mergeBuffer != nullptr) {
            delete[] transaction->mergeBuffer;
            transaction->mergeBuffer = nullptr;
        }
    }

    void TransactionBuffer::rollbackTransactionChunk(Transaction* transaction) {
        if (unlikely(transaction->lastTc == nullptr))
            throw RedoLogException(50044, "trying to remove from empty buffer size: <null> elements: <null>");
        if (unlikely(transaction->lastTc->size < ROW_HEADER_TOTAL || transaction->lastTc->elements == 0))
            throw RedoLogException(50044, "trying to remove from empty buffer size: " + std::to_string(transaction->lastTc->size) +
                                          " elements: " + std::to_string(transaction->lastTc->elements));

        uint64_t chunkSize = *(reinterpret_cast<uint64_t*>(transaction->lastTc->buffer + transaction->lastTc->size - ROW_HEADER_TOTAL + ROW_HEADER_SIZE));
        transaction->lastTc->size -= chunkSize;
        --transaction->lastTc->elements;
        transaction->size -= chunkSize;

        if (unlikely(transaction->lastTc->elements == 0)) {
            TransactionChunk* tc = transaction->lastTc;
            transaction->lastTc = tc->prev;

            if (transaction->lastTc != nullptr) {
                transaction->lastTc->next = nullptr;
            } else {
                transaction->firstTc = nullptr;
            }
            deleteTransactionChunk(tc);
        }
    }

    void TransactionBuffer::mergeBlocks(uint8_t* mergeBuffer, RedoLogRecord* redoLogRecord1, const RedoLogRecord* redoLogRecord2) {
        memcpy(reinterpret_cast<void*>(mergeBuffer),
               reinterpret_cast<const void*>(redoLogRecord1->data()), redoLogRecord1->fieldSizesDelta);
        typePos pos = redoLogRecord1->fieldSizesDelta;
        typeField fieldCnt;
        typePos fieldPos1;
        typePos fieldPos2;

        if ((redoLogRecord1->flg & OpCode::FLG_LASTBUFFERSPLIT) != 0) {
            redoLogRecord1->flg &= ~OpCode::FLG_LASTBUFFERSPLIT;
            typeSize size1 = ctx->read16(redoLogRecord1->data() + redoLogRecord1->fieldSizesDelta + redoLogRecord1->fieldCnt * 2);
            typeSize size2 = ctx->read16(redoLogRecord2->data() + redoLogRecord2->fieldSizesDelta + 6);
            ctx->write16(redoLogRecord2->data() + redoLogRecord2->fieldSizesDelta + 6, size1 + size2);
            --redoLogRecord1->fieldCnt;
        }

        // Field list
        fieldCnt = redoLogRecord1->fieldCnt + redoLogRecord2->fieldCnt - 2;
        ctx->write16(mergeBuffer + pos, fieldCnt);
        memcpy(reinterpret_cast<void*>(mergeBuffer + pos + 2),
               reinterpret_cast<const void*>(redoLogRecord1->data() + redoLogRecord1->fieldSizesDelta + 2), redoLogRecord1->fieldCnt * 2);
        memcpy(reinterpret_cast<void*>(mergeBuffer + pos + 2 + redoLogRecord1->fieldCnt * 2),
               reinterpret_cast<const void*>(redoLogRecord2->data() + redoLogRecord2->fieldSizesDelta + 6), redoLogRecord2->fieldCnt * 2 - 4);
        pos += (((fieldCnt + 1) * 2) + 2) & (0xFFFC);
        fieldPos1 = pos;

        memcpy(reinterpret_cast<void*>(mergeBuffer + pos),
               reinterpret_cast<const void*>(redoLogRecord1->data() + redoLogRecord1->fieldPos), redoLogRecord1->size - redoLogRecord1->fieldPos);
        pos += (redoLogRecord1->size - redoLogRecord1->fieldPos + 3) & (0xFFFC);
        fieldPos2 = redoLogRecord2->fieldPos +
                    ((ctx->read16(redoLogRecord2->data() + redoLogRecord2->fieldSizesDelta + 2) + 3) & 0xFFFC) +
                    ((ctx->read16(redoLogRecord2->data() + redoLogRecord2->fieldSizesDelta + 4) + 3) & 0xFFFC);

        memcpy(reinterpret_cast<void*>(mergeBuffer + pos),
               reinterpret_cast<const void*>(redoLogRecord2->data() + fieldPos2), redoLogRecord2->size - fieldPos2);
        pos += (redoLogRecord2->size - fieldPos2 + 3) & (0xFFFC);

        redoLogRecord1->size = pos;
        redoLogRecord1->fieldCnt = fieldCnt;
        redoLogRecord1->fieldPos = fieldPos1;
        redoLogRecord1->dataExt = mergeBuffer;
        redoLogRecord1->flg |= redoLogRecord2->flg;
        if ((redoLogRecord1->flg & OpCode::FLG_MULTIBLOCKUNDOTAIL) != 0)
            redoLogRecord1->flg &= ~(OpCode::FLG_MULTIBLOCKUNDOHEAD | OpCode::FLG_MULTIBLOCKUNDOMID | OpCode::FLG_MULTIBLOCKUNDOTAIL);
    }

    void TransactionBuffer::checkpoint(typeSeq& minSequence, uint64_t& minOffset, typeXid& minXid) {
        for (auto xidTransactionMapIt: xidTransactionMap) {
            const Transaction* transaction = xidTransactionMapIt.second;
            if (transaction->firstSequence < minSequence) {
                minSequence = transaction->firstSequence;
                minOffset = transaction->firstOffset;
                minXid = transaction->xid;
            } else if (transaction->firstSequence == minSequence && transaction->firstOffset < minOffset) {
                minOffset = transaction->firstOffset;
                minXid = transaction->xid;
            }
        }
    }

    void TransactionBuffer::addOrphanedLob(RedoLogRecord* redoLogRecord1) {
        if (unlikely(ctx->trace & Ctx::TRACE_LOB))
            ctx->logTrace(Ctx::TRACE_LOB, "id: " + redoLogRecord1->lobId.upper() + " page: " + std::to_string(redoLogRecord1->dba) +
                                          " can't match, offset: " + std::to_string(redoLogRecord1->dataOffset));

        LobKey lobKey(redoLogRecord1->lobId, redoLogRecord1->dba);

        if (orphanedLobs.find(lobKey) != orphanedLobs.end()) {
            ctx->warning(60009, "duplicate orphaned lob: " + redoLogRecord1->lobId.lower() + ", page: " +
                                std::to_string(redoLogRecord1->dba));
            return;
        }

        orphanedLobs.emplace(lobKey, allocateLob(redoLogRecord1));
    }

    Lob TransactionBuffer::allocateLob(const RedoLogRecord* redoLogRecord1) const {
        return Lob(redoLogRecord1);
    }
}
