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

#include "Builder.h"

#ifndef BUILDER_BUFFER_H_
#define BUILDER_BUFFER_H_

namespace OpenLogReplicator {

    struct BuilderBufferMessage {
        uint64_t position;
        uint64_t size; 
    };

    class BuilderBuffer {

        static constexpr uint64_t BUFFER_START_UNDEFINED = 0xFFFFFFFFFFFFFFFF;

    public:
    
        BuilderBuffer(Ctx* newCtx);
        ~BuilderBuffer();

        void initialize();
        void expand(bool);
        void releaseBuffers(uint64_t);

    private:
        Ctx* const ctx;

        uint64_t chunksAllocated;
        BuilderQueue* firstChunk;
        BuilderQueue* lastChunk;
    
        std::mutex mtx;
    };

} // end namespace OpenLogReplicator

#endif 
