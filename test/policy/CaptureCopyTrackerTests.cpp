// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "CaptureCopyTracker.h"
using namespace melonDS;

namespace {
std::vector<u8> ImageBytes(u8 value)
{
    std::vector<u8> result(CaptureCopyTracker::ImageBytes);
    for (unsigned y=0;y<192;++y)
        std::fill(result.begin()+y*512,result.begin()+(y+1)*512,static_cast<u8>(value+y));
    return result;
}
}

POLICY_TEST(CaptureCopiesPreserveExactRowsAcrossRepeatedDestinationWrites)
{
    CaptureCopyTracker cache;auto image=ImageBytes(7);
    const auto slot=cache.Store(0x18000,image.data());
    CopiedCaptureFrameProof frame;
    for(unsigned y=0;y<192;++y) {
        cache.NoteCopy(0x18000+y*512,0x06204000,512);
        const auto row=cache.Match(0x06204000,image.data()+y*512);
        CHECK(row.has_value());CHECK_EQ(row->Slot,slot);CHECK_EQ(row->Y,y);
        frame.Observe(y,row);frame.Observe(y,row);
    }
    CHECK(frame.Complete());
    auto changed=image;changed[191*512+511]^=1;
    CHECK(!cache.Match(0x06204000,changed.data()+191*512));
}

POLICY_TEST(CaptureCopiesRejectPartialUnalignedUnknownAndOverwrittenRows)
{
    CaptureCopyTracker cache;auto image=ImageBytes(3);cache.Store(0,image.data());
    for(unsigned bytes:{0u,256u,511u,513u,1024u}) {
        cache.NoteCopy(0,0x06204000,bytes);CHECK(!cache.Match(0x06204000,image.data()));
    }
    for(unsigned source:{1u,511u,CaptureCopyTracker::ImageBytes,0xFFFFFFFFu}) {
        cache.NoteCopy(source,0x06204000,512);CHECK(!cache.Match(0x06204000,image.data()));
    }
    cache.NoteCopy(0,0x06204000,512);CHECK(cache.Match(0x06204000,image.data()).has_value());
    cache.NoteCopy(0xFFFFFFFFu,0x06204000+8,4);CHECK(!cache.Match(0x06204000,image.data()));
}

POLICY_TEST(CaptureCopiesInvalidateReusedEvictedResetAndOverlappingRAM)
{
    CaptureCopyTracker cache;auto image=ImageBytes(1);
    const auto slot=cache.Store(0x10000,image.data());const auto old=cache.Get(slot).Generation;
    cache.NoteCopy(0x10000,0x06204000,512);cache.Store(0x10000,image.data());
    CHECK(cache.Get(slot).Generation!=old);CHECK(!cache.Match(0x06204000,image.data()));
    cache.NoteCopy(0x10000,0x06204000,512);
    cache.InvalidateRAMRange(0x10000+512,1);CHECK(!cache.Match(0x06204000,image.data()));
    for(unsigned n=0;n<CaptureCopyTracker::Capacity+1;++n)cache.Store(n*0x20000,image.data());
    cache.NoteCopy(0,0x06204000,512);CHECK(!cache.Match(0x06204000,image.data()));
    cache.NoteCopy(0x80000,0x06204000,512);CHECK(cache.Match(0x06204000,image.data()).has_value());
    cache.Reset();CHECK(!cache.Match(0x06204000,image.data()));
}

POLICY_TEST(CaptureFrameProofRejectsMissingReorderedMixedAndChangedRows)
{
    for(int kind=0;kind<5;++kind) {
        CopiedCaptureFrameProof frame;
        for(unsigned y=0;y<192;++y) {
            std::optional<CaptureCopyTracker::Row> row=CaptureCopyTracker::Row{0,1,y};
            if(y==80) {
                if(kind==0)row.reset();
                if(kind==1)row->Y=81;
                if(kind==2)row->Generation=2;
                if(kind==3)row->Slot=1;
                if(kind==4)continue;
            }
            frame.Observe(y,row);
        }
        CHECK(!frame.Complete());CHECK(frame.Rejected);
    }
}

POLICY_TEST(CaptureFrameProofCannotBeReusedAfterAnUndrawnFrame)
{
    CopiedCaptureFrameProof frame;
    for (unsigned y=0;y<192;++y) frame.Observe(y,CaptureCopyTracker::Row{0,1,y});
    CHECK(frame.Consume());
    CHECK(!frame.Consume()); // VBlank without a new scanline zero.
    CHECK(!frame.Complete());
    frame = {};
    for (unsigned y=0;y<192;++y) frame.Observe(y,CaptureCopyTracker::Row{0,2,y});
    CHECK(frame.Consume());
}
