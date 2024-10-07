#include "BuilderBuffer.h"
/* Memory buffer for handling output data
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

#include "BuilderBuffer.h"
#include "../common/types.h"

namespace OpenLogReplicator {

    BuilderBuffer::BuilderBuffer(Ctx *newCtx) :
        ctx(newCtx),
        firstChunk(nullptr),
        lastChunk(nullptr),
        chunksAllocated(0) {
        
    }

    BuilderBuffer::~BuilderBuffer()
    {
        while (firstChunk != nullptr) {
            BuilderQueue* nextBuffer = firstChunk->next;
            ctx->freeMemoryChunk(Ctx::MEMORY_MODULE_BUILDER, reinterpret_cast<uint8_t*>(firstChunk), true);
            firstChunk = nextBuffer;
            --chunksAllocated;
        }

        assert(chunksAllocated == 0);
    }

    void BuilderBuffer::releaseBuffers(uint64_t maxId) {
        BuilderQueue* builderQueue;
        {
            std::unique_lock<std::mutex> lck(mtx);
            builderQueue = firstChunk;
            while (firstChunk->id < maxId) {
                firstChunk = firstChunk->next;
            }
        }

        if (builderQueue != nullptr) {
            while (builderQueue->id < maxId) {
                BuilderQueue* nextBuffer = builderQueue->next;
                ctx->freeMemoryChunk(Ctx::MEMORY_MODULE_BUILDER, reinterpret_cast<uint8_t*>(builderQueue), true);
                builderQueue = nextBuffer;
                --chunksAllocated;
            }
        }
    }

    void BuilderBuffer::initialize() {
        void* memoryChunk = ctx->getMemoryChunk(Ctx::MEMORY_MODULE_BUILDER, true);
        chunksAllocated = 1;

        std::memset(reinterpret_cast<void*>(memoryChunk), 0, sizeof(BuilderQueue));
        lastChunk = firstChunk = reinterpret_cast<BuilderQueue*>(memoryChunk);
        firstChunk->data = reinterpret_cast<uint8_t*>(firstChunk) + sizeof(struct BuilderQueue);
    }

    void BuilderBuffer::expand(bool copy) {
        void* memoryChunk = ctx->getMemoryChunk(Ctx::MEMORY_MODULE_BUILDER, true);

        auto nextChunk = reinterpret_cast<BuilderQueue*>(memoryChunk);
        nextChunk->next = nullptr;
        nextChunk->id = lastChunk->id + 1;
        nextChunk->data = reinterpret_cast<uint8_t*>(nextChunk) + sizeof(struct BuilderQueue);

        // Message could potentially fit in one buffer
        if (likely(copy && msg != nullptr && messageSize + messagePosition < OUTPUT_BUFFER_DATA_SIZE)) {
            memcpy(reinterpret_cast<void*>(nextChunk->data), msg, messagePosition);
            msg = reinterpret_cast<BuilderMsg*>(nextChunk->data);
            msg->data = nextChunk->data + sizeof(struct BuilderMsg);
            nextChunk->start = 0;
        } else {
            lastChunk->size += messagePosition;
            messageSize += messagePosition;
            messagePosition = 0;
            nextChunk->start = BUFFER_START_UNDEFINED;
        }
        nextChunk->size = 0;

        {
            std::unique_lock<std::mutex> lck(mtx);
            lastChunk->next = nextChunk;
            ++chunksAllocated;
            lastChunk = nextChunk;
        }
    }
}
