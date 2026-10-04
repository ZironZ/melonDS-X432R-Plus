// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "GPU3D_TextureReconstruction.h"
#include "GPU3D_Texture2DPolicy.h"
using namespace melonDS;
POLICY_TEST(TextureReconstructionProjectsNegativeSourceCoordinatesWithFloor)
{
    using Source = Polygon::UnclippedQuadSource;
    CHECK(Source::ProjectAxis(-17749,16384,192,0)==-128);
    CHECK(Source::ProjectAxis(-17749,16384,192,8)==0);
    CHECK(Source::ProjectAxis(-15018,16384,192,0)==128);
    CHECK(Source::ProjectAxis(-20480,16384,64,0)==-128);
    CHECK(Source::ProjectAxis(-16384,16384,192,0)==0);
    // Retain subpixel positions; do not snap arbitrary geometry to whole pixels.
    CHECK(Source::ProjectAxis(-17738,16384,192,0)==-127);
}
POLICY_TEST(TextureReconstructionExtendsUnknownDomainNotAuthoredTransparency)
{
    std::vector<u32> pixels(25, 0);
    std::vector<u8> domain(25, 0);
    pixels[11] = 0xFF123456; domain[11] = 1;
    pixels[12] = 0; domain[12] = 1;
    pixels[13] = 0xFF789ABC; domain[13] = 1;
    ExtendTextureReconstructionDomain(5,5,pixels,domain);
    CHECK(pixels[0] == 0xFF123456);
    CHECK(pixels[4] == 0xFF789ABC);
    CHECK(pixels[12] == 0);
    CHECK(pixels[2] == 0);
    CHECK(pixels[11] == 0xFF123456);
    CHECK(pixels[13] == 0xFF789ABC);
}
namespace
{
struct Quad
{
    Polygon P {};
    std::array<Vertex,4> V {};
    Quad(int x, int y, int u, int v)
    {
        P.NumVertices = 4; P.Attr = 0x1F00C0; P.TexParam = (4u<<26)|(3u<<20)|(3u<<23)|(1u<<29);
        const int dx[] = {0,0,8,8}, dy[] = {0,8,8,0};
        for (int i=0;i<4;i++)
        {
            P.Vertices[i]=&V[i]; P.FinalZ[i]=100; P.FinalW[i]=4096;
            V[i].FinalPosition[0]=x+dx[i]; V[i].FinalPosition[1]=y+dy[i];
            V[i].HiresPosition[0]=(x+dx[i])*16; V[i].HiresPosition[1]=(y+dy[i])*16;
            V[i].TexCoords[0]=s16((u+dx[i])*16); V[i].TexCoords[1]=s16((v+dy[i])*16);
            for (auto& c:V[i].FinalColor) c=511;
        }
    }
};
}
POLICY_TEST(TextureReconstructionConnectsScreenNeighborsNotAtlasNeighbors)
{
    Quad a(0,0,32,8), b(8,0,0,32), c(32,0,40,8);
    auto p=BuildTextureReconstructionPlan({&a.P,&b.P,&c.P});
    CHECK(p.Groups.size()==1); CHECK(p.Groups[0].Pieces.size()==2);
    CHECK(p.Membership[0]==0); CHECK(p.Membership[1]==0); CHECK(p.Membership[2]==-1);
    Polygon copy; std::array<Vertex,4> vertices;
    RemapTextureReconstructionPolygon(b.P,p.Groups[0],copy,vertices);
    CHECK(copy.Vertices[0]->TexCoords[0]==160); CHECK(b.V[0].TexCoords[0]==0);
    CHECK(copy.FinalZ[0]==b.P.FinalZ[0]); CHECK(copy.Vertices[0]->HiresPosition[0]==b.V[0].HiresPosition[0]);
}

namespace
{
struct WideSourceScope
{
    int Previous = WideMelon::ViewWidth;
    explicit WideSourceScope(int width) { WideMelon::ViewWidth = width; }
    ~WideSourceScope() { WideMelon::ViewWidth = Previous; }
};
void SetWideSource(Quad& quad, int width)
{
    for (auto& v : quad.V)
    {
        v.NativeSourceValid = true;
        for (int axis=0;axis<2;axis++) v.NativeSourcePosition[axis]=v.HiresPosition[axis];
        v.HiresPosition[0]=2048+WideMelon::ProjectX(v.HiresPosition[0]-2048,width);
        v.FinalPosition[0]=v.HiresPosition[0]/16;
    }
}
}
POLICY_TEST(TextureReconstructionWidescreenPreservesSourceGridAndDrawGeometry)
{
    for (int width : {308,342,448,682,768})
    {
        WideSourceScope wide(width);
        Quad a(16,24,32,8), b(24,24,0,32);
        SetWideSource(a,width); SetWideSource(b,width);
        auto plan=BuildTextureReconstructionPlan({&a.P,&b.P});
        CHECK(plan.Groups.size()==1); CHECK(plan.Groups[0].Pieces.size()==2);
        CHECK(plan.Groups[0].Width==20); CHECK(plan.Groups[0].Height==12);
        Polygon copy; std::array<Vertex,4> vertices;
        RemapTextureReconstructionPolygon(b.P,plan.Groups[0],copy,vertices);
        CHECK(copy.Vertices[0]->TexCoords[0]==160);
        for(int i=0;i<4;i++) for(int axis=0;axis<2;axis++)
            CHECK(copy.Vertices[i]->HiresPosition[axis]==b.V[i].HiresPosition[axis]);
        CHECK(b.V[0].TexCoords[0]==0);
        // A real source-grid warp is still ineligible, even if the widened
        // display happens to make it look like a native-sized rectangle.
        b.V[2].NativeSourcePosition[0]++;
        CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
    }
}
POLICY_TEST(TextureReconstructionWidescreenClippingAndCacheUseNativeMetadata)
{
    WideSourceScope wide(342);
    Quad a(-8,0,32,8), b(0,0,0,32);
    for(int i=0;i<4;i++) for(int axis=0;axis<2;axis++)
    {
        a.P.UnclippedSource.Position[i][axis]=a.V[i].HiresPosition[axis];
        a.P.UnclippedSource.UV[i][axis]=a.V[i].TexCoords[axis];
    }
    a.P.UnclippedSource.Valid=true;
    SetWideSource(a,342); SetWideSource(b,342);
    for(int i : {0,1})
    {
        a.V[i].Clipped=true;
        a.V[i].NativeSourcePosition[0]=-64;
    }
    TextureReconstructionPlanCache cache;
    auto& plan=cache.Get({&a.P,&b.P},false,false);
    CHECK(plan.Groups.size()==1);
    Polygon copy; std::array<Vertex,4> vertices;
    RemapTextureReconstructionPolygon(a.P,plan.Groups[0],copy,vertices);
    CHECK(copy.Vertices[0]->TexCoords[0]==96);
    // Metadata missing after loading a state is a conservative fallback.
    b.V[0].NativeSourceValid=false;
    CHECK(cache.Get({&a.P,&b.P},false,false).Groups.empty());
    b.V[0].NativeSourceValid=true;
    CHECK(cache.Get({&a.P,&b.P},false,false).Groups.size()==1);
    b.V[0].NativeSourcePosition[0]++;
    CHECK(cache.Get({&a.P,&b.P},false,false).Groups.empty());
}

POLICY_TEST(TextureReconstructionPlanCacheTracksLiveMetadataAndOptions)
{
    Quad a(0,0,32,8), b(8,0,0,32), donor(0,8,16,16);
    for (auto& z : donor.P.FinalZ) z = 101;
    TextureReconstructionPlanCache cache;
    std::vector<Polygon*> inputs {&a.P,&b.P,&donor.P};
    const auto& first = cache.Get(inputs,true,false);
    CHECK(first.Groups.size()==1);
    CHECK(!first.Groups[0].ContextSpans.empty());
    const auto* storage = first.Groups[0].Domain.data();
    // Different RAM addresses and irrelevant pre-transform color do not
    // invalidate the recipe. Nothing in the cache retains these pointers.
    Quad relocated(8,0,0,32);
    relocated.V[0].Color[0] = 123;
    inputs[1] = &relocated.P;
    CHECK(cache.Get(inputs,true,false).Groups[0].Domain.data()==storage);
    // Current source addressing and UVs must replace the cached recipe.
    relocated.P.TexParam += 32;
    for (auto& v : relocated.V) v.TexCoords[0] += 16;
    const auto& changed = cache.Get(inputs,true,false);
    CHECK(changed.Groups[0].Pieces[1].TexParam==relocated.P.TexParam);
    CHECK(changed.Groups[0].Pieces[1].U==1);
    CHECK(cache.Get(inputs,false,false).Groups[0].ContextPieces.empty());
    CHECK(!cache.Get(inputs,true,false).Groups[0].ContextSpans.empty());
    // Depth changes separate groups; lighting and subpixel changes reject
    // compatibility even though polygon addresses remain unchanged.
    for (auto& z : relocated.P.FinalZ) z++;
    CHECK(cache.Get(inputs,true,false).Groups.empty());
    for (auto& z : relocated.P.FinalZ) z--;
    relocated.V[2].FinalColor[0]--;
    CHECK(cache.Get(inputs,true,false).Groups.empty());
    relocated.V[2].FinalColor[0]++;
    relocated.V[2].HiresPosition[0]++;
    CHECK(cache.Get(inputs,true,false).Groups.empty());
    relocated.V[2].HiresPosition[0]--;
    CHECK(cache.Get(inputs,true,false).Groups.size()==1);
    cache.Reset();
    CHECK(cache.Get({},true,false).Membership.empty());
    CHECK(cache.Get(inputs,true,false).Groups.size()==1);
    // Area/source-mip rejection must not allocate prepared pixel-domain maps.
    CHECK(cache.Get(inputs,true,false,0).Groups[0].Domain.empty());
    CHECK(!cache.Get(inputs,true,false).Groups[0].Domain.empty());
}

POLICY_TEST(TextureReconstructionKeepsTranslatedNativeGridAndLivePhase)
{
    TextureReconstructionPlanCache cache;
    // Exercise every fractional phase, including negative source coordinates.
    for (int phaseX=0;phaseX<16;phaseX++) for (int phaseY=0;phaseY<16;phaseY++)
    {
        Quad a(-4,-4,0,0), b(4,-4,16,0), donor(-4,4,0,16);
        for (auto* q : {&a,&b,&donor}) for (auto& v : q->V)
        {
            v.HiresPosition[0]+=phaseX; v.HiresPosition[1]+=phaseY;
        }
        if (phaseX & 1) for (auto* q : {&a,&b,&donor})
        {
            q->V[1].TexCoords[1]-=16;
            q->V[2].TexCoords[0]-=16; q->V[2].TexCoords[1]-=16;
            q->V[3].TexCoords[0]-=16;
        }
        for (auto& z : donor.P.FinalZ) z++;
        const auto& plan=cache.Get({&a.P,&b.P,&donor.P},true,false);
        CHECK(plan.Groups.size()==1);
        const auto& group=plan.Groups[0];
        CHECK(group.PhaseX==phaseX); CHECK(group.PhaseY==phaseY);
        CHECK(group.X==-4); CHECK(group.Y==-4);
        CHECK(group.ContextPieces.size()==1); CHECK(!group.ContextSpans.empty());
        Polygon copy; std::array<Vertex,4> vertices;
        RemapTextureReconstructionPolygon(b.P,group,copy,vertices);
        CHECK(vertices[0].TexCoords[0]==160); CHECK(vertices[0].TexCoords[1]==32);
        for (int i=0;i<4;i++)
        {
            CHECK(vertices[i].HiresPosition[0]==b.V[i].HiresPosition[0]);
            CHECK(vertices[i].HiresPosition[1]==b.V[i].HiresPosition[1]);
            CHECK(vertices[i].FinalPosition[0]==b.V[i].FinalPosition[0]);
            CHECK(copy.FinalZ[i]==b.P.FinalZ[i]); CHECK(copy.FinalW[i]==b.P.FinalW[i]);
        }
    }
}

POLICY_TEST(TextureReconstructionRejectsDifferentGridPhasesAndWarpedEdges)
{
    Quad a(0,0,0,0), b(8,0,8,0), donor(0,8,0,16);
    for (auto& z : donor.P.FinalZ) z++;
    for (auto& v : b.V) v.HiresPosition[0]+=8;
    CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
    for (auto& v : a.V) v.HiresPosition[0]+=8;
    auto plan=BuildTextureReconstructionPlan({&a.P,&b.P,&donor.P},true);
    CHECK(plan.Groups.size()==1); CHECK(plan.Groups[0].ContextPieces.empty());
    b.V[2].HiresPosition[0]++;
    CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
}

POLICY_TEST(TextureReconstructionClippedFractionalOriginPreservesSamplingPhase)
{
    Quad a(-4,0,0,0), b(4,0,8,0);
    for (auto* q : {&a,&b}) for (auto& v : q->V) v.HiresPosition[0]+=8;
    a.P.UnclippedSource.Valid=true;
    for (int i=0;i<4;i++) for (int axis=0;axis<2;axis++)
    {
        a.P.UnclippedSource.Position[i][axis]=a.V[i].HiresPosition[axis];
        a.P.UnclippedSource.UV[i][axis]=a.V[i].TexCoords[axis];
    }
    for (auto& v : a.V) if (v.HiresPosition[0]<0)
    {
        v.HiresPosition[0]=v.FinalPosition[0]=0;
        v.Clipped=true; v.TexCoords[0]=55;
    }
    auto plan=BuildTextureReconstructionPlan({&a.P,&b.P});
    CHECK(plan.Groups.size()==1); CHECK(plan.Groups[0].X==-4); CHECK(plan.Groups[0].PhaseX==8);
    Polygon copy; std::array<Vertex,4> vertices;
    RemapTextureReconstructionPolygon(a.P,plan.Groups[0],copy,vertices);
    CHECK(vertices[0].HiresPosition[0]==0); CHECK(vertices[0].TexCoords[0]==88);
    CHECK(vertices[2].TexCoords[0]==160);
    // A clipped vertex outside the actual fractional source bounds stays unsafe.
    a.V[2].HiresPosition[0]++;
    CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
}

POLICY_TEST(TextureReconstructionPlanCacheTracksClippedSourcesAndFractionalFormats)
{
    Quad a(0,0,0,0), b(8,0,8,0);
    TextureReconstructionPlanCache cache;
    std::vector<Polygon*> inputs {&a.P,&b.P};
    CHECK(cache.Get(inputs,false,false).Groups.size()==1);
    b.P.UnclippedSource.Valid = true;
    for (int i=0;i<4;i++) for (int axis=0;axis<2;axis++)
    {
        b.P.UnclippedSource.Position[i][axis]=b.V[i].HiresPosition[axis];
        b.P.UnclippedSource.UV[i][axis]=b.V[i].TexCoords[axis];
    }
    CHECK(cache.Get(inputs,false,false).Groups.size()==1);
    b.P.UnclippedSource.UV[2][0]--;
    CHECK(cache.Get(inputs,false,false).Groups.empty());
    b.P.UnclippedSource.UV[2][0]++;
    b.P.UnclippedSource.Position[2][0]--;
    CHECK(cache.Get(inputs,false,false).Groups.empty());
    b.P.UnclippedSource.Valid = false;
    for (auto* p : inputs) p->TexParam = (p->TexParam & ~(7u<<26)) | (6u<<26);
    CHECK(cache.Get(inputs,false,false).Groups.empty());
    CHECK(cache.Get(inputs,false,true).Groups.size()==1);
    b.P.TexParam = (b.P.TexParam & ~(7u<<26)) | (7u<<26);
    CHECK(cache.Get(inputs,false,true).Groups.empty()); // capture/direct color
}

POLICY_TEST(TextureReconstructionPlanCacheRetainsUnchangedGroupsAcrossAnimation)
{
    Quad a(0,0,0,0), b(8,0,8,0), c(64,0,0,16), d(72,0,8,16);
    TextureReconstructionPlanCache cache;
    std::vector<Polygon*> inputs {&a.P,&b.P,&c.P,&d.P};
    const auto& first=cache.Get(inputs,true,false);
    CHECK(first.Groups.size()==2);
    const auto* firstDomain=first.Groups[0].Domain.data();
    for (auto& v : d.V) v.TexCoords[1]+=16;
    const auto& animated=cache.Get(inputs,true,false);
    CHECK(animated.Groups[0].Domain.data()==firstDomain);
    CHECK(animated.Groups[1].Pieces[1].V==17);
    // The new donor changes ownership and must not inherit a stale domain.
    Quad donor(0,8,0,32);
    for (auto& z : donor.P.FinalZ) z++;
    inputs.push_back(&donor.P);
    CHECK(!cache.Get(inputs,true,false).Groups[0].ContextSpans.empty());
    donor.P.TexPalette++;
    CHECK(cache.Get(inputs,true,false).Groups[0].ContextSpans.empty());
}

POLICY_TEST(TextureReconstructionPreparedCopiesMatchOwnershipWithLivePixels)
{
    for (int flips=0;flips<4;flips++)
    {
        Quad a(0,0,0,0), b(8,0,8,0), donor(0,8,16,16), competitor(4,8,32,32);
        for (auto& z : donor.P.FinalZ) z = 101;
        for (auto& z : competitor.P.FinalZ) z = 102;
        for (auto& v : donor.V)
        {
            if (flips&1) v.TexCoords[0]=s16(40*16-v.TexCoords[0]);
            if (flips&2) v.TexCoords[1]=s16(40*16-v.TexCoords[1]);
        }
        auto plan=BuildTextureReconstructionPlan({&a.P,&b.P,&donor.P,&competitor.P},true);
        CHECK(plan.Groups.size()==1);
        auto& group=plan.Groups[0];
        PrepareTextureReconstructionGroup(group);
        CHECK(!group.ContextSpans.empty());
        // Reuse one recipe while every atlas texel changes, including alpha.
        for (u32 frame=0;frame<3;frame++)
        {
            std::vector<u32> atlas(64*64);
            for (u32 i=0;i<atlas.size();i++) atlas[i]=(i%5 ? 0xFF000000u : 0) | (i+frame*65536);
            std::vector<u32> expected(group.Width*group.Height,0), actual(expected.size(),0);
            std::vector<u8> domain(expected.size(),0);
            for (const auto& piece : group.Pieces)
                CopyTextureReconstructionPiece(group,piece,64,atlas.data(),expected.data(),domain.data());
            const auto owners=TextureReconstructionContextOwners(group,domain);
            for (int i=0;i<int(owners.size());i++) if (owners[i]>=0)
            {
                const auto& piece=group.ContextPieces[owners[i]];
                const int x=i%group.Width+group.X-group.Border-piece.X;
                const int y=i/group.Width+group.Y-group.Border-piece.Y;
                const int u=piece.U+(piece.FlipX ? piece.Width-1-x : x);
                const int v=piece.V+(piece.FlipY ? piece.Height-1-y : y);
                expected[i]=atlas[v*64+u]; domain[i]=1;
            }
            for (int n : group.SourceOrder)
                CopyTextureReconstructionPiece(group,group.Pieces[n],64,atlas.data(),actual.data(),nullptr);
            for (const auto& span : group.ContextSpans)
            {
                const int step=group.ContextPieces[span.Donor].FlipX ? -1 : 1;
                for (int i=0;i<span.Count;i++) actual[span.Destination+i]=atlas[span.Source+i*step];
            }
            CHECK(expected==actual); CHECK(domain==group.Domain);
        }
    }
}
POLICY_TEST(TextureReconstructionUsesUniformGridForInclusiveEndpoints)
{
    Quad a(0,0,16,16), b(8,0,32,32);
    for (auto* q : {&a,&b})
    {
        q->V[1].TexCoords[1] -= 16;
        q->V[2].TexCoords[0] -= 16; q->V[2].TexCoords[1] -= 16;
        q->V[3].TexCoords[0] -= 16;
    }
    auto plan=BuildTextureReconstructionPlan({&a.P,&b.P});
    CHECK(plan.Groups.size()==1);
    CHECK(plan.Groups[0].Pieces[1].Width==8);
    Polygon copy; std::array<Vertex,4> vertices;
    RemapTextureReconstructionPolygon(b.P,plan.Groups[0],copy,vertices);
    CHECK(copy.Vertices[0]->TexCoords[0]==160);
    for (int i=0;i<4;i++) for(int axis=0;axis<2;axis++)
    {
        CHECK(copy.Vertices[i]->TexCoords[axis]-copy.Vertices[0]->TexCoords[axis] ==
              b.V[i].HiresPosition[axis]-b.V[0].HiresPosition[axis]);
        CHECK(copy.Vertices[i]->HiresPosition[axis]==b.V[i].HiresPosition[axis]);
    }
    // A smaller arbitrary footprint is not an inclusive endpoint convention.
    b.V[2].TexCoords[0]-=16; b.V[3].TexCoords[0]-=16;
    CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
}
POLICY_TEST(TextureReconstructionClippedTileKeepsOriginalGridAndVisibleGeometry)
{
    Quad a(-4,8,16,16), b(4,8,32,16);
    for (int i=0;i<4;i++) for(int axis=0;axis<2;axis++)
    {
        a.P.UnclippedSource.Position[i][axis]=a.V[i].HiresPosition[axis];
        a.P.UnclippedSource.UV[i][axis]=a.V[i].TexCoords[axis];
    }
    for (auto& v:a.V) if(v.HiresPosition[0]<0)
    {
        v.HiresPosition[0]=v.FinalPosition[0]=0;
        v.TexCoords[0]=319; // clipping may lose fractional precision
        v.Clipped=true;
    }
    CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
    a.P.UnclippedSource.Valid=true;
    auto plan=BuildTextureReconstructionPlan({&a.P,&b.P});
    CHECK(plan.Groups.size()==1);
    CHECK(plan.Groups[0].X==-4);
    CHECK(plan.Groups[0].Pieces[0].Width==8);
    CHECK(plan.Groups[0].Pieces[0].U==16);
    Polygon copy; std::array<Vertex,4> vertices;
    RemapTextureReconstructionPolygon(a.P,plan.Groups[0],copy,vertices);
    CHECK(copy.Vertices[0]->HiresPosition[0]==0);
    CHECK(copy.Vertices[0]->TexCoords[0]==96);
    CHECK(a.V[0].TexCoords[0]==319);
    CHECK(copy.FinalZ[0]==a.P.FinalZ[0]);
    a.V[0].HiresPosition[0]=8191;
    CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
    a.V[0].HiresPosition[0]=0;
    a.P.UnclippedSource.UV[2][0]++;
    CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
}
POLICY_TEST(TextureReconstructionTopClippedSourceJoinsAfterSignedProjection)
{
    Quad a(0,-8,16,16), b(0,0,32,16);
    for (int i=0;i<4;i++) for (int axis=0;axis<2;axis++)
    {
        a.P.UnclippedSource.Position[i][axis]=a.V[i].HiresPosition[axis];
        a.P.UnclippedSource.UV[i][axis]=a.V[i].TexCoords[axis];
    }
    // Use a 16-pixel-tall original tile ending at Y=8, clipped at the viewport.
    for (int i=0;i<4;i++)
    {
        const bool top=i==0 || i==3;
        a.P.UnclippedSource.Position[i][1]=top ? -127 : 128;
        a.P.UnclippedSource.UV[i][1]=s16((top?16:31)*16);
        a.V[i].HiresPosition[1]=top?0:128; a.V[i].FinalPosition[1]=top?0:8;
        a.V[i].Clipped=top;
        a.V[i].TexCoords[1]=s16(top?375:496);
        b.V[i].HiresPosition[1]+=128; b.V[i].FinalPosition[1]+=8;
    }
    a.P.UnclippedSource.Valid=true;
    CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
    for (int i : {0,3}) a.P.UnclippedSource.Position[i][1]=s32(
        Polygon::UnclippedQuadSource::ProjectAxis(-17749,16384,192,0));
    auto plan=BuildTextureReconstructionPlan({&a.P,&b.P});
    CHECK(plan.Groups.size()==1); CHECK(plan.Groups[0].Y==-8);
    CHECK(plan.Groups[0].Pieces[0].Height==16);
    Polygon copy; std::array<Vertex,4> vertices;
    RemapTextureReconstructionPolygon(a.P,plan.Groups[0],copy,vertices);
    CHECK(copy.Vertices[0]->TexCoords[1]==160);
    CHECK(copy.Vertices[0]->HiresPosition[1]==0); CHECK(copy.FinalZ[0]==a.P.FinalZ[0]);
}
POLICY_TEST(TextureReconstructionRetainsDifferentAtlasAddresses)
{
    Quad a(0,0,16,16), b(8,0,32,32);
    b.P.TexParam |= 0x100;
    auto plan=BuildTextureReconstructionPlan({&a.P,&b.P});
    CHECK(plan.Groups.size()==1);
    CHECK(plan.Groups[0].Pieces[0].TexParam==a.P.TexParam);
    CHECK(plan.Groups[0].Pieces[1].TexParam==b.P.TexParam);
    CHECK(plan.Groups[0].Pieces[0].TexParam!=plan.Groups[0].Pieces[1].TexParam);
    b.P.TexParam |= 1u<<16;
    CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
}
POLICY_TEST(TextureReconstructionCopiesFlippedArtworkInScreenOrientation)
{
    std::vector<u32> atlas(64*64);
    for (int i=0;i<int(atlas.size());i++) atlas[i]=u32(i+1);
    atlas[16*64+16]=0; // authored transparency must move with its artwork
    for (bool inclusive : {false,true}) for (int flips=0;flips<4;flips++)
    {
        Quad a(0,0,0,0), b(8,0,16,16);
        const int span = inclusive ? 7 : 8;
        for (auto& v:b.V) for (int axis=0;axis<2;axis++)
        {
            int offset=(v.TexCoords[axis]/16-16) ? span : 0;
            if (flips & (1<<axis)) offset=span-offset;
            v.TexCoords[axis]=s16((16+offset)*16);
        }
        auto plan=BuildTextureReconstructionPlan({&a.P,&b.P});
        CHECK(plan.Groups.size()==1);
        const auto& group=plan.Groups[0]; const auto& piece=group.Pieces[1];
        CHECK(piece.FlipX==bool(flips&1)); CHECK(piece.FlipY==bool(flips&2));
        std::vector<u32> source(group.Width*group.Height,0xDEADBEEF);
        std::vector<u8> domain(source.size(),0);
        CopyTextureReconstructionPiece(group,piece,64,atlas.data(),source.data(),domain.data());
        for (int y=0;y<8;y++) for (int x=0;x<8;x++)
        {
            const int sx=16+((flips&1)?7-x:x), sy=16+((flips&2)?7-y:y);
            const int dst=(y+2)*group.Width+10+x;
            CHECK(source[dst]==atlas[sy*64+sx]); CHECK(domain[dst]==1);
        }
        CHECK(domain[0]==0); CHECK(source[0]==0xDEADBEEF);
        Polygon copy; std::array<Vertex,4> vertices;
        RemapTextureReconstructionPolygon(b.P,group,copy,vertices);
        CHECK(copy.Vertices[0]->TexCoords[0]==160);
        CHECK(copy.Vertices[2]->TexCoords[0]==288);
        CHECK(copy.Vertices[2]->HiresPosition[0]==b.V[2].HiresPosition[0]);
        // Twisting only one corner is not an axis flip.
        b.V[0].TexCoords[0]=b.V[2].TexCoords[0];
        CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
    }
}
POLICY_TEST(TextureReconstructionClippedFlipUsesOriginalSourceFootprint)
{
    Quad a(-4,8,16,16), b(4,8,32,16);
    for (int i=0;i<4;i++)
    {
        a.V[i].TexCoords[0]=s16((16+7)*16-(a.V[i].TexCoords[0]-16*16)*7/8);
        for (int axis=0;axis<2;axis++)
        {
            a.P.UnclippedSource.Position[i][axis]=a.V[i].HiresPosition[axis];
            a.P.UnclippedSource.UV[i][axis]=a.V[i].TexCoords[axis];
        }
        if (a.V[i].HiresPosition[0]<0)
        {
            a.V[i].HiresPosition[0]=a.V[i].FinalPosition[0]=0;
            a.V[i].TexCoords[0]=311; a.V[i].Clipped=true;
        }
    }
    a.P.UnclippedSource.Valid=true;
    auto plan=BuildTextureReconstructionPlan({&a.P,&b.P});
    CHECK(plan.Groups.size()==1);
    const auto& piece=plan.Groups[0].Pieces[0];
    CHECK(piece.FlipX); CHECK(!piece.FlipY); CHECK(piece.U==16); CHECK(piece.Width==8);
    Polygon copy; std::array<Vertex,4> vertices;
    RemapTextureReconstructionPolygon(a.P,plan.Groups[0],copy,vertices);
    CHECK(copy.Vertices[0]->TexCoords[0]==96); CHECK(copy.Vertices[0]->HiresPosition[0]==0);
    CHECK(copy.FinalZ[0]==a.P.FinalZ[0]);
}
POLICY_TEST(TextureReconstructionRejectsDifferentPlanesMaterialsAndOverlaps)
{
    for (int kind=0;kind<9;kind++)
    {
        Quad a(0,0,0,0), b(8,0,24,0);
        if(kind==0) for(auto& z:b.P.FinalZ) z=101;
        if(kind==1) b.P.Attr^=1u<<24;
        if(kind==2) b.P.TexPalette=1;
        if(kind==3) b.P.TexParam^=1u<<20;
        if(kind==4) b.P.Attr=(b.P.Attr&~0x1F0000)|0x100000;
        if(kind==5) b.P.IsShadow=true;
        if(kind==6) b.P.FinalW[1]++;
        if(kind==7) b.V[1].FinalColor[0]--;
        if(kind==8) b.V[0].Clipped=true;
        CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
    }
    Quad a(0,0,0,0), b(8,0,24,0), overlapping(4,0,32,0);
    CHECK(BuildTextureReconstructionPlan({&a.P,&b.P,&overlapping.P}).Groups.empty());
}
POLICY_TEST(TextureReconstructionRejectsCaptureRepeatAndNonIdentityMappings)
{
    for(int kind=0;kind<5;kind++)
    {
        Quad a(0,0,0,0), b(8,0,24,0);
        if(kind==0) b.P.TexParam=(b.P.TexParam&~(7u<<26))|(7u<<26);
        if(kind==1) b.P.TexParam|=1u<<16;
        if(kind==2) for(auto& v:b.V) v.TexCoords[0]*=2;
        if(kind==3) for(auto& v:b.V) v.TexCoords[0]++;
        if(kind==4) for(auto& v:b.V) v.HiresPosition[0]++;
        CHECK(BuildTextureReconstructionPlan({&a.P,&b.P}).Groups.empty());
    }
}

POLICY_TEST(TextureReconstructionFractionalPiecesAreIndependentAndOptIn)
{
    Quad a(0,0,0,0), b(8,0,8,0), c(0,16,0,0), d(8,16,0,0);
    c.P.TexParam = (1u<<26); c.P.Type = 1;
    d.P.TexParam = (1u<<26)|(1u<<20)|16; d.P.Type = 1;
    std::vector<Polygon*> polygons {&a.P,&b.P,&c.P,&d.P};
    auto base = BuildTextureReconstructionPlan(polygons);
    auto plan = BuildTextureReconstructionPlan(polygons, false, true);
    CHECK(base.Groups.size()==1); CHECK(plan.Groups.size()==2);
    CHECK(plan.Membership[0]==base.Membership[0]); CHECK(plan.Membership[1]==base.Membership[1]);
    CHECK(base.Membership[2]==-1); CHECK(plan.Membership[2]==1); CHECK(plan.Membership[3]==1);
    CHECK(plan.Groups[1].HasFractionalAlpha()); CHECK(plan.Groups[1].ContextPieces.empty());
    for (auto& z : d.P.FinalZ) z++;
    CHECK(BuildTextureReconstructionPlan(polygons, true, true).Groups.size()==1);
    for (auto& z : d.P.FinalZ) z--;
    d.P.TexPalette++;
    CHECK(BuildTextureReconstructionPlan(polygons, true, true).Groups.size()==1);
}
POLICY_TEST(TextureReconstructionFractionalFullReversePeriodUsesRealTexels)
{
    Quad a(0,0,0,0);
    a.P.TexParam = (6u<<26)|(1u<<16)|(1u<<18); a.P.Type=1;
    for (auto& v : a.V) v.TexCoords[0] = 7*16-v.TexCoords[0];
    TextureReconstructionPiece piece;
    CHECK(!DescribeTextureReconstructionPiece(a.P,0,piece));
    CHECK(DescribeTextureReconstructionPiece(a.P,0,piece,true));
    CHECK(piece.U==0); CHECK(piece.FlipX); CHECK(piece.Width==8);
    TextureReconstructionGroup group; group.Width=12; group.Height=12;
    std::vector<u32> atlas(64), pixels(144); std::vector<u8> domain(144);
    for (unsigned i=0;i<atlas.size();i++) atlas[i]=i;
    CopyTextureReconstructionPiece(group,piece,8,atlas.data(),pixels.data(),domain.data());
    CHECK(pixels[26]==7); CHECK(pixels[33]==0); CHECK(pixels[110]==63);
    a.P.TexParam |= 1u<<20; // Footprint is no longer an exact full period.
    CHECK(!DescribeTextureReconstructionPiece(a.P,0,piece,true));
    a.P.TexParam = 7u<<26;
    CHECK(!DescribeTextureReconstructionPiece(a.P,0,piece,true));
}
POLICY_TEST(TextureReconstructionContextDoesNotChangeMembershipOrDepth)
{
    Quad a(0,0,0,0), b(8,0,8,0), donor(16,0,16,0);
    for (auto& z : donor.P.FinalZ) z=200;
    auto base=BuildTextureReconstructionPlan({&a.P,&b.P,&donor.P});
    auto plan=BuildTextureReconstructionPlan({&a.P,&b.P,&donor.P},true);
    CHECK(base.Groups.size()==1); CHECK(base.Groups[0].ContextPieces.empty());
    CHECK(plan.Membership==base.Membership); CHECK(plan.Groups[0].Width==base.Groups[0].Width);
    CHECK(plan.Groups[0].ContextPieces.size()==1); CHECK(plan.Groups[0].ContextAdjacent[0]);
    CHECK(donor.P.FinalZ[0]==200);
    donor.P.Attr ^= 1u<<24;
    CHECK(BuildTextureReconstructionPlan({&a.P,&b.P,&donor.P},true).Groups[0].ContextPieces.empty());
}
POLICY_TEST(TextureReconstructionContextProtectsTransparencyAndRejectsCompetition)
{
    TextureReconstructionGroup group; group.Width=12; group.Height=12;
    group.Pieces.push_back({0,0,0,8,8,0,0,0});
    group.ContextPieces.push_back({1,8,0,8,8,0,0,0}); group.ContextAdjacent.push_back(true);
    std::vector<u8> domain(144,0);
    for (int y=2;y<10;y++) for(int x=2;x<10;x++) domain[y*12+x]=1;
    // An authored transparent texel remains protected; no RGB test is used.
    domain[34]=1;
    auto owners=TextureReconstructionContextOwners(group,domain);
    CHECK(owners[34]==-1); CHECK(owners[46]==0); CHECK(owners[47]==0);
    // A non-adjacent rectangle still blocks competing context.
    group.ContextPieces.push_back({2,9,0,8,8,0,0,0}); group.ContextAdjacent.push_back(false);
    owners=TextureReconstructionContextOwners(group,domain);
    CHECK(owners[46]==0); CHECK(owners[47]==-2);
    group.ContextAdjacent[0]=false;
    CHECK(TextureReconstructionContextOwners(group,domain)[46]==-1);
}

POLICY_TEST(Smart2DProtectsSubtexelFillsAndSingleAxisStrips)
{
    Quad q(0,0,0,0);
    CHECK(!IsStretchedTexture2DStrip(q.P));
    for (auto& v : q.V) v.TexCoords[0] = v.TexCoords[0] == 0 ? 32 : 46;
    CHECK(IsStretchedTexture2DStrip(q.P)); // U=2..2.875, V spans eight texels.
    for (auto& v : q.V) v.TexCoords[1] = v.TexCoords[1] == 0 ? 928 : 944;
    CHECK(IsStretchedTexture2DStrip(q.P)); // One-texel fill, fractional alpha allowed.
    q.P.TexParam = (1u<<26)|(3u<<20)|(3u<<23);
    q.P.Attr = (q.P.Attr & ~0x1F0000u) | (28u<<16);
    CHECK(IsStretchedTexture2DStrip(q.P));
    for (auto& v : q.V) v.TexCoords[0]=32;
    CHECK(IsStretchedTexture2DStrip(q.P)); // Constant coordinate.
}
POLICY_TEST(Smart2DStripPolicyRejectsCrossTexelSkewAndPerspective)
{
    Quad q(0,0,0,0);
    for (auto& v : q.V) v.TexCoords[0] = v.TexCoords[0] == 0 ? 15 : 17;
    CHECK(!IsStretchedTexture2DStrip(q.P)); // Small range spans two native cells.
    for (auto& v : q.V) v.TexCoords[0] = v.TexCoords[0] == 15 ? 0 : 16;
    CHECK(IsStretchedTexture2DStrip(q.P));
    q.V[1].TexCoords[0]=1;
    CHECK(!IsStretchedTexture2DStrip(q.P));
    q.V[1].TexCoords[0]=0;
    q.P.FinalW[1]++;
    CHECK(!IsStretchedTexture2DStrip(q.P));
    q.P.FinalW[1]--;
    q.P.FinalZ[2]++;
    CHECK(!IsStretchedTexture2DStrip(q.P));
    q.P.FinalZ[2]--;
    q.V[2].HiresPosition[0]++;
    CHECK(!IsStretchedTexture2DStrip(q.P));
}
POLICY_TEST(Smart2DStripPolicyKeepsCaptureAndRepeatedArtworkOut)
{
    Quad q(0,0,0,0);
    for (auto& v : q.V) v.TexCoords[0]/=8;
    CHECK(IsStretchedTexture2DStrip(q.P));
    const auto param=q.P.TexParam;
    for (u32 flags : {1u<<16,1u<<17,1u<<18,1u<<19,1u<<30})
    {
        q.P.TexParam=param|flags; CHECK(!IsStretchedTexture2DStrip(q.P));
    }
    for (u32 format : {0u,5u,7u})
    {
        q.P.TexParam=(param&~(7u<<26))|(format<<26); CHECK(!IsStretchedTexture2DStrip(q.P));
    }
    q.P.TexParam=param; q.P.IsShadowMask=true;
    CHECK(!IsStretchedTexture2DStrip(q.P));
    q.P.IsShadowMask=false;
    for (auto& v : q.V) v.TexCoords[0]=-1;
    CHECK(!IsStretchedTexture2DStrip(q.P));
    for (auto& v : q.V) v.TexCoords[0]=64*16;
    CHECK(!IsStretchedTexture2DStrip(q.P));
}
POLICY_TEST(Smart2DStripPolicySupportsFlipsAndSwappedAxesWithoutChangingPolygon)
{
    Quad q(0,0,0,0);
    for (auto& v : q.V) v.TexCoords[0]=16-v.TexCoords[0]/8;
    CHECK(IsStretchedTexture2DStrip(q.P));
    for (auto& v : q.V) std::swap(v.TexCoords[0],v.TexCoords[1]);
    CHECK(IsStretchedTexture2DStrip(q.P));
    CHECK(q.V[0].TexCoords[1]==16); CHECK(q.V[2].TexCoords[1]==0);
    // One screen pixel along the narrow source axis is ordinary artwork.
    for (auto& v : q.V) v.HiresPosition[0]/=8;
    CHECK(!IsStretchedTexture2DStrip(q.P));
}

POLICY_TEST(Smart2DSmallFillsSupportInBoundsRepeatAndOrientation)
{
    for (int orientation=0;orientation<4;orientation++)
    {
        Quad q(0,0,0,0);
        q.P.TexParam |= 3u<<16;
        for (auto& v:q.V)
        {
            v.TexCoords[0]=s16(62*16+v.TexCoords[0]/4);
            v.TexCoords[1]=s16(12*16+v.TexCoords[1]/8);
            if (orientation&1) v.TexCoords[0]=s16(126*16-v.TexCoords[0]);
            if (orientation&2) std::swap(v.TexCoords[0],v.TexCoords[1]);
        }
        CHECK(IsStretchedTexture2DStrip(q.P)); // 2x1 at the atlas edge, 4x/8x magnification.
        CHECK((q.P.TexParam & (3u<<16)) == (3u<<16));
    }
}
POLICY_TEST(Smart2DSmallFillsRejectArtworkWrappingAndUnsafeGeometry)
{
    for (int kind=0;kind<15;kind++)
    {
        Quad q(0,0,0,0);
        for (auto& v:q.V) { v.TexCoords[0]/=4; v.TexCoords[1]/=4; }
        q.P.TexParam |= 3u<<16;
        CHECK(IsStretchedTexture2DStrip(q.P));
        if (kind==0) for (auto& v:q.V) v.HiresPosition[0]/=2; // Insufficient enlargement on one axis.
        if (kind==1) for (auto& v:q.V) v.TexCoords[0]=s16(v.TexCoords[0]*3/2); // 3x2 artwork.
        if (kind==2) for (auto& v:q.V) v.TexCoords[0]++; // Fractional endpoints.
        if (kind==3) for (auto& v:q.V) v.TexCoords[0]+=63*16; // Crosses repeat boundary.
        if (kind==4) for (auto& v:q.V) v.TexCoords[0]-=16;
        if (kind==5) q.P.TexParam |= 1u<<18; // Mirroring.
        if (kind==6) q.P.TexParam |= 1u<<30; // Generated coordinates.
        if (kind==7) q.P.FinalW[1]++;
        if (kind==8) q.P.FinalZ[1]++;
        if (kind==9) q.V[1].TexCoords[0]++;
        if (kind==10) q.P.TexParam=(q.P.TexParam&~(7u<<26))|(7u<<26); // Capture/direct color.
        if (kind==11) q.P.TexParam=(q.P.TexParam&~(7u<<26))|(5u<<26); // Compressed texture.
        if (kind==12) q.P.IsShadow=true;
        if (kind==13) q.P.Degenerate=true;
        if (kind==14) q.V[1].HiresPosition[0]++;
        CHECK(!IsStretchedTexture2DStrip(q.P));
    }
}
