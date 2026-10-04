// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "types.h"

#include <vector>

namespace melonDS
{

inline constexpr int WholeSceneOBJBandLimit = 8;
inline constexpr int WholeSceneOBJPresentationProductLimit = 16;
inline constexpr int WholeSceneOBJOperandLimit = 48;

enum WholeSceneOBJBandRejection : u32
{
    OBJBandRejectNone = 0,
    OBJBandRejectNoAffineOBJ = 1u << 0,
    OBJBandRejectNoOrdinaryOBJ = 1u << 1,
    OBJBandRejectNonNormalMode = 1u << 2,
    OBJBandRejectUnsupportedSource = 1u << 3,
    OBJBandRejectMosaic = 1u << 4,
    OBJBandRejectUnprovenAffineGroup = 1u << 5,
    OBJBandRejectSplitsAffineGroup = 1u << 6,
    OBJBandRejectTooManyBands = 1u << 7,
};

struct WholeSceneOBJBandInput
{
    int RenderedIndex = -1;
    int OAMIndex = -1;
    u32 Priority = 0;
    u32 OBJMode = 0;
    u32 SourceType = 0;
    bool Mosaic = false;
    bool IsAffine = false;
    int AffineGroupKey = -1;
    bool AffineGroupReady = false;
    int PositionX = 0;
    int PositionY = 0;
    int BoundWidth = 0;
    int BoundHeight = 0;
};

struct WholeSceneOBJBandContext
{
    u32 AffineBGMask = 0;
    u32 BGPriority[4] {};
    bool OBJWindowEnabled = true;
};

struct WholeSceneOrdinaryOBJBandMember
{
    int RenderedIndex = -1;
    int OAMIndex = -1;
    int TotalPriority = -1;
    int BandIndex = -1;
    u32 Priority = 0;
    u32 OBJMode = 0;
    u32 SourceType = 0;
    bool Mosaic = false;
    bool IsAffine = false;
    int PositionX = 0;
    int PositionY = 0;
    int BoundWidth = 0;
    int BoundHeight = 0;
    u64 AffineInFrontOAMMask[2] {};
    u64 AffineBehindOAMMask[2] {};
    u64 PreservedInFrontOAMMask[2] {};
    u64 PreservedBehindOAMMask[2] {};
    u32 AffineBGInFrontMask = 0;
    u32 AffineBGBehindMask = 0;
    u32 RejectionMask = OBJBandRejectNone;
};

struct WholeSceneOrdinaryOBJBand
{
    int BandIndex = -1;
    int MemberCount = 0;
    int MinTotalPriority = -1;
    int MaxTotalPriority = -1;
    u64 MemberOAMMask[2] {};
    u64 AffineInFrontOAMMask[2] {};
    u64 AffineBehindOAMMask[2] {};
    u64 PreservedInFrontOAMMask[2] {};
    u64 PreservedBehindOAMMask[2] {};
    u32 AffineBGInFrontMask = 0;
    u32 AffineBGBehindMask = 0;
    u64 RelationHash = 0;
};

struct WholeSceneOBJBandSummary
{
    int InputCount = 0;
    int AffineCount = 0;
    int OrdinaryCount = 0;
    int SupportedOrdinaryCount = 0;
    int RejectedOrdinaryCount = 0;
    int BandCount = 0;
    int MaxBandMemberCount = 0;
    int SplitAffineGroupMemberCount = 0;
    int BandLimit = 0;
    bool RecipeReady = false;
    u32 RejectionMask = OBJBandRejectNone;
    u32 BlockingRejectionMask = OBJBandRejectNone;
    u64 AffineOAMMask[2] {};
    u32 AffineBGMask = 0;
    u64 ClassificationHash = 0;
};

struct WholeSceneOBJBandClassification
{
    WholeSceneOBJBandSummary Summary;
    std::vector<WholeSceneOBJBandInput> AffineInputs;
    std::vector<WholeSceneOrdinaryOBJBandMember> Members;
    std::vector<WholeSceneOrdinaryOBJBand> Bands;
};

enum class WholeSceneOBJOperandKind : u32
{
    OrdinaryBand = 0,
    AffinePartition = 1,
    SemiTransparentOBJ = 2,
};

enum WholeSceneOBJOperandPlanRejection : u32
{
    OBJOperandPlanRejectNone = 0,
    OBJOperandPlanRejectClassification = 1u << 0,
    OBJOperandPlanRejectNoAffineOBJ = 1u << 1,
    OBJOperandPlanRejectNoOrdinaryBand = 1u << 2,
    OBJOperandPlanRejectUnsupportedSpecial = 1u << 3,
    OBJOperandPlanRejectBehindEnabledBG = 1u << 4,
    OBJOperandPlanRejectOverlappingRanges = 1u << 5,
    OBJOperandPlanRejectTooManyOperands = 1u << 6,
    OBJOperandPlanRejectUnisolatedSpecial = 1u << 7,
};

struct WholeSceneOBJOperandPlanOptions
{
    bool AllowNoAffineOBJ = false;
    int SpecialIsolationMargin = 0;
};

struct WholeSceneOBJOperand
{
    WholeSceneOBJOperandKind Kind = WholeSceneOBJOperandKind::OrdinaryBand;
    int BandIndex = -1;
    int PresentationIndex = -1;
    int MinTotalPriority = -1;
    int MaxTotalPriority = -1;
    u32 OBJMode = 0;
    u64 OAMMask[2] {};
};

struct WholeSceneOBJOperandPlan
{
    bool Ready = false;
    u32 RejectionMask = OBJOperandPlanRejectNone;
    u64 ClassificationHash = 0;
    u64 PlanHash = 0;
    std::vector<WholeSceneOBJOperand> BackToFront;
};

WholeSceneOBJBandClassification ClassifyWholeSceneOBJBands(
    const std::vector<WholeSceneOBJBandInput>& inputs,
    int bandLimit = WholeSceneOBJBandLimit,
    const WholeSceneOBJBandContext& context = {});

WholeSceneOBJOperandPlan BuildWholeSceneOBJForegroundOperandPlan(
    const WholeSceneOBJBandClassification& classification,
    u32 enabledBGMask,
    const u32 (&bgPriority)[4],
    const WholeSceneOBJOperandPlanOptions& options = {},
    int operandLimit = WholeSceneOBJOperandLimit);

bool IsWholeSceneOBJBandSingleForegroundRecipe(
    const WholeSceneOBJBandClassification& classification,
    u32 enabledBGMask,
    const u32 (&bgPriority)[4]);

bool IsWholeSceneOBJBandSingleForegroundPreservedBehindRecipe(
    const WholeSceneOBJBandClassification& classification,
    u32 enabledBGMask,
    const u32 (&bgPriority)[4]);

bool IsWholeSceneOBJBandTwoForegroundBandsAroundPreservedSpecialRecipe(
    const WholeSceneOBJBandClassification& classification,
    u32 enabledBGMask,
    const u32 (&bgPriority)[4]);

}
