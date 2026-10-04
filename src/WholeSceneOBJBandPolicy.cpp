// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WholeSceneOBJBandPolicy.h"

#include <algorithm>

namespace melonDS
{

namespace
{

constexpr u64 kFNVOffset = 1469598103934665603ull;
constexpr u64 kFNVPrime = 1099511628211ull;

void HashValue(u64& hash, u64 value)
{
    for (int byte = 0; byte < 8; byte++)
    {
        hash ^= (value >> (byte * 8)) & 0xFFu;
        hash *= kFNVPrime;
    }
}

void SetOAMMaskBit(u64 (&mask)[2], int oamIndex)
{
    if (oamIndex >= 0 && oamIndex < 128)
        mask[oamIndex >> 6] |= 1ull << (oamIndex & 63);
}

struct VisibleOBJRect
{
    int Left = 0;
    int Top = 0;
    int Right = 0;
    int Bottom = 0;
};

int BuildVisibleOBJRects(const WholeSceneOrdinaryOBJBandMember& member,
                         VisibleOBJRect (&rects)[2])
{
    if (member.BoundWidth <= 0 || member.BoundHeight <= 0)
        return -1;

    const int left = std::max(0, member.PositionX);
    const int right = std::min(256, member.PositionX + member.BoundWidth);
    if (right <= left)
        return 0;

    int count = 0;
    const int candidateY[2] = {
        member.PositionY,
        member.PositionY & 0xFF,
    };
    for (int candidate = 0; candidate < 2; candidate++)
    {
        const int top = std::max(0, candidateY[candidate]);
        const int bottom = std::min(
            192, candidateY[candidate] + member.BoundHeight);
        if (bottom <= top)
            continue;
        if (count != 0 && rects[0].Top == top && rects[0].Bottom == bottom)
            continue;
        rects[count++] = {left, top, right, bottom};
    }
    return count;
}

bool OBJBoundsOverlapWithinIsolationMargin(
    const WholeSceneOrdinaryOBJBandMember& first,
    const WholeSceneOrdinaryOBJBandMember& second,
    int margin)
{
    VisibleOBJRect firstRects[2] {};
    VisibleOBJRect secondRects[2] {};
    const int firstCount = BuildVisibleOBJRects(first, firstRects);
    const int secondCount = BuildVisibleOBJRects(second, secondRects);
    if (firstCount < 0 || secondCount < 0)
        return true;

    const int guardedMargin = std::max(margin, 0);
    for (int firstIndex = 0; firstIndex < firstCount; firstIndex++)
    {
        const auto& firstRect = firstRects[firstIndex];
        for (int secondIndex = 0; secondIndex < secondCount; secondIndex++)
        {
            const auto& secondRect = secondRects[secondIndex];
            const bool separated =
                firstRect.Right + guardedMargin <= secondRect.Left ||
                secondRect.Right + guardedMargin <= firstRect.Left ||
                firstRect.Bottom + guardedMargin <= secondRect.Top ||
                secondRect.Bottom + guardedMargin <= firstRect.Top;
            if (!separated)
                return true;
        }
    }
    return false;
}

bool AreSupportedSpecialOBJsIsolated(
    const WholeSceneOBJBandClassification& classification,
    int margin)
{
    for (const auto& special : classification.Members)
    {
        const bool supportedSpecial =
            special.OBJMode == 1 && special.SourceType < 2 &&
            !special.Mosaic &&
            special.RejectionMask == OBJBandRejectNonNormalMode;
        if (!supportedSpecial)
            continue;

        for (const auto& other : classification.Members)
        {
            if (&special == &other)
                continue;
            if (OBJBoundsOverlapWithinIsolationMargin(
                    special, other, margin))
                return false;
        }
    }
    return true;
}

bool SameRelation(const WholeSceneOrdinaryOBJBand& band,
                  const WholeSceneOrdinaryOBJBandMember& member)
{
    return band.AffineInFrontOAMMask[0] ==
               member.AffineInFrontOAMMask[0] &&
           band.AffineInFrontOAMMask[1] ==
               member.AffineInFrontOAMMask[1] &&
           band.AffineBehindOAMMask[0] ==
               member.AffineBehindOAMMask[0] &&
           band.AffineBehindOAMMask[1] ==
               member.AffineBehindOAMMask[1] &&
           band.PreservedInFrontOAMMask[0] ==
               member.PreservedInFrontOAMMask[0] &&
           band.PreservedInFrontOAMMask[1] ==
               member.PreservedInFrontOAMMask[1] &&
           band.PreservedBehindOAMMask[0] ==
               member.PreservedBehindOAMMask[0] &&
           band.PreservedBehindOAMMask[1] ==
               member.PreservedBehindOAMMask[1] &&
           band.AffineBGInFrontMask == member.AffineBGInFrontMask &&
           band.AffineBGBehindMask == member.AffineBGBehindMask;
}

}

WholeSceneOBJBandClassification ClassifyWholeSceneOBJBands(
    const std::vector<WholeSceneOBJBandInput>& inputs,
    int bandLimit,
    const WholeSceneOBJBandContext& context)
{
    WholeSceneOBJBandClassification result;
    auto& summary = result.Summary;
    summary.InputCount = static_cast<int>(inputs.size());
    summary.BandLimit = std::max(bandLimit, 0);
    summary.AffineBGMask = context.AffineBGMask & 0xFu;

    std::vector<const WholeSceneOBJBandInput*> affineInputs;
    affineInputs.reserve(inputs.size());
    for (const auto& input : inputs)
    {
        if (!input.IsAffine)
            continue;

        summary.AffineCount++;
        SetOAMMaskBit(summary.AffineOAMMask, input.OAMIndex);
        const bool supportedNormalAffine =
            input.OBJMode == 0 && input.SourceType < 2 && !input.Mosaic;
        const bool supportedSpecialAffine =
            input.OBJMode == 1 && input.SourceType < 2 && !input.Mosaic;
        if (supportedNormalAffine)
        {
            // The ordered presentation product retains priority/OAM identity
            // per entry, so sharing a rotscale parameter no longer makes the
            // group atomic. Adjacent normal entries are composed in native
            // back-to-front order at the subpixel grid.
            affineInputs.push_back(&input);
            result.AffineInputs.push_back(input);
        }
        else if (supportedSpecialAffine)
        {
            // Mode-1 affine OBJ remains a separate exact semantic operand.
            // It is added to Members below as an ordering anchor rather than
            // folded into fractional geometric coverage.
            summary.RejectionMask |= OBJBandRejectNonNormalMode;
        }
        else
        {
            if (input.OBJMode != 0)
            {
                summary.RejectionMask |= OBJBandRejectNonNormalMode;
                summary.BlockingRejectionMask |= OBJBandRejectNonNormalMode;
            }
            if (input.SourceType >= 2)
            {
                summary.RejectionMask |= OBJBandRejectUnsupportedSource;
                summary.BlockingRejectionMask |= OBJBandRejectUnsupportedSource;
            }
            if (input.Mosaic)
            {
                summary.RejectionMask |= OBJBandRejectMosaic;
                summary.BlockingRejectionMask |= OBJBandRejectMosaic;
            }
        }
    }

    if (affineInputs.empty() && summary.AffineBGMask == 0)
    {
        summary.RejectionMask |= OBJBandRejectNoAffineOBJ;
        summary.BlockingRejectionMask |= OBJBandRejectNoAffineOBJ;
    }

    // Normal-mode bands may be reconstructed while supported mode-1/mode-3
    // ordinary roles remain in the semantic compositor. Treat those roles as
    // ordering anchors so a band never straddles one of them. Unsupported
    // sources, mosaic and OBJ-window masks are not presentation anchors.
    std::vector<const WholeSceneOBJBandInput*> preservedInputs;
    preservedInputs.reserve(inputs.size());
    for (const auto& input : inputs)
    {
        if ((input.OBJMode == 1 || input.OBJMode == 3) &&
            input.SourceType < 2 && !input.Mosaic)
        {
            preservedInputs.push_back(&input);
        }
    }

    for (const auto& input : inputs)
    {
        if (input.IsAffine && input.OBJMode == 0)
            continue;

        WholeSceneOrdinaryOBJBandMember member;
        member.RenderedIndex = input.RenderedIndex;
        member.OAMIndex = input.OAMIndex;
        member.Priority = input.Priority;
        member.OBJMode = input.OBJMode;
        member.SourceType = input.SourceType;
        member.Mosaic = input.Mosaic;
        member.IsAffine = input.IsAffine;
        member.PositionX = input.PositionX;
        member.PositionY = input.PositionY;
        member.BoundWidth = input.BoundWidth;
        member.BoundHeight = input.BoundHeight;
        member.TotalPriority = static_cast<int>(input.Priority) * 128 +
                               input.OAMIndex;
        summary.OrdinaryCount++;

        if (input.OBJMode != 0)
            member.RejectionMask |= OBJBandRejectNonNormalMode;
        if (input.SourceType >= 2)
            member.RejectionMask |= OBJBandRejectUnsupportedSource;
        if (input.Mosaic)
            member.RejectionMask |= OBJBandRejectMosaic;

        for (const auto* affine : affineInputs)
        {
            const int affineTotalPriority =
                static_cast<int>(affine->Priority) * 128 + affine->OAMIndex;
            if (affineTotalPriority < member.TotalPriority)
                SetOAMMaskBit(member.AffineInFrontOAMMask, affine->OAMIndex);
            else
                SetOAMMaskBit(member.AffineBehindOAMMask, affine->OAMIndex);
        }

        for (const auto* preserved : preservedInputs)
        {
            const int preservedTotalPriority =
                static_cast<int>(preserved->Priority) * 128 +
                preserved->OAMIndex;
            if (preservedTotalPriority < member.TotalPriority)
                SetOAMMaskBit(member.PreservedInFrontOAMMask,
                              preserved->OAMIndex);
            else
                SetOAMMaskBit(member.PreservedBehindOAMMask,
                              preserved->OAMIndex);
        }

        for (int layer = 0; layer < 4; layer++)
        {
            const u32 layerMask = 1u << layer;
            if ((summary.AffineBGMask & layerMask) == 0)
                continue;

            // At equal BG priority, OBJ is evaluated after BG and therefore
            // appears in front. Only a numerically lower-priority BG is in
            // front of this ordinary OBJ role.
            if (context.BGPriority[layer] < member.Priority)
                member.AffineBGInFrontMask |= layerMask;
            else
                member.AffineBGBehindMask |= layerMask;
        }

        summary.RejectionMask |= member.RejectionMask;
        const bool inactiveOBJWindow =
            input.OBJMode == 2 && !context.OBJWindowEnabled;
        const bool preservedSpecialRole =
            (input.OBJMode == 1 || input.OBJMode == 3) &&
            input.SourceType < 2 && !input.Mosaic &&
            member.RejectionMask == OBJBandRejectNonNormalMode;
        if (!inactiveOBJWindow && !preservedSpecialRole)
            summary.BlockingRejectionMask |= member.RejectionMask;
        if (member.RejectionMask != OBJBandRejectNone)
        {
            summary.RejectedOrdinaryCount++;
            summary.SplitAffineGroupMemberCount +=
                (member.RejectionMask & OBJBandRejectSplitsAffineGroup) != 0;
            result.Members.push_back(member);
            continue;
        }

        summary.SupportedOrdinaryCount++;
        auto bandIt = std::find_if(
            result.Bands.begin(), result.Bands.end(),
            [&member](const WholeSceneOrdinaryOBJBand& band)
            {
                return SameRelation(band, member);
            });
        if (bandIt == result.Bands.end())
        {
            WholeSceneOrdinaryOBJBand band;
            band.BandIndex = static_cast<int>(result.Bands.size());
            band.MinTotalPriority = member.TotalPriority;
            band.MaxTotalPriority = member.TotalPriority;
            band.AffineInFrontOAMMask[0] = member.AffineInFrontOAMMask[0];
            band.AffineInFrontOAMMask[1] = member.AffineInFrontOAMMask[1];
            band.AffineBehindOAMMask[0] = member.AffineBehindOAMMask[0];
            band.AffineBehindOAMMask[1] = member.AffineBehindOAMMask[1];
            band.PreservedInFrontOAMMask[0] =
                member.PreservedInFrontOAMMask[0];
            band.PreservedInFrontOAMMask[1] =
                member.PreservedInFrontOAMMask[1];
            band.PreservedBehindOAMMask[0] =
                member.PreservedBehindOAMMask[0];
            band.PreservedBehindOAMMask[1] =
                member.PreservedBehindOAMMask[1];
            band.AffineBGInFrontMask = member.AffineBGInFrontMask;
            band.AffineBGBehindMask = member.AffineBGBehindMask;
            u64 relationHash = kFNVOffset;
            HashValue(relationHash, band.AffineInFrontOAMMask[0]);
            HashValue(relationHash, band.AffineInFrontOAMMask[1]);
            HashValue(relationHash, band.AffineBehindOAMMask[0]);
            HashValue(relationHash, band.AffineBehindOAMMask[1]);
            HashValue(relationHash, band.PreservedInFrontOAMMask[0]);
            HashValue(relationHash, band.PreservedInFrontOAMMask[1]);
            HashValue(relationHash, band.PreservedBehindOAMMask[0]);
            HashValue(relationHash, band.PreservedBehindOAMMask[1]);
            HashValue(relationHash, band.AffineBGInFrontMask);
            HashValue(relationHash, band.AffineBGBehindMask);
            band.RelationHash = relationHash ? relationHash : 1;
            result.Bands.push_back(band);
            bandIt = result.Bands.end() - 1;
        }

        member.BandIndex = bandIt->BandIndex;
        bandIt->MemberCount++;
        bandIt->MinTotalPriority =
            std::min(bandIt->MinTotalPriority, member.TotalPriority);
        bandIt->MaxTotalPriority =
            std::max(bandIt->MaxTotalPriority, member.TotalPriority);
        SetOAMMaskBit(bandIt->MemberOAMMask, member.OAMIndex);
        result.Members.push_back(member);
    }

    if (summary.SupportedOrdinaryCount == 0)
    {
        summary.RejectionMask |= OBJBandRejectNoOrdinaryOBJ;
        // An affine-only ordered product needs no ordinary band texture. The
        // affine entries themselves remain fully classified and may form a
        // valid presentation plan when every group is otherwise admitted.
        if (affineInputs.empty())
            summary.BlockingRejectionMask |= OBJBandRejectNoOrdinaryOBJ;
    }

    std::sort(result.Bands.begin(), result.Bands.end(),
              [](const WholeSceneOrdinaryOBJBand& first,
                 const WholeSceneOrdinaryOBJBand& second)
              {
                  return first.MinTotalPriority < second.MinTotalPriority;
              });
    std::vector<int> reorderedBandIndices(result.Bands.size(), -1);
    for (size_t bandIndex = 0; bandIndex < result.Bands.size(); bandIndex++)
    {
        const int oldIndex = result.Bands[bandIndex].BandIndex;
        reorderedBandIndices[oldIndex] = static_cast<int>(bandIndex);
        result.Bands[bandIndex].BandIndex = static_cast<int>(bandIndex);
        summary.MaxBandMemberCount = std::max(
            summary.MaxBandMemberCount, result.Bands[bandIndex].MemberCount);
    }
    for (auto& member : result.Members)
    {
        if (member.BandIndex >= 0)
            member.BandIndex = reorderedBandIndices[member.BandIndex];
    }

    summary.BandCount = static_cast<int>(result.Bands.size());
    if (summary.BandCount > summary.BandLimit)
    {
        summary.RejectionMask |= OBJBandRejectTooManyBands;
        summary.BlockingRejectionMask |= OBJBandRejectTooManyBands;
    }

    u64 classificationHash = kFNVOffset;
    for (const auto& input : inputs)
    {
        HashValue(classificationHash, static_cast<u32>(input.OAMIndex));
        HashValue(classificationHash, input.Priority);
        HashValue(classificationHash, input.OBJMode);
        HashValue(classificationHash, input.SourceType);
        HashValue(classificationHash, input.Mosaic);
        HashValue(classificationHash, input.IsAffine);
        HashValue(classificationHash,
                  static_cast<u32>(input.AffineGroupKey));
        HashValue(classificationHash, input.AffineGroupReady);
    }
    HashValue(classificationHash, summary.AffineBGMask);
    for (int layer = 0; layer < 4; layer++)
        HashValue(classificationHash, context.BGPriority[layer]);
    HashValue(classificationHash, context.OBJWindowEnabled);
    for (const auto& member : result.Members)
    {
        HashValue(classificationHash, static_cast<u32>(member.OAMIndex));
        HashValue(classificationHash, static_cast<u32>(member.BandIndex));
        HashValue(classificationHash, member.AffineInFrontOAMMask[0]);
        HashValue(classificationHash, member.AffineInFrontOAMMask[1]);
        HashValue(classificationHash, member.PreservedInFrontOAMMask[0]);
        HashValue(classificationHash, member.PreservedInFrontOAMMask[1]);
        HashValue(classificationHash, member.PreservedBehindOAMMask[0]);
        HashValue(classificationHash, member.PreservedBehindOAMMask[1]);
        HashValue(classificationHash, member.AffineBGInFrontMask);
        HashValue(classificationHash, member.AffineBGBehindMask);
        HashValue(classificationHash, member.RejectionMask);
    }
    summary.ClassificationHash = classificationHash ? classificationHash : 1;
    summary.RecipeReady =
        summary.BlockingRejectionMask == OBJBandRejectNone;
    return result;
}

WholeSceneOBJOperandPlan BuildWholeSceneOBJForegroundOperandPlan(
    const WholeSceneOBJBandClassification& classification,
    u32 enabledBGMask,
    const u32 (&bgPriority)[4],
    const WholeSceneOBJOperandPlanOptions& options,
    int operandLimit)
{
    WholeSceneOBJOperandPlan plan;
    plan.ClassificationHash = classification.Summary.ClassificationHash;
    const auto& summary = classification.Summary;

    if (!summary.RecipeReady)
        plan.RejectionMask |= OBJOperandPlanRejectClassification;
    const bool hasAffineOBJ =
        summary.AffineCount != 0 && !classification.AffineInputs.empty();
    if (!hasAffineOBJ)
    {
        if (!options.AllowNoAffineOBJ)
            plan.RejectionMask |= OBJOperandPlanRejectNoAffineOBJ;
        else if (!AreSupportedSpecialOBJsIsolated(
                     classification, options.SpecialIsolationMargin))
            plan.RejectionMask |= OBJOperandPlanRejectUnisolatedSpecial;
    }
    if ((summary.BandCount == 0 || classification.Bands.empty()) &&
        !hasAffineOBJ)
        plan.RejectionMask |= OBJOperandPlanRejectNoOrdinaryBand;

    std::vector<WholeSceneOBJOperand> operands;
    operands.reserve(classification.Bands.size() +
                     classification.AffineInputs.size() +
                     classification.Members.size());

    for (const auto& band : classification.Bands)
    {
        WholeSceneOBJOperand operand;
        operand.Kind = WholeSceneOBJOperandKind::OrdinaryBand;
        operand.BandIndex = band.BandIndex;
        operand.PresentationIndex = band.BandIndex;
        operand.MinTotalPriority = band.MinTotalPriority;
        operand.MaxTotalPriority = band.MaxTotalPriority;
        operand.OAMMask[0] = band.MemberOAMMask[0];
        operand.OAMMask[1] = band.MemberOAMMask[1];
        operands.push_back(operand);
    }

    int nextPresentationIndex = summary.BandCount;
    for (const auto& input : classification.AffineInputs)
    {
        const int totalPriority = static_cast<int>(input.Priority) * 128 +
                                  input.OAMIndex;
        WholeSceneOBJOperand operand;
        operand.Kind = WholeSceneOBJOperandKind::AffinePartition;
        operand.MinTotalPriority = totalPriority;
        operand.MaxTotalPriority = totalPriority;
        SetOAMMaskBit(operand.OAMMask, input.OAMIndex);
        operands.push_back(operand);

        // A front BG is retained by native semantic ownership at insertion
        // time. It is not a reason to discard the affine operand globally.
    }

    for (const auto& member : classification.Members)
    {
        if (member.RejectionMask == OBJBandRejectNone)
        {
            for (int layer = 0; layer < 4; layer++)
            {
                if ((enabledBGMask & (1u << layer)) != 0 &&
                    member.Priority > bgPriority[layer])
                {
                    plan.RejectionMask |=
                        OBJOperandPlanRejectBehindEnabledBG;
                }
            }
            continue;
        }

        const bool supportedSpecial =
            member.OBJMode == 1 && member.SourceType < 2 && !member.Mosaic &&
            member.RejectionMask == OBJBandRejectNonNormalMode;
        if (!supportedSpecial)
        {
            plan.RejectionMask |= OBJOperandPlanRejectUnsupportedSpecial;
            continue;
        }

        WholeSceneOBJOperand operand;
        operand.Kind = WholeSceneOBJOperandKind::SemiTransparentOBJ;
        // Normal affine coverage is reconstructed by the adjacent ordered
        // 2x partitions. Keep an affine mode-1 role as a semantic operator;
        // do not allocate and rerun a separate full-screen scaler product for
        // it. Ordinary mode-1 roles retain their existing presentation layer.
        operand.PresentationIndex = member.IsAffine
            ? -1 : nextPresentationIndex++;
        operand.MinTotalPriority = member.TotalPriority;
        operand.MaxTotalPriority = member.TotalPriority;
        operand.OBJMode = member.OBJMode;
        SetOAMMaskBit(operand.OAMMask, member.OAMIndex);
        operands.push_back(operand);
    }

    std::sort(operands.begin(), operands.end(),
              [](const WholeSceneOBJOperand& first,
                 const WholeSceneOBJOperand& second)
              {
                  return first.MinTotalPriority > second.MinTotalPriority;
              });
    for (size_t index = 1; index < operands.size(); index++)
    {
        if (operands[index - 1].MinTotalPriority <=
            operands[index].MaxTotalPriority)
        {
            plan.RejectionMask |= OBJOperandPlanRejectOverlappingRanges;
        }
    }

    // Adjacent affine groups have no semantic operand between them, so one
    // selected-sprite render preserves their internal depth/OAM order while
    // avoiding one full-screen pass per singleton sprite.
    for (const auto& operand : operands)
    {
        if (!plan.BackToFront.empty() &&
            operand.Kind == WholeSceneOBJOperandKind::AffinePartition &&
            plan.BackToFront.back().Kind ==
                WholeSceneOBJOperandKind::AffinePartition &&
            operand.MinTotalPriority / 128 ==
                plan.BackToFront.back().MinTotalPriority / 128)
        {
            auto& partition = plan.BackToFront.back();
            partition.MinTotalPriority =
                std::min(partition.MinTotalPriority,
                         operand.MinTotalPriority);
            partition.MaxTotalPriority =
                std::max(partition.MaxTotalPriority,
                         operand.MaxTotalPriority);
            partition.OAMMask[0] |= operand.OAMMask[0];
            partition.OAMMask[1] |= operand.OAMMask[1];
            continue;
        }
        auto emitted = operand;
        if (emitted.Kind == WholeSceneOBJOperandKind::AffinePartition)
            emitted.BandIndex = -1;
        plan.BackToFront.push_back(emitted);
    }

    if (static_cast<int>(plan.BackToFront.size()) >
        std::max(operandLimit, 0))
    {
        plan.RejectionMask |= OBJOperandPlanRejectTooManyOperands;
    }

    u64 hash = kFNVOffset;
    HashValue(hash, plan.ClassificationHash);
    HashValue(hash, enabledBGMask);
    if (options.AllowNoAffineOBJ)
    {
        HashValue(hash, options.AllowNoAffineOBJ);
        HashValue(hash, static_cast<u32>(
            std::max(options.SpecialIsolationMargin, 0)));
    }
    for (int layer = 0; layer < 4; layer++)
        HashValue(hash, bgPriority[layer]);
    for (const auto& operand : plan.BackToFront)
    {
        HashValue(hash, static_cast<u32>(operand.Kind));
        HashValue(hash, static_cast<u32>(operand.BandIndex));
        HashValue(hash, static_cast<u32>(operand.PresentationIndex));
        HashValue(hash, static_cast<u32>(operand.MinTotalPriority));
        HashValue(hash, static_cast<u32>(operand.MaxTotalPriority));
        HashValue(hash, operand.OBJMode);
        HashValue(hash, operand.OAMMask[0]);
        HashValue(hash, operand.OAMMask[1]);
    }
    HashValue(hash, plan.RejectionMask);
    plan.PlanHash = hash ? hash : 1;
    plan.Ready = plan.RejectionMask == OBJOperandPlanRejectNone;
    return plan;
}

bool IsWholeSceneOBJBandSingleForegroundPreservedBehindRecipe(
    const WholeSceneOBJBandClassification& classification,
    u32 enabledBGMask,
    const u32 (&bgPriority)[4])
{
    const auto& summary = classification.Summary;
    if (!summary.RecipeReady || summary.AffineCount != 0 ||
        summary.RejectedOrdinaryCount == 0 || summary.AffineBGMask == 0 ||
        summary.BandCount != 1 || classification.Bands.size() != 1 ||
        classification.Bands[0].AffineBGBehindMask != summary.AffineBGMask ||
        classification.Bands[0].PreservedInFrontOAMMask[0] != 0 ||
        classification.Bands[0].PreservedInFrontOAMMask[1] != 0)
    {
        return false;
    }

    bool foundPreservedRole = false;
    for (const auto& member : classification.Members)
    {
        if (member.RejectionMask != OBJBandRejectNone)
        {
            const bool preservedSpecialRole =
                (member.OBJMode == 1 || member.OBJMode == 3) &&
                member.SourceType < 2 && !member.Mosaic &&
                member.RejectionMask == OBJBandRejectNonNormalMode;
            if (!preservedSpecialRole ||
                member.TotalPriority <= classification.Bands[0].MaxTotalPriority)
            {
                return false;
            }
            foundPreservedRole = true;
            continue;
        }

        for (int layer = 0; layer < 4; layer++)
        {
            if ((enabledBGMask & (1u << layer)) != 0 &&
                member.Priority > bgPriority[layer])
            {
                return false;
            }
        }
    }
    return foundPreservedRole;
}

bool IsWholeSceneOBJBandSingleForegroundRecipe(
    const WholeSceneOBJBandClassification& classification,
    u32 enabledBGMask,
    const u32 (&bgPriority)[4])
{
    const auto& summary = classification.Summary;
    if (!summary.RecipeReady || summary.AffineCount != 0 ||
        summary.RejectedOrdinaryCount != 0 || summary.AffineBGMask == 0 ||
        summary.BandCount != 1 || classification.Bands.size() != 1 ||
        classification.Bands[0].AffineBGBehindMask != summary.AffineBGMask)
    {
        return false;
    }

    for (const auto& member : classification.Members)
    {
        if (member.RejectionMask != OBJBandRejectNone)
            return false;
        for (int layer = 0; layer < 4; layer++)
        {
            if ((enabledBGMask & (1u << layer)) != 0 &&
                member.Priority > bgPriority[layer])
            {
                return false;
            }
        }
    }
    return !classification.Members.empty();
}

bool IsWholeSceneOBJBandTwoForegroundBandsAroundPreservedSpecialRecipe(
    const WholeSceneOBJBandClassification& classification,
    u32 enabledBGMask,
    const u32 (&bgPriority)[4])
{
    const auto& summary = classification.Summary;
    if (!summary.RecipeReady || summary.AffineCount == 0 ||
        summary.RejectedOrdinaryCount != 1 || summary.AffineBGMask == 0 ||
        summary.BandCount != 2 || classification.Bands.size() != 2)
    {
        return false;
    }

    const auto& front = classification.Bands[0];
    const auto& rear = classification.Bands[1];
    if (front.AffineInFrontOAMMask[0] != 0 ||
        front.AffineInFrontOAMMask[1] != 0 ||
        front.AffineBehindOAMMask[0] != summary.AffineOAMMask[0] ||
        front.AffineBehindOAMMask[1] != summary.AffineOAMMask[1] ||
        (rear.AffineInFrontOAMMask[0] |
         rear.AffineBehindOAMMask[0]) != summary.AffineOAMMask[0] ||
        (rear.AffineInFrontOAMMask[1] |
         rear.AffineBehindOAMMask[1]) != summary.AffineOAMMask[1] ||
        (rear.AffineInFrontOAMMask[0] &
         rear.AffineBehindOAMMask[0]) != 0 ||
        (rear.AffineInFrontOAMMask[1] &
         rear.AffineBehindOAMMask[1]) != 0 ||
        front.AffineBGBehindMask != summary.AffineBGMask ||
        rear.AffineBGBehindMask != summary.AffineBGMask)
    {
        return false;
    }

    const WholeSceneOrdinaryOBJBandMember* special = nullptr;
    for (const auto& member : classification.Members)
    {
        if (member.RejectionMask != OBJBandRejectNone)
        {
            const bool supportedSemiTransparentRole =
                member.OBJMode == 1 && member.SourceType < 2 &&
                !member.Mosaic &&
                member.RejectionMask == OBJBandRejectNonNormalMode;
            if (!supportedSemiTransparentRole || special != nullptr)
                return false;
            special = &member;
            continue;
        }

        for (int layer = 0; layer < 4; layer++)
        {
            if ((enabledBGMask & (1u << layer)) != 0 &&
                member.Priority > bgPriority[layer])
            {
                return false;
            }
        }
    }

    if (!special ||
        special->TotalPriority <= front.MaxTotalPriority ||
        special->TotalPriority >= rear.MinTotalPriority ||
        special->AffineInFrontOAMMask[0] !=
            rear.AffineInFrontOAMMask[0] ||
        special->AffineInFrontOAMMask[1] !=
            rear.AffineInFrontOAMMask[1] ||
        special->AffineBehindOAMMask[0] !=
            rear.AffineBehindOAMMask[0] ||
        special->AffineBehindOAMMask[1] !=
            rear.AffineBehindOAMMask[1])
    {
        return false;
    }

    const int word = special->OAMIndex >> 6;
    const u64 bit = 1ull << (special->OAMIndex & 63);
    return word >= 0 && word < 2 &&
           (front.PreservedBehindOAMMask[word] & bit) != 0 &&
           (rear.PreservedInFrontOAMMask[word] & bit) != 0;
}

}
