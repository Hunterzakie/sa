/*
 * HLSSegment.cpp
 *****************************************************************************
 * Copyright (C) 2015 VideoLAN and VLC authors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "HLSSegment.hpp"
#include "../../adaptive/playlist/BaseRepresentation.h"


using namespace hls::playlist;

HLSSegment::HLSSegment( ICanonicalUrl *parent, uint64_t seq ) :
    Segment( parent )
{
    setSequenceNumber(seq);
}

HLSSegment::~HLSSegment()
{
}

bool HLSSegment::prepareChunk(SharedResources *res, SegmentChunk *chunk, BaseRepresentation *rep)
{
    if(encryption.method == CommonEncryption::Method::AES_128)
    {
        if (encryption.iv.size() != 16)
        {
            /* no explicit IV: derive from the 64-bit media sequence (RFC 8216 5.2) */
            encryption.iv.assign(16, 0);
            SetQWBE(&encryption.iv[8], getSequenceNumber());
        }
    }

    return Segment::prepareChunk(res, chunk, rep);
}
