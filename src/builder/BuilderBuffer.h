/* Header for BuilderBuffer class
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

#include <mutex>
#include <atomic>

#include "../common/Ctx.h"

#ifndef BUILDER_BUFFER_H_
#define BUILDER_BUFFER_H_

namespace OpenLogReplicator {
    class Ctx;

    struct BuilderChunkHeader {
        uint64_t id;
        std::atomic<uint64_t> size;
        std::atomic<uint64_t> start;
        uint8_t* data;
        std::atomic<BuilderChunkHeader*> next;
    };

    struct BuilderMessageHeader {
        void* ptr;
        uint64_t id;
        uint64_t queueId;
        std::atomic<uint64_t> size;
        typeScn scn;
        typeScn lwnScn;
        typeIdx lwnIdx;
        uint8_t* data;
        typeSeq sequence;
        typeObj obj;
        uint16_t pos;
        uint16_t flags;

        std::string ToString() const {
            return "id: " + std::to_string(id) + " size: " + std::to_string(size.load()) + " scn: " + std::to_string(scn) 
                    + " lwnScn: " + std::to_string(lwnScn) + " lwnIdx: " + std::to_string(lwnIdx)
                    + " sequence: " + std::to_string(sequence) + " obj: " + std::to_string(obj)
                    ;
        }
    };

    struct BuilderMessage {
        BuilderMessageHeader* header;
        uint64_t size;
        uint64_t position;
    };

    class BuilderBuffer {

        static constexpr uint64_t OUTPUT_BUFFER_DATA_SIZE = Ctx::MEMORY_CHUNK_SIZE - sizeof(struct BuilderChunkHeader);
        static constexpr uint64_t BUFFER_START_UNDEFINED = 0xFFFFFFFFFFFFFFFF;

    public:
    
        BuilderBuffer(Ctx* newCtx);
        ~BuilderBuffer();

        void initialize();
        void expand(bool, BuilderMessage&);
        void releaseBuffers(uint64_t);

        const BuilderChunkHeader& begin() const;
        BuilderChunkHeader& begin();
        const BuilderChunkHeader& end() const;
        BuilderChunkHeader& end();

    private:
        Ctx* const ctx;

        BuilderChunkHeader* firstChunk;
        BuilderChunkHeader* lastChunk;
        uint64_t chunksAllocated;
    
        std::mutex mtx;
    };

}

#endif 
