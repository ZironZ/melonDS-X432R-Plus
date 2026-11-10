// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "WideGamePatches.h"
#include "WideMelon.h"
#include "frontend/ScreenLayout.h"
#include <cmath>
#include <map>

using namespace melonDS;

POLICY_TEST(CommunityCatalogPreservesPublishedProgramsAndRatioCapabilities)
{
    CHECK_EQ(std::size(WideGamePatches::CommunityProfiles),542u);
    CHECK_EQ(sizeof(WideGamePatches::CommunityWords),29536u);
    unsigned custom = 0;
    for (const auto& profile : WideGamePatches::CommunityProfiles)
    {
        CHECK(WideGamePatches::FindCommunity(profile.ID,profile.HeaderCRC)==&profile);
        const auto original=WideGamePatches::BuildCommunityCode(profile,16,10);
        CHECK(WideGamePatches::ValidProgram(original));
        CHECK_EQ(original.size(),profile.Words);
        for (size_t i=0;i<original.size();++i) CHECK_EQ(original[i],WideGamePatches::CommunityWords[profile.Offset+i]);
        if (profile.CustomAspect)
        {
            ++custom;
            for (const auto ratio : {std::array<int,2>{16,9},{21,9},{32,9},{7,4},{4,3}})
            {
                const auto code=WideGamePatches::BuildCommunityCode(profile,ratio[0],ratio[1]);
                CHECK_EQ(code.size(),original.size());
                for(size_t i=0;i<code.size();++i)
                    CHECK_EQ(code[i],i%6==3?WideGamePatches::AspectQ12(ratio[0],ratio[1]):original[i]);
            }
        }
        else
        {
            CHECK(WideGamePatches::BuildCommunityCode(profile,16,9).empty());
            CHECK(WideGamePatches::BuildCommunityCode(profile,32,9).empty());
        }
    }
    CHECK_EQ(custom,334u);
}

POLICY_TEST(CommunityMatchingUsesExactRevisionBeforePublishedWildcard)
{
    const auto* mk=WideGamePatches::FindCommunity("AMCE",0xDEF2);
    CHECK(mk && mk->CustomAspect);
    CHECK(!WideGamePatches::FindCommunity("AMCE",0x6600)); // Empty package is not a usable profile.
    CHECK(!WideGamePatches::FindCommunity("ZZZZ",0xFFFF));
    const auto* wildcard=WideGamePatches::FindCommunity("YV5E",0x1234);
    CHECK(wildcard && wildcard->HeaderCRC==0xFFFF);
    CHECK(!WideGamePatches::FindCommunity("AMCE",0x1234));
}

POLICY_TEST(CommunityRatioAndPayloadValidationRejectUnsupportedInputs)
{
    CHECK_EQ(WideGamePatches::AspectQ12(16,9),0x1C72u);
    CHECK_EQ(WideGamePatches::AspectQ12(21,9),0x2555u);
    CHECK_EQ(WideGamePatches::AspectQ12(32,9),0x38E4u);
    for (const auto bad : {std::array<int,2>{0,9},{16,0},{33,9},{3,4},{32,1},{-1,3}})
        CHECK_EQ(WideGamePatches::AspectQ12(bad[0],bad[1]),0u);
    CHECK(!WideGamePatches::ValidProgram({}));
    CHECK(!WideGamePatches::ValidProgram({0xD2000000}));
    CHECK(!WideGamePatches::ValidProgram({0xE2000000,8}));
    CHECK(!WideGamePatches::ValidProgram({0xE2000000,0xFFFFFFFF}));
    CHECK(!WideGamePatches::ValidProgram({0xC4000000,0}));
    CHECK(!WideGamePatches::ValidProgram({0xCF000000,0}));
    CHECK(!WideGamePatches::ValidProgram({0xDD000000,0}));
    CHECK(!WideGamePatches::ValidProgram({0xD4000009,0}));
    CHECK(WideGamePatches::ValidProgram({0xE2000000,1,0x1234,0}));
}

POLICY_TEST(GamePatchMethodNeverExpandsRendererProjection)
{
    const auto saved=WideMelon::CurrentConfiguration();
    const auto patch=WideMelon::ResolveConfiguration(682,WideMelon::Both,288,true,true,
        true,16,9,150,WideMelon::GamePatch,21,9,true);
    WideMelon::ApplyConfiguration(patch);
    CHECK(!WideMelon::Enabled()); CHECK(WideMelon::NeedsPresentation());
    CHECK_EQ(WideMelon::Width(),256); CHECK_EQ(WideMelon::Height(),192);
    CHECK_EQ(WideMelon::CaptureOverlap(),0); CHECK_EQ(WideMelon::VerticalCaptureOverlap(),0);
    CHECK_EQ(WideMelon::ProjectX(123456),123456); CHECK_EQ(WideMelon::ProjectY(123456),123456);
    CHECK_EQ(WideMelon::DisplayWidth(1),256); // Touch uses the entire stretched logical display.
    const auto renderer=WideMelon::ResolveConfiguration(342,WideMelon::Top,192,true,false,true,16,9,150);
    CHECK_EQ(renderer.Width,512); CHECK_EQ(renderer.Height,288);
    CHECK(renderer!=patch);
    const auto off=WideMelon::ResolveConfiguration(682,WideMelon::Both,288,true,true,false,4,3,100,WideMelon::Off);
    CHECK(!off.NeedsPresentation()); CHECK_EQ(off.Width,256); CHECK_EQ(off.Height,192);
    WideMelon::ApplyConfiguration(saved);
}

POLICY_TEST(StretchedGamePatchTouchMapsWholeNativePictureAfterRotation)
{
    for (int rotation=0;rotation<4;++rotation) for (int kind=0;kind<4;++kind)
    for (float stretch : {1.2f,4.f/3.f,8.f/3.f})
    {
        ScreenLayout layout;
        layout.Setup(2400,1800,ScreenLayoutType(kind),ScreenRotation(rotation),screenSizing_Even,8,false,false,1.f,stretch,true);
        float matrices[kMaxScreenTransforms*6]; int screens[kMaxScreenTransforms];
        const int count=layout.GetScreenTransforms(matrices,screens);
        for(int i=0;i<count;++i) if(screens[i]==1)
        {
            const auto* m=matrices+i*6;
            for(int x : {1,16,128,240,254}) for(int y : {1,96,190})
            {
                int hx=int(std::lround((x+0.5f)*m[0]+(y+0.5f)*m[2]+m[4]));
                int hy=int(std::lround((x+0.5f)*m[1]+(y+0.5f)*m[3]+m[5]));
                CHECK(layout.GetTouchCoords(hx,hy,false));
                CHECK(std::abs(hx-x)<=1); CHECK(std::abs(hy-y)<=1);
            }
        }
    }
}

POLICY_TEST(MarioKartTypedComponentPreservesUserWordsAndAvoidsRepeatedInvalidation)
{
    std::map<u32,u32> memory;
    for (const auto& p:WideGamePatches::MarioKartCull) memory[p.Address]=p.Original;
    unsigned writes=0;
    auto read=[&](u32 a){return memory[a];};
    auto write=[&](u32 a,u32 v){memory[a]=v;++writes;};
    CHECK_EQ(WideGamePatches::ApplyMarioKartCull(false,read,write),0u);
    memory[WideGamePatches::MarioKartCull[0].Address]=0x12345678;
    CHECK_EQ(WideGamePatches::ApplyMarioKartCull(true,read,write),0x7Eu);
    CHECK_EQ(writes,6u);
    CHECK_EQ(memory[WideGamePatches::MarioKartCull[0].Address],0x12345678u);
    CHECK_EQ(WideGamePatches::ApplyMarioKartCull(true,read,write),0u); CHECK_EQ(writes,6u);
    const auto& overlay=WideGamePatches::MarioKartCull[2]; memory[overlay.Address]=overlay.Original;
    CHECK_EQ(WideGamePatches::ApplyMarioKartCull(true,read,write),4u); CHECK_EQ(writes,7u);
}

POLICY_TEST(MarioKartRacerFrustumSiteHonorsConflictsAndCodeReloads)
{
    const auto& profile=WideGamePatches::VisibilityProfiles[0];
    CHECK_EQ(profile.Count,7u);
    const auto& racer=profile.Patches[6];
    std::map<u32,u32> memory;
    for(unsigned i=0;i<profile.Count;++i) memory[profile.Patches[i].Address]=profile.Patches[i].Replacement;
    unsigned writes=0;
    auto read=[&](u32 a) { return memory[a]; };
    auto write=[&](u32 a,u32 v) { memory[a]=v; ++writes; };
    memory[racer.Address]=0xE1A00000; // Preserve another cheat at this site.
    CHECK_EQ(WideGamePatches::ApplyVisibilityFix(profile,0x02000000,read,write),0u);
    CHECK_EQ(writes,0u);
    memory[racer.Address]=racer.Original; // Original code restored by a state/reload.
    CHECK_EQ(WideGamePatches::ApplyVisibilityFix(profile,0x02000000,read,write),0x40u);
    CHECK_EQ(writes,1u); CHECK_EQ(memory[racer.Address],racer.Replacement);
    CHECK_EQ(WideGamePatches::ApplyVisibilityFix(profile,0x02000000,read,write),0u);
    CHECK_EQ(writes,1u);
}

POLICY_TEST(VisibilityProfilesRejectOtherBuildsAndNativeViews)
{
    CHECK_EQ(std::size(WideGamePatches::VisibilityProfiles),4u);
    for (const auto& p : WideGamePatches::VisibilityProfiles)
    {
        CHECK(WideGamePatches::FindVisibilityCandidate(p.GameCode,p.Revision,p.HeaderCRC,p.UsedSize)==&p);
        CHECK(!WideGamePatches::FindVisibilityCandidate(p.GameCode,p.Revision+1,p.HeaderCRC,p.UsedSize));
        CHECK(!WideGamePatches::FindVisibilityCandidate(p.GameCode,p.Revision,p.HeaderCRC^1,p.UsedSize));
        CHECK(!WideGamePatches::FindVisibilityCandidate(p.GameCode,p.Revision,p.HeaderCRC,p.UsedSize+1));
        CHECK(!WideGamePatches::FindVisibilityCandidate("ZZZZ",p.Revision,p.HeaderCRC,p.UsedSize));
    }
    CHECK(!WideGamePatches::SupportsVisibilityView(256,192));
    for (const auto view : {std::array<int,2>{308,192},{342,192},{448,192},{682,192},
                           {336,192},{512,288},{384,288},{256,342},{768,768}})
        CHECK(WideGamePatches::SupportsVisibilityView(view[0],view[1]));
}

POLICY_TEST(VisibilityReturnStubsAreGuardedAsOneUnitAndReapplyAfterReload)
{
    for (const auto& profile : WideGamePatches::VisibilityProfiles)
    {
        if (!profile.PredicateEntry) continue;
        std::map<u32,u32> memory;
        unsigned writes=0;
        auto read=[&](u32 a) { return memory[a]; };
        auto write=[&](u32 a,u32 v) { memory[a]=v; ++writes; };
        auto restore=[&] { for(unsigned i=0;i<profile.Count;++i) memory[profile.Patches[i].Address]=profile.Patches[i].Original; };
        restore();
        // Defer while the CPU might already have executed/prefetched the push.
        for (u32 offset : {0u,4u,8u,12u})
            CHECK_EQ(WideGamePatches::ApplyVisibilityFix(profile,profile.Patches[0].Address+offset,read,write),0u);
        CHECK_EQ(writes,0u);
        CHECK_EQ(WideGamePatches::ApplyVisibilityFix(profile,0x02000000,read,write),3u);
        CHECK_EQ(writes,2u);
        CHECK_EQ(WideGamePatches::ApplyVisibilityFix(profile,0x02000000,read,write),0u);
        CHECK_EQ(writes,2u);
        restore();
        CHECK_EQ(WideGamePatches::ApplyVisibilityFix(profile,0x02000000,read,write),3u);
        CHECK_EQ(writes,4u);
        // A different cheat, overlay, or partially patched state must never
        // leave a pushed stack paired with our early return.
        for(unsigned conflict=0;conflict<profile.Count;++conflict)
        {
            restore(); memory[profile.Patches[conflict].Address]=0xE1A00000;
            CHECK_EQ(WideGamePatches::ApplyVisibilityFix(profile,0x02000000,read,write),0u);
            CHECK_EQ(writes,4u);
            restore(); memory[profile.Patches[conflict].Address]=profile.Patches[conflict].Replacement;
            CHECK_EQ(WideGamePatches::ApplyVisibilityFix(profile,0x02000000,read,write),0u);
            CHECK_EQ(writes,4u);
        }
    }
}

POLICY_TEST(ZeldaSceneryWindowSupportsSavedStatesAndRejectsMixedCode)
{
    std::map<u32,u32> memory;
    unsigned writes=0;
    auto read=[&](u32 a) { return memory[a]; };
    auto write=[&](u32 a,u32 v) { memory[a]=v; ++writes; };
    auto restore=[&] { for (const auto& p : WideGamePatches::ZeldaCameraSites) memory[p.Address]=p.Original; };
    auto apply=[&](int w,int h,u32 pc=0x02000000) {
        return WideGamePatches::ApplyZeldaCameraFix(w,h,pc,read,write);
    };
    restore();
    // State 3 already contains the old sphere bypass. It must not gate the
    // independent camera window fix or be overwritten by it.
    for (const auto& p : WideGamePatches::ZeldaCull) memory[p.Address]=p.Replacement;
    CHECK(!apply(256,192)); CHECK(!apply(770,288)); CHECK(!apply(383,288));
    for (u32 pc : {0x0208A404u,0x0208A488u,0x0208A610u,0x0208A7E4u})
        CHECK(!apply(384,288,pc));
    CHECK_EQ(writes,0u);
    CHECK(apply(384,288));
    // Independently reviewed ARM words from the working 150% replay.
    CHECK_EQ(memory[0x0208A488],0xE3E0003Fu); // x=-64
    CHECK_EQ(memory[0x0208A4AC],0xE3E0102Fu); // y=-48
    CHECK_EQ(memory[0x0208A4F4],0xE3A00D05u); // x=320 (319 rounded outward)
    CHECK_EQ(memory[0x0208A530],0xE3A010EFu); // y=239
    CHECK_EQ(memory[0x0208A4B4],0xE58D2014u); // zero argument, not negative x
    CHECK_EQ(memory[0x02088014],0xE3A00001u);
    const unsigned initial=writes;
    CHECK(!apply(384,288)); CHECK_EQ(writes,initial);
    // Complete patches saved at other view sizes can be replaced safely.
    CHECK(apply(768,768)); CHECK(apply(342,192)); CHECK(apply(384,288));
    const auto saved=memory;
    for (const auto& p : WideGamePatches::ZeldaCameraSites)
    {
        restore(); memory[p.Address]=0xE1A01001;
        const unsigned before=writes;
        CHECK(!apply(384,288)); CHECK_EQ(writes,before);
        memory=saved; memory[p.Address]=0xE1A01001;
        CHECK(!apply(512,288)); CHECK_EQ(writes,before);
    }
    restore(); CHECK(apply(384,288)); // original code reloaded from state
}

POLICY_TEST(ZeldaSceneryCornerEncodingNeverShrinksSupportedViews)
{
    auto decode=[](u32 word) {
        u32 value=WideGamePatches::RotateRight(word&255,((word>>8)&15)*2);
        return (word&0x00400000) ? s32(~value) : s32(value);
    };
    for (int extent=192; extent<=768; extent+=2)
    {
        const int margin=(extent-192)/2;
        const int near=decode(WideGamePatches::CornerMove(-margin));
        const int far=decode(WideGamePatches::CornerMove(191+margin));
        CHECK(near <= -margin && near >= -margin-3);
        CHECK(far >= 191+margin && far <= 191+margin+3);
    }
}
