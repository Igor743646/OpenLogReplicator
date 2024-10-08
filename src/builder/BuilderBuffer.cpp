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
            BuilderChunkHeader* nextChunk = firstChunk->next;
            ctx->freeMemoryChunk(Ctx::MEMORY_MODULE_BUILDER, reinterpret_cast<uint8_t*>(firstChunk), true);
            firstChunk = nextChunk;
            --chunksAllocated;
        }

        assert(chunksAllocated == 0);
    }

    void BuilderBuffer::releaseBuffers(uint64_t maxId) {
        BuilderChunkHeader* chunk;
        {
            std::unique_lock<std::mutex> lck(mtx);
            chunk = firstChunk;
            while (firstChunk->id < maxId) {
                firstChunk = firstChunk->next;
            }
        }

        if (chunk != nullptr) {
            while (chunk->id < maxId) {
                BuilderChunkHeader* nextBuffer = chunk->next;
                ctx->freeMemoryChunk(Ctx::MEMORY_MODULE_BUILDER, reinterpret_cast<uint8_t*>(chunk), true);
                chunk = nextBuffer;
                --chunksAllocated;
            }
        }
    }

    const BuilderChunkHeader& BuilderBuffer::begin() const {
        return *reinterpret_cast<BuilderChunkHeader*>(firstChunk);
    }

    BuilderChunkHeader& BuilderBuffer::begin() {
        return *reinterpret_cast<BuilderChunkHeader*>(firstChunk);
    }

    const BuilderChunkHeader& BuilderBuffer::end() const {
        return *reinterpret_cast<BuilderChunkHeader*>(lastChunk);
    }

    BuilderChunkHeader& BuilderBuffer::end() {
        return *reinterpret_cast<BuilderChunkHeader*>(lastChunk);
    }

    void BuilderBuffer::initialize() {
        void* memoryChunk = ctx->getMemoryChunk(Ctx::MEMORY_MODULE_BUILDER, true);
        chunksAllocated = 1;

        std::memset(reinterpret_cast<void*>(memoryChunk), 0, sizeof(BuilderChunkHeader));
        lastChunk = firstChunk = reinterpret_cast<BuilderChunkHeader*>(memoryChunk);
        firstChunk->data = reinterpret_cast<uint8_t*>(firstChunk) + sizeof(struct BuilderChunkHeader);
    }

    void BuilderBuffer::expand(bool copy, BuilderMessage& message) {
        void* memoryChunk = ctx->getMemoryChunk(Ctx::MEMORY_MODULE_BUILDER, true);

        auto nextChunk = reinterpret_cast<BuilderChunkHeader*>(memoryChunk);
        nextChunk->next = nullptr;
        nextChunk->id = lastChunk->id + 1;
        nextChunk->data = reinterpret_cast<uint8_t*>(nextChunk) + sizeof(struct BuilderChunkHeader);

        // Message could potentially fit in one buffer
        if (likely(copy && message.header != nullptr && message.size + message.position < OUTPUT_BUFFER_DATA_SIZE)) {
            memcpy(reinterpret_cast<void*>(nextChunk->data), message.header, message.position);
            message.header = reinterpret_cast<BuilderMessageHeader*>(nextChunk->data);
            message.header->data = nextChunk->data + sizeof(struct BuilderMessageHeader);
            nextChunk->start = 0;
        } else {
            lastChunk->size += message.position;
            message.size += message.position;
            message.position = 0;
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
