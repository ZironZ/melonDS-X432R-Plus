// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "WholeSceneOutputPlan.h"

namespace melonDS
{

static constexpr u64 ChannelBit(WholeSceneOutputChannel channel)
{
    return static_cast<u64>(channel);
}

static void MoveChannels(u64 mask, u64& from, u64& to)
{
    const u64 moved = from & mask;
    from &= ~mask;
    to |= moved;
}

static void AddOutputTransform(
    WholeSceneOutputPlan& plan,
    WholeSceneOutputTransformKind kind,
    WholeSceneOutputInsertionStage owningStage,
    int ystart,
    int yend,
    u32 state,
    u32 auxState = 0,
    WholeScenePhysicalTarget target = WholeScenePhysicalTarget::Unknown)
{
    if (plan.TransformCount >= WholeSceneMaxOutputTransforms)
        return;

    WholeSceneOutputTransform& transform = plan.Transforms[plan.TransformCount++];
    transform.Kind = kind;
    transform.OwningStage = owningStage;
    transform.YStart = ystart;
    transform.YEnd = yend;
    transform.State = state;
    transform.AuxState = auxState;
    transform.Target = target;
    plan.PlannedTransformMask |= WholeSceneOutputTransformBit(kind);
}

WholeSceneOutputRepresentationKind WholeSceneOutputRepresentationForPath(
    WholeSceneRenderPath path)
{
    switch (path)
    {
    case WholeSceneRenderPath::Current:
        return WholeSceneOutputRepresentationKind::Current;
    case WholeSceneRenderPath::LegacyNativeUpscale:
        return WholeSceneOutputRepresentationKind::NativeStack;
    case WholeSceneRenderPath::HighResCompositor:
        return WholeSceneOutputRepresentationKind::HighResCompositor;
    case WholeSceneRenderPath::FinalNativeUpscale:
        return WholeSceneOutputRepresentationKind::FinalNative;
    case WholeSceneRenderPath::OverlayOperatorUpscale:
        return WholeSceneOutputRepresentationKind::OverlayOperator;
    case WholeSceneRenderPath::ConservativeHybridUpscale:
        return WholeSceneOutputRepresentationKind::ConservativeHybrid;
    case WholeSceneRenderPath::CaptureBackedHandoff:
        return WholeSceneOutputRepresentationKind::CaptureBackedHandoff;
    case WholeSceneRenderPath::SourceACaptureReplacement:
        return WholeSceneOutputRepresentationKind::SourceACaptureReplacement;
    case WholeSceneRenderPath::CaptureEpochOverlay:
        return WholeSceneOutputRepresentationKind::CaptureEpochOverlay;
    case WholeSceneRenderPath::PhysicalFinalPostprocessInput:
        return WholeSceneOutputRepresentationKind::PhysicalFinalPostprocessInput;
    case WholeSceneRenderPath::NativeExactFloor:
        return WholeSceneOutputRepresentationKind::NativeExactFloor;
    case WholeSceneRenderPath::StrictAffineHighRes:
        return WholeSceneOutputRepresentationKind::StrictAffineHighRes;
    case WholeSceneRenderPath::None:
    default:
        return WholeSceneOutputRepresentationKind::None;
    }
}

static bool PathUsesSelector(WholeSceneRenderPath path)
{
    return path == WholeSceneRenderPath::OverlayOperatorUpscale ||
           path == WholeSceneRenderPath::ConservativeHybridUpscale ||
           path == WholeSceneRenderPath::CaptureBackedHandoff ||
           path == WholeSceneRenderPath::SourceACaptureReplacement ||
           path == WholeSceneRenderPath::CaptureEpochOverlay;
}

static WholeSceneOutputRepresentationProduct
BuildWholeSceneOutputRepresentationProduct(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOutputPlanInputs& inputs);

static bool HasProvenCaptureEpochOverlayInput(
    const WholeSceneOutputPlan& plan,
    u32 captureCnt)
{
    const auto& evidence = plan.RepresentationEvidence;
    const u32 captureMode = (captureCnt >> 29) & 0x3u;
    const u32 captureEVB = std::min<u32>((captureCnt >> 8) & 0x1Fu, 16u);
    const bool captureUsesSourceB =
        captureMode == 1 || (captureMode == 2 && captureEVB > 0);
    return plan.SelectedRepresentation ==
               WholeSceneOutputRepresentationKind::CaptureEpochOverlay &&
           evidence.Available &&
           evidence.ContentProven &&
           evidence.PresentationProven &&
           evidence.ProductRef.Kind ==
               WholeSceneCaptureProductKind::ParentOutput3D &&
           evidence.ProductRef.BackgroundSource ==
               SourceABackgroundSource::ParentOutputTex3D &&
           evidence.ProofKind ==
               WholeSceneCaptureProofKind::RouteStateIdentity &&
           evidence.AuthorizationProofKind ==
               WholeSceneCaptureProofKind::ActiveBackgroundEpoch &&
           evidence.AuthorizationEpochSerial != 0 &&
           evidence.AuthorizationCaptureBank < 4 &&
           evidence.Source3DSerial != 0 &&
           evidence.Source3DSceneHash != 0 &&
           evidence.ProductRef.CapturePresentationHash != 0 &&
           evidence.ProductRef.CapturePresentationHash ==
               evidence.ProductRef.CurrentPresentationHash &&
           evidence.AuthorizationPresentationHash != 0 &&
           evidence.OutputEffectOwner ==
               WholeSceneCaptureEffectOwner::CurrentEngineColorEffect &&
           evidence.OutputPreMaster &&
           evidence.CaptureInputProven &&
           evidence.CaptureCnt == captureCnt &&
           (captureCnt & (1u << 31)) != 0 &&
           ((captureCnt >> 24) & 0x1u) == 0 &&
           evidence.CaptureConsumesSelectedPresentation &&
           !captureUsesSourceB &&
           !evidence.CaptureUsesSourceB;
}

static void ResetWholeSceneOutputPlanAssessment(WholeSceneOutputPlan& plan)
{
    plan.InsertionStage = WholeSceneOutputInsertionStage::UnclassifiedLegacy;
    plan.CorrectnessClass = WholeSceneOutputCorrectnessClass::Unclassified;
    plan.ChannelsAssessed = false;
    plan.RequiredChannels = 0;
    plan.ReproducedChannels = 0;
    plan.InactiveChannels = 0;
    plan.UnsupportedChannels = 0;
    plan.EvidenceUnavailableChannels = 0;
    plan.EffectEvidence = {};
    plan.TransformCount = 0;
    plan.PlannedTransformMask = 0;
    plan.Transforms = {};
}

void AssessWholeSceneOutputPlan(
    WholeSceneOutputPlan& plan,
    const WholeSceneOutputPlanInputs& inputs)
{
    if (!plan.Valid)
        return;

    ResetWholeSceneOutputPlanAssessment(plan);

    switch (plan.SelectedRepresentation)
    {
    case WholeSceneOutputRepresentationKind::Current:
        if (inputs.OutputScale <= 1)
        {
            plan.InsertionStage = WholeSceneOutputInsertionStage::NativeExactFloor;
            plan.CorrectnessClass = WholeSceneOutputCorrectnessClass::NativeExact;
        }
        else
        {
            plan.InsertionStage = WholeSceneOutputInsertionStage::HighResComposition;
            plan.CorrectnessClass = WholeSceneOutputCorrectnessClass::HeuristicHighRes;
        }
        break;
    case WholeSceneOutputRepresentationKind::NativeStack:
    case WholeSceneOutputRepresentationKind::FinalNative:
        plan.InsertionStage = WholeSceneOutputInsertionStage::CompositorProductUpscale;
        plan.CorrectnessClass = WholeSceneOutputCorrectnessClass::NativeEnhanced;
        break;
    case WholeSceneOutputRepresentationKind::HighResCompositor:
    case WholeSceneOutputRepresentationKind::StrictAffineHighRes:
        plan.InsertionStage = WholeSceneOutputInsertionStage::HighResComposition;
        plan.CorrectnessClass = WholeSceneOutputCorrectnessClass::ModelCorrectHighRes;
        break;
    case WholeSceneOutputRepresentationKind::OverlayOperator:
    case WholeSceneOutputRepresentationKind::ConservativeHybrid:
        plan.InsertionStage = WholeSceneOutputInsertionStage::CompositorProductUpscale;
        plan.CorrectnessClass = WholeSceneOutputCorrectnessClass::HeuristicHighRes;
        break;
    case WholeSceneOutputRepresentationKind::CaptureBackedHandoff:
    case WholeSceneOutputRepresentationKind::SourceACaptureReplacement:
    case WholeSceneOutputRepresentationKind::CaptureEpochOverlay:
        plan.InsertionStage = WholeSceneOutputInsertionStage::DisplayProductUpscale;
        plan.CorrectnessClass = WholeSceneOutputCorrectnessClass::HeuristicHighRes;
        break;
    case WholeSceneOutputRepresentationKind::CurrentVRAMDisplay:
        plan.InsertionStage = WholeSceneOutputInsertionStage::DisplayProductUpscale;
        plan.CorrectnessClass = WholeSceneOutputCorrectnessClass::NativeEnhanced;
        break;
    case WholeSceneOutputRepresentationKind::PhysicalFinalPostprocessInput:
        plan.InsertionStage = WholeSceneOutputInsertionStage::NativeEnhancedPracticalFloor;
        plan.CorrectnessClass = WholeSceneOutputCorrectnessClass::NativeEnhanced;
        break;
    case WholeSceneOutputRepresentationKind::NativeExactFloor:
        plan.InsertionStage = WholeSceneOutputInsertionStage::NativeExactFloor;
        plan.CorrectnessClass = WholeSceneOutputCorrectnessClass::NativeExact;
        break;
    case WholeSceneOutputRepresentationKind::None:
    default:
        break;
    }

    plan.ChannelsAssessed = true;
    plan.RequiredChannels = inputs.RequiredChannels & WholeSceneOutputAllChannelMask;
    plan.InactiveChannels = WholeSceneOutputAllChannelMask & ~plan.RequiredChannels;
    plan.ReproducedChannels = plan.RequiredChannels;

    const u64 effectOwnershipChannels =
        ChannelBit(WholeSceneOutputChannel::ColorEffect) |
        ChannelBit(WholeSceneOutputChannel::AlphaBlend) |
        ChannelBit(WholeSceneOutputChannel::SemiTransparentOBJ);
    const u64 activeEffectOwnershipChannels =
        plan.RequiredChannels & effectOwnershipChannels;
    if (plan.Selector.Configured && plan.Selector.ActiveDirect3D &&
        activeEffectOwnershipChannels != 0)
    {
        auto& evidence = plan.EffectEvidence;
        evidence.Available = true;
        evidence.OperandSource =
            WholeSceneOutputEffectOperandSource::NativeStackMetadata;
        evidence.CandidateChannelMask = activeEffectOwnershipChannels;
        // No channel is promoted from register masks alone. Each bit moves to
        // ProvenOwnershipChannelMask only after its scale-1/downsample,
        // temporal and negative controls pass.
        evidence.ProvenOwnershipChannelMask = 0;
        evidence.BlendCnt = inputs.BlendCnt;
        evidence.BlendMode = (inputs.BlendCnt >> 6) & 0x3u;
        evidence.Target1Mask = inputs.BlendCnt & 0x3Fu;
        evidence.Target2Mask = (inputs.BlendCnt >> 8) & 0x3Fu;
        evidence.EVA = std::min<u8>(inputs.EVA, 16u);
        evidence.EVB = std::min<u8>(inputs.EVB, 16u);
        evidence.EVY = std::min<u8>(inputs.EVY, 16u);
        constexpr u8 direct3DMask = 1u << 0;
        evidence.Direct3DTarget1 =
            (evidence.Target1Mask & direct3DMask) != 0;
        evidence.Direct3DTarget2 =
            (evidence.Target2Mask & direct3DMask) != 0;
        evidence.SemiTransparentOBJModeMask =
            inputs.SemiTransparentOBJModeMask & 0x3u;
        constexpr u8 objMask = 1u << 4;
        evidence.OBJTarget1 = (evidence.Target1Mask & objMask) != 0;
        evidence.OBJTarget2 = (evidence.Target2Mask & objMask) != 0;

        // This bounded class has a repeat-stable positive replay family and
        // structural negative controls. NativeMetaTex supplies the per-pixel
        // top/second stack; Target-2 assist is required for cells where the
        // inserted Direct3D operand is second. Requiring Direct3D and a 2D
        // peer in both BLDCNT target sets keeps one-sided, inactive and
        // unrelated 2D blends in debt.
        const u64 alphaBlend =
            ChannelBit(WholeSceneOutputChannel::AlphaBlend);
        const u64 colorEffect =
            ChannelBit(WholeSceneOutputChannel::ColorEffect);
        constexpr u8 nonDirect3DMask = 0x3Eu;
        if (plan.Selector.Kind ==
                WholeSceneOutputSelectorKind::ConservativeHybrid &&
            plan.Selector.EffectiveConservativeHybrid &&
            plan.Selector.Target2AlphaBlendAssist &&
            evidence.BlendMode == 1 &&
            evidence.Direct3DTarget1 &&
            evidence.Direct3DTarget2 &&
            (evidence.Target1Mask & nonDirect3DMask) != 0 &&
            (evidence.Target2Mask & nonDirect3DMask) != 0 &&
            (evidence.CandidateChannelMask & alphaBlend) != 0)
        {
            evidence.ProofMask |= static_cast<u32>(WholeSceneOutputEffectProof::
                ConservativeHybridAlphaBothTargets);
            evidence.ProvenOwnershipChannelMask |= alphaBlend;
        }

        // Brightness effects target only the top stack operand. Keep the
        // first color-effect proof to darken: the qualified corpus covers
        // both partial and full EVY, with and without Target-2 peers. Brighten
        // and inactive EVY remain separate evidence classes.
        if (plan.Selector.Kind ==
                WholeSceneOutputSelectorKind::ConservativeHybrid &&
            plan.Selector.EffectiveConservativeHybrid &&
            evidence.BlendMode == 3 &&
            evidence.EVY > 0 &&
            evidence.Direct3DTarget1 &&
            (evidence.CandidateChannelMask & colorEffect) != 0)
        {
            evidence.ProofMask |= static_cast<u32>(WholeSceneOutputEffectProof::
                ConservativeHybridDirect3DDarken);
            evidence.ProvenOwnershipChannelMask |= colorEffect;
        }

        // Mode-1 OBJ uses EVA/EVB when its second stack source is a Target-2
        // participant. The first qualified class is intentionally degenerate:
        // EVA=16 and EVB=0 make the result independent of the second color.
        // Mode 3 and contributing second-operand coefficients remain debt.
        const u64 semiTransparentOBJ =
            ChannelBit(WholeSceneOutputChannel::SemiTransparentOBJ);
        if (plan.Selector.Kind ==
                WholeSceneOutputSelectorKind::ConservativeHybrid &&
            plan.Selector.EffectiveConservativeHybrid &&
            plan.Selector.Target2AlphaBlendAssist &&
            evidence.BlendMode == 3 &&
            evidence.EVA == 16 &&
            evidence.EVB == 0 &&
            evidence.Direct3DTarget1 &&
            evidence.Direct3DTarget2 &&
            evidence.OBJTarget1 &&
            evidence.OBJTarget2 &&
            evidence.SemiTransparentOBJModeMask == 1 &&
            (evidence.CandidateChannelMask & semiTransparentOBJ) != 0)
        {
            evidence.ProofMask |= static_cast<u32>(WholeSceneOutputEffectProof::
                ConservativeHybridMode1OBJZeroSecond);
            evidence.ProvenOwnershipChannelMask |= semiTransparentOBJ;
        }
    }

    auto requireEvidence = [&](u64 mask)
    {
        mask &= ~plan.EffectEvidence.ProvenOwnershipChannelMask;
        MoveChannels(mask, plan.ReproducedChannels, plan.EvidenceUnavailableChannels);
    };
    auto unsupported = [&](u64 mask)
    {
        MoveChannels(mask, plan.ReproducedChannels, plan.UnsupportedChannels);
        MoveChannels(mask, plan.EvidenceUnavailableChannels, plan.UnsupportedChannels);
    };

    const u64 objectModelChannels =
        ChannelBit(WholeSceneOutputChannel::OBJGeometry) |
        ChannelBit(WholeSceneOutputChannel::AffineOBJGeometry) |
        ChannelBit(WholeSceneOutputChannel::SemiTransparentOBJ);
    const u64 ownershipSensitiveChannels =
        ChannelBit(WholeSceneOutputChannel::AlphaBlend) |
        ChannelBit(WholeSceneOutputChannel::SemiTransparentOBJ);

    if (inputs.OutputScale > 1)
    {
        switch (plan.SelectedRepresentation)
        {
        case WholeSceneOutputRepresentationKind::Current:
        case WholeSceneOutputRepresentationKind::HighResCompositor:
            requireEvidence(objectModelChannels | ownershipSensitiveChannels);
            unsupported(ChannelBit(WholeSceneOutputChannel::AffineOBJGeometry) |
                        ChannelBit(WholeSceneOutputChannel::OBJWindow));
            break;
        case WholeSceneOutputRepresentationKind::StrictAffineHighRes:
        {
            constexpr u64 strictAffineChannels =
                ChannelBit(WholeSceneOutputChannel::TextBGGeometry) |
                ChannelBit(WholeSceneOutputChannel::AffineBGGeometry) |
                ChannelBit(WholeSceneOutputChannel::Direct3D) |
                ChannelBit(WholeSceneOutputChannel::OBJGeometry) |
                ChannelBit(WholeSceneOutputChannel::AffineOBJGeometry) |
                ChannelBit(WholeSceneOutputChannel::Priority) |
                ChannelBit(WholeSceneOutputChannel::Palette) |
                ChannelBit(WholeSceneOutputChannel::ColorEffect) |
                ChannelBit(WholeSceneOutputChannel::AlphaBlend) |
                ChannelBit(WholeSceneOutputChannel::SemiTransparentOBJ) |
                ChannelBit(WholeSceneOutputChannel::DisplaySelection) |
                ChannelBit(WholeSceneOutputChannel::MasterBrightness) |
                ChannelBit(WholeSceneOutputChannel::PhysicalRouting);
            unsupported(plan.RequiredChannels & ~strictAffineChannels);
            break;
        }
        case WholeSceneOutputRepresentationKind::OverlayOperator:
        case WholeSceneOutputRepresentationKind::CaptureBackedHandoff:
        case WholeSceneOutputRepresentationKind::SourceACaptureReplacement:
        case WholeSceneOutputRepresentationKind::CaptureEpochOverlay:
            requireEvidence(ownershipSensitiveChannels |
                            ChannelBit(WholeSceneOutputChannel::ColorEffect));
            unsupported(ChannelBit(WholeSceneOutputChannel::OBJWindow));
            break;
        case WholeSceneOutputRepresentationKind::ConservativeHybrid:
            // Without active Direct3D this representation explicitly renders
            // the complete native compositor fallback. Native alpha, BLDY and
            // semi-transparent OBJ are therefore already authoritative rather
            // than high-resolution operations needing separate ownership proof.
            if (plan.Selector.ActiveDirect3D)
            {
                requireEvidence(ownershipSensitiveChannels |
                                ChannelBit(WholeSceneOutputChannel::ColorEffect));
            }
            // Conservative Hybrid consumes the compositor's exact native
            // window-allow metadata. The window-edge assist keeps excluded
            // native cells and their operator boundary on the native/overlay
            // side, so the existing Advance Wars OBJ-window fixture is a
            // positive capability proof for this precise configuration.
            if (!plan.Selector.Configured || !plan.Selector.WindowEdgeAssist)
                unsupported(ChannelBit(WholeSceneOutputChannel::OBJWindow));
            break;
        default:
            break;
        }
    }

    // A physical-screen representation is not automatically valid input to
    // the earlier display-capture boundary. CaptureEpochOverlay is the first
    // family with both content authority and completed pre-master phase facts
    // available before admission. Post-execution capture observation verifies
    // this claim but does not create capability retroactively.
    if (!HasProvenCaptureEpochOverlayInput(plan, inputs.CaptureCnt))
        requireEvidence(ChannelBit(WholeSceneOutputChannel::DisplayCapture));

    if (plan.SelectedRepresentation !=
        WholeSceneOutputRepresentationKind::PhysicalFinalPostprocessInput)
    {
        requireEvidence(ChannelBit(WholeSceneOutputChannel::CaptureFeedback));
        if (plan.SelectedRepresentation !=
            WholeSceneOutputRepresentationKind::CurrentVRAMDisplay)
        {
            requireEvidence(ChannelBit(WholeSceneOutputChannel::VRAMDisplay));
        }
    }

    const u64 colorEffect = ChannelBit(WholeSceneOutputChannel::ColorEffect);
    const u64 displayCapture = ChannelBit(WholeSceneOutputChannel::DisplayCapture);
    const u64 displaySelection = ChannelBit(WholeSceneOutputChannel::DisplaySelection);
    const u64 masterBrightness = ChannelBit(WholeSceneOutputChannel::MasterBrightness);
    const u64 physicalRouting = ChannelBit(WholeSceneOutputChannel::PhysicalRouting);

    if (plan.RequiredChannels & colorEffect)
    {
        AddOutputTransform(plan,
                           WholeSceneOutputTransformKind::CompositorColorEffect,
                           WholeSceneOutputInsertionStage::HighResComposition,
                           plan.YStart,
                           plan.YEnd,
                           inputs.BlendCnt,
                           inputs.EVY);
    }
    if (plan.RequiredChannels & displayCapture)
    {
        const u32 captureSize = (inputs.CaptureCnt >> 20) & 0x3u;
        const int captureHeight = captureSize == 0
            ? 128
            : static_cast<int>(64 * captureSize);
        AddOutputTransform(plan,
                           WholeSceneOutputTransformKind::DisplayCapture,
                           WholeSceneOutputInsertionStage::DisplayProductUpscale,
                           plan.YStart,
                           std::min(plan.YEnd, captureHeight),
                           inputs.CaptureCnt);
    }
    if (plan.RequiredChannels & displaySelection)
    {
        AddOutputTransform(plan,
                           WholeSceneOutputTransformKind::DisplaySelection,
                           WholeSceneOutputInsertionStage::DisplayProductUpscale,
                           plan.YStart,
                           plan.YEnd,
                           inputs.DisplayMode);
    }
    if (plan.RequiredChannels & masterBrightness)
    {
        const u32 mode = (inputs.MasterBrightness >> 14) & 0x3u;
        const u32 factor = std::min<u32>(inputs.MasterBrightness & 0x1Fu, 16u);
        AddOutputTransform(plan,
                           WholeSceneOutputTransformKind::MasterBrightness,
                           WholeSceneOutputInsertionStage::NativeEnhancedPracticalFloor,
                           inputs.MasterBrightnessYStart >= 0
                               ? inputs.MasterBrightnessYStart
                               : plan.YStart,
                           inputs.MasterBrightnessYEnd >= 0
                               ? inputs.MasterBrightnessYEnd
                               : plan.YEnd,
                           inputs.MasterBrightness,
                           (mode << 8) | factor);
    }
    if (plan.RequiredChannels & physicalRouting)
    {
        AddOutputTransform(plan,
                           WholeSceneOutputTransformKind::PhysicalRouting,
                           WholeSceneOutputInsertionStage::NativeExactFloor,
                           plan.YStart,
                           plan.YEnd,
                           static_cast<u32>(inputs.PhysicalTarget),
                           0,
                           inputs.PhysicalTarget);
    }
    if (inputs.HasPhysicalPresentationBrightness)
    {
        const u32 mode = (inputs.PhysicalPresentationBrightness >> 14) & 0x3u;
        const u32 factor =
            std::min<u32>(inputs.PhysicalPresentationBrightness & 0x1Fu, 16u);
        AddOutputTransform(
            plan,
            WholeSceneOutputTransformKind::PhysicalPresentationBrightness,
            WholeSceneOutputInsertionStage::NativeExactFloor,
            inputs.PhysicalPresentationBrightnessYStart,
            inputs.PhysicalPresentationBrightnessYEnd,
            inputs.PhysicalPresentationBrightness,
            (mode << 8) | factor,
            inputs.PhysicalTarget);
    }
    plan.SelectedRepresentationProduct =
        BuildWholeSceneOutputRepresentationProduct(plan, inputs);
}

static void EnforceWholeSceneOutputPlanAdmission(
    WholeSceneOutputPlan& plan,
    const WholeSceneOutputPlanInputs& inputs)
{
    if (!plan.Valid || !plan.ChannelsAssessed)
        return;

    plan.AdmissionEvaluated = true;
    plan.InitialSelectedPath = plan.SelectedPath;
    plan.InitialCurrentReason = plan.CurrentReason;
    plan.InitialSelectedRepresentation = plan.SelectedRepresentation;

    if (plan.UnsupportedChannels == 0)
    {
        plan.AdmissionReason = plan.EvidenceUnavailableChannels == 0
            ? WholeSceneOutputAdmissionReason::SelectedRepresentationAdmitted
            : WholeSceneOutputAdmissionReason::
                  SelectedRepresentationRetainedForParityWithEvidenceDebt;
        return;
    }

    plan.AdmissionRejectedUnsupportedChannels = plan.UnsupportedChannels;
    plan.AdmissionRejectedEvidenceUnavailableChannels =
        plan.EvidenceUnavailableChannels;

    const u32 finalNativeBit = WholeSceneOutputRepresentationBit(
        WholeSceneOutputRepresentationKind::FinalNative);
    const u32 nativeStackBit = WholeSceneOutputRepresentationBit(
        WholeSceneOutputRepresentationKind::NativeStack);
    const u32 nativeExactFloorBit = WholeSceneOutputRepresentationBit(
        WholeSceneOutputRepresentationKind::NativeExactFloor);
    const bool canDemoteToFinalNative =
        (plan.ConsideredRepresentationMask & finalNativeBit) != 0;
    const bool canDemoteToNativeStack =
        (plan.ConsideredRepresentationMask & nativeStackBit) != 0;
    const bool canDemoteToNativeExactFloor =
        (plan.ConsideredRepresentationMask & nativeExactFloorBit) != 0;
    if (!canDemoteToFinalNative && !canDemoteToNativeStack &&
        !canDemoteToNativeExactFloor)
    {
        plan.AdmissionReason =
            WholeSceneOutputAdmissionReason::UnsupportedChannelsNoFallback;
        return;
    }

    plan.AdmissionChangedSelection = true;
    if (canDemoteToFinalNative)
    {
        plan.AdmissionReason =
            WholeSceneOutputAdmissionReason::UnsupportedChannelsDemotedToFinalNative;
        plan.SelectedPath = WholeSceneRenderPath::FinalNativeUpscale;
        plan.SelectedRepresentation = WholeSceneOutputRepresentationKind::FinalNative;
    }
    else if (canDemoteToNativeStack)
    {
        plan.AdmissionReason =
            WholeSceneOutputAdmissionReason::UnsupportedChannelsDemotedToNativeStack;
        plan.SelectedPath = WholeSceneRenderPath::LegacyNativeUpscale;
        plan.SelectedRepresentation = WholeSceneOutputRepresentationKind::NativeStack;
    }
    else
    {
        plan.AdmissionReason = WholeSceneOutputAdmissionReason::
            UnsupportedChannelsDemotedToNativeExactFloor;
        plan.SelectedPath = WholeSceneRenderPath::NativeExactFloor;
        plan.SelectedRepresentation =
            WholeSceneOutputRepresentationKind::NativeExactFloor;
    }
    plan.CapturePlan = {};
    plan.RepresentationEvidence = {};
    plan.CurrentReason = WholeSceneCurrentPathReason::None;
    plan.Selector.Configured = false;
    plan.Selector.Kind = WholeSceneOutputSelectorKind::None;
    AssessWholeSceneOutputPlan(plan, inputs);
}

static WholeSceneOutputExecutionKind ResolveWholeSceneOutputExecutionKind(
    const WholeSceneOutputPlan& plan)
{
    if (!plan.Valid)
        return WholeSceneOutputExecutionKind::None;

    if (plan.AdmissionChangedSelection)
        return WholeSceneOutputExecutionKind::SelectedRepresentation;

    switch (plan.PathReason)
    {
    case WholeScenePathDecisionReason::CaptureBackedProducerDuringHybridGuard:
    case WholeScenePathDecisionReason::CaptureBackedBeforeGeneralFallbacks:
    case WholeScenePathDecisionReason::CaptureBackedAfterGeneralFallbacks:
        return WholeSceneOutputExecutionKind::CaptureBackedPlan;
    case WholeScenePathDecisionReason::HybridPresentationGuard:
    case WholeScenePathDecisionReason::FragmentationOrUnsafeFrameCurrentFallback:
    case WholeScenePathDecisionReason::ScalePathUnavailable:
        return WholeSceneOutputExecutionKind::Current;
    case WholeScenePathDecisionReason::PhysicalFinalPostprocessNativeInput:
        return WholeSceneOutputExecutionKind::PhysicalFinalPostprocessNativeInput;
    case WholeScenePathDecisionReason::SplitLegacyFallback:
    case WholeScenePathDecisionReason::WholeSceneScale:
        return WholeSceneOutputExecutionKind::SelectedRepresentation;
    case WholeScenePathDecisionReason::None:
    default:
        return WholeSceneOutputExecutionKind::None;
    }
}

static WholeSceneScaleDecision ChooseWholeSceneScaleDecisionForOutputPlan(
    const WholeSceneScaleCandidateInputs& inputs)
{
    WholeSceneScaleDecision decision = {};
    decision.CapturePlan = inputs.CapturePlan;
    if (inputs.HighResCompositorAvailable)
    {
        decision.Reason = WholeSceneScaleDecisionReason::HighResCompositor;
        decision.Path = WholeSceneRenderPath::HighResCompositor;
        return decision;
    }
    if (inputs.StrictAffineHighResAvailable)
    {
        decision.Reason = WholeSceneScaleDecisionReason::StrictAffineHighRes;
        decision.Path = WholeSceneRenderPath::StrictAffineHighRes;
        return decision;
    }
    if (inputs.IdentityEquivalentAffineWholeSceneAvailable)
    {
        // Native rasterization already produced exactly the desired affine
        // geometry. Reconstruct that complete product with the selected scaler
        // instead of entering Conservative Hybrid's native fallback.
        decision.Reason = WholeSceneScaleDecisionReason::LegacyNativeUpscale;
        decision.Path = WholeSceneRenderPath::LegacyNativeUpscale;
        return decision;
    }
    if (inputs.SourceACaptureReplacementAvailable)
    {
        decision.Reason =
            WholeSceneScaleDecisionReason::SourceACaptureOnlyReplacement;
        decision.Path = WholeSceneRenderPath::SourceACaptureReplacement;
        return decision;
    }
    if (inputs.OverlayOperatorAvailable)
    {
        WholeSceneOverlayScaleDecisionInputs overlayInputs = {};
        overlayInputs.CapturePlan = inputs.CapturePlan;
        overlayInputs.ConservativeHybridMode = inputs.ConservativeHybridMode;
        overlayInputs.HybridFragmentationGuardActive =
            inputs.HybridFragmentationGuardActive;
        return ChooseWholeSceneOverlayScaleDecision(overlayInputs);
    }
    if (inputs.FinalNativeAvailable)
    {
        decision.Reason = WholeSceneScaleDecisionReason::FinalNativeUpscale;
        decision.Path = WholeSceneRenderPath::FinalNativeUpscale;
        return decision;
    }

    decision.Reason = WholeSceneScaleDecisionReason::LegacyNativeUpscale;
    decision.Path = WholeSceneRenderPath::LegacyNativeUpscale;
    return decision;
}

WholeSceneOutputPlan BuildWholeSceneOutputPlan(const WholeSceneOutputPlanInputs& inputs)
{
    const WholeScenePathDecision pathDecision = inputs.SelectPathFromFacts
        ? ChooseWholeScenePathDecision(inputs.PathDecisionInputs)
        : inputs.PathDecision;
    const WholeSceneScaleDecision scaleDecision = inputs.SelectScaleFromFacts
        ? ChooseWholeSceneScaleDecisionForOutputPlan(inputs.ScaleCandidateInputs)
        : inputs.ScaleDecision;
    WholeSceneOutputPlan plan = {};
    plan.Valid = pathDecision.Reason != WholeScenePathDecisionReason::None;
    plan.YStart = inputs.YStart;
    plan.YEnd = inputs.YEnd;
    plan.OutputScale = inputs.OutputScale;
    const u64 compositorEffectChannels = inputs.RequiredChannels &
        (ChannelBit(WholeSceneOutputChannel::ColorEffect) |
         ChannelBit(WholeSceneOutputChannel::AlphaBlend) |
         ChannelBit(WholeSceneOutputChannel::SemiTransparentOBJ));
    plan.CompositorEffectState = BuildWholeSceneCompositorEffectState(
        compositorEffectChannels,
        inputs.BlendCnt,
        inputs.EVA,
        inputs.EVB,
        inputs.EVY,
        inputs.SemiTransparentOBJModeMask);
    plan.PathReason = pathDecision.Reason;
    plan.CurrentReason = pathDecision.CurrentReason;
    plan.StrictAffineBlockReason =
        inputs.ScaleCandidateInputs.StrictAffineBlockReason;

    if (inputs.HasScaleDecision &&
        pathDecision.Reason == WholeScenePathDecisionReason::WholeSceneScale)
    {
        plan.DecisionSource = WholeSceneOutputPlanDecisionSource::ScaleDecision;
        plan.ScaleReason = scaleDecision.Reason;
        plan.SelectedPath = scaleDecision.Path;
    }
    else
    {
        plan.DecisionSource = WholeSceneOutputPlanDecisionSource::PathDecision;
        plan.SelectedPath = pathDecision.Path;
    }

    const WholeSceneCaptureBackedPlan& candidateCapturePlan =
        plan.DecisionSource == WholeSceneOutputPlanDecisionSource::ScaleDecision
            ? scaleDecision.CapturePlan
            : pathDecision.CapturePlan;
    if (WholeSceneRenderPathForCaptureBackedPlanKind(candidateCapturePlan.Kind) ==
        plan.SelectedPath)
    {
        plan.CapturePlan = candidateCapturePlan;
        plan.RepresentationEvidence = candidateCapturePlan.Evidence;
    }

    plan.SelectedRepresentation =
        WholeSceneOutputRepresentationForPath(plan.SelectedPath);
    if (plan.SelectedRepresentation == WholeSceneOutputRepresentationKind::Current &&
        (inputs.RequiredChannels &
         ChannelBit(WholeSceneOutputChannel::VRAMDisplay)) != 0)
    {
        plan.SelectedRepresentation =
            WholeSceneOutputRepresentationKind::CurrentVRAMDisplay;
    }
    plan.ConsideredRepresentationMask =
        inputs.ConsideredRepresentationMask |
        WholeSceneOutputRepresentationBit(plan.SelectedRepresentation);

    plan.Selector.Configured =
        inputs.SelectorConfigured && PathUsesSelector(plan.SelectedPath);
    plan.Selector.LegacyReason = inputs.HybridDecision.Reason;
    plan.Selector.ConservativeHybridRequested =
        inputs.HybridDecision.ConservativeHybridRequested;
    plan.Selector.EffectiveConservativeHybrid =
        inputs.HybridDecision.EffectiveConservativeHybrid;
    plan.Selector.OverlaySuppressedDirect3D =
        inputs.HybridDecision.OverlaySuppressedDirect3D;
    plan.Selector.ActiveDirect3D = inputs.HybridDecision.ActiveDirect3D;
    plan.Selector.RenderNativeFallback = inputs.HybridDecision.RenderNativeFallback;
    plan.Selector.RenderForeground2DBase = inputs.HybridDecision.RenderForeground2DBase;
    plan.Selector.RenderForegroundCandidate = inputs.HybridDecision.RenderForegroundCandidate;
    plan.Selector.WindowEdgeAssist = inputs.WindowEdgeAssist;
    plan.Selector.Target2AlphaBlendAssist = inputs.Target2AlphaBlendAssist;
    plan.Selector.NativeEffectGuard = inputs.NativeEffectGuard;
    plan.Selector.CleanLegacyCandidateEnabled = inputs.CleanLegacyCandidateEnabled;
    plan.Selector.CleanLegacyBlockReason = inputs.CleanLegacyBlockReason;

    if (plan.Selector.Configured)
    {
        plan.Selector.Kind = inputs.HybridDecision.EffectiveConservativeHybrid
            ? WholeSceneOutputSelectorKind::ConservativeHybrid
            : WholeSceneOutputSelectorKind::OverlayOperator;
    }

    AssessWholeSceneOutputPlan(plan, inputs);
    EnforceWholeSceneOutputPlanAdmission(plan, inputs);
    plan.ExecutionKind = ResolveWholeSceneOutputExecutionKind(plan);
    const bool executeFullHybridPresentationGuard =
        !plan.AdmissionChangedSelection &&
        plan.PathReason == WholeScenePathDecisionReason::HybridPresentationGuard;
    plan.ExecutionYStart = executeFullHybridPresentationGuard
        ? 0
        : plan.YStart;
    plan.ExecutionYEnd = executeFullHybridPresentationGuard
        ? 192
        : plan.YEnd;
    plan.ExecutionCurrentFragmentationFallback =
        plan.PathReason == WholeScenePathDecisionReason::
            FragmentationOrUnsafeFrameCurrentFallback;
    plan.CompositorFallbackPolicy =
        plan.SelectedPath == WholeSceneRenderPath::StrictAffineHighRes
            ? WholeSceneCompositorFallbackPolicy::
                  PreparedRecipeOrStrictCandidateBindings
            : WholeSceneCompositorFallbackPolicy::None;
    plan.OrderedPresentationFallbackPolicy =
        plan.SelectedPath == WholeSceneRenderPath::StrictAffineHighRes
            ? WholeSceneCompositorFallbackPolicy::PreparedRecipe
            : WholeSceneCompositorFallbackPolicy::None;
    plan.OperandExcludedOverlayFallbackPolicy =
        plan.SelectedPath == WholeSceneRenderPath::StrictAffineHighRes
            ? WholeSceneCompositorFallbackPolicy::PreparedRecipe
            : WholeSceneCompositorFallbackPolicy::None;

    return plan;
}

static WholeSceneOutputRepresentationProduct
BuildWholeSceneOutputRepresentationProduct(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOutputPlanInputs& inputs)
{
    WholeSceneOutputRepresentationProduct product;
    product.Kind = plan.SelectedRepresentation;
    product.TerminalCompleted =
        plan.SelectedRepresentation != WholeSceneOutputRepresentationKind::None;
    // A selected representation is a completed output candidate. It is never
    // an implicit semantic layer; only CompositorInputProduct descriptors may
    // enter a compositor recipe.
    product.CompositorInputEligible = false;
    product.CapabilityMask = product.TerminalCompleted
        ? OutputRepresentationCapabilityCompletedScene |
              OutputRepresentationCapabilityTerminalPresentation
        : OutputRepresentationCapabilityNone;
    product.ValidYStart = plan.YStart;
    product.ValidYEnd = plan.YEnd;
    product.Lifetime = plan.YStart == 0 && plan.YEnd == 192
        ? WholeSceneCompositorLifetimeKind::FullFrameSnapshot
        : WholeSceneCompositorLifetimeKind::ScanlineRange;
    product.OutputScale = plan.OutputScale;
    product.InsertionStage = plan.InsertionStage;
    product.CorrectnessClass = plan.CorrectnessClass;
    product.SourceGeneration = inputs.RepresentationSourceGeneration;
    product.RowEpochIdentity = inputs.RepresentationRowEpochIdentity;

    switch (plan.SelectedRepresentation)
    {
    case WholeSceneOutputRepresentationKind::NativeStack:
    case WholeSceneOutputRepresentationKind::FinalNative:
        product.CapabilityMask |=
            OutputRepresentationCapabilityNativeSemanticOwnership;
        break;
    case WholeSceneOutputRepresentationKind::OverlayOperator:
    case WholeSceneOutputRepresentationKind::ConservativeHybrid:
        product.CapabilityMask |=
            OutputRepresentationCapabilityNativeSemanticOwnership |
            OutputRepresentationCapabilityEndpointReconstruction |
            OutputRepresentationCapabilitySelectorResolved;
        break;
    case WholeSceneOutputRepresentationKind::HighResCompositor:
    case WholeSceneOutputRepresentationKind::StrictAffineHighRes:
        product.CapabilityMask |=
            OutputRepresentationCapabilityHighResGeometry;
        break;
    case WholeSceneOutputRepresentationKind::NativeExactFloor:
        product.CapabilityMask |= OutputRepresentationCapabilityNativeExact;
        break;
    case WholeSceneOutputRepresentationKind::CaptureBackedHandoff:
    case WholeSceneOutputRepresentationKind::SourceACaptureReplacement:
    case WholeSceneOutputRepresentationKind::CaptureEpochOverlay:
        product.CapabilityMask |=
            OutputRepresentationCapabilitySelectorResolved;
        break;
    default:
        break;
    }

    constexpr u64 offset = 1469598103934665603ull;
    constexpr u64 prime = 1099511628211ull;
    u64 hash = offset;
    auto mix = [&](u64 value)
    {
        for (int byte = 0; byte < 8; byte++)
        {
            hash ^= value & 0xFFu;
            hash *= prime;
            value >>= 8;
        }
    };
    mix(static_cast<u64>(product.Kind));
    mix(product.CapabilityMask);
    mix(static_cast<u64>(product.ValidYStart));
    mix(static_cast<u64>(product.ValidYEnd));
    mix(static_cast<u64>(product.Lifetime));
    mix(static_cast<u64>(product.OutputScale));
    mix(static_cast<u64>(product.InsertionStage));
    mix(static_cast<u64>(product.CorrectnessClass));
    mix(product.SourceGeneration);
    mix(product.RowEpochIdentity);
    product.ProductIdentity = hash;
    product.Valid = product.TerminalCompleted &&
        product.ValidYStart >= 0 && product.ValidYEnd <= 192 &&
        product.ValidYStart < product.ValidYEnd &&
        product.OutputScale >= 1 && product.SourceGeneration != 0 &&
        product.RowEpochIdentity != 0;
    return product;
}

WholeSceneCompositorRecipeBinding PrepareWholeSceneCompositorRecipeBinding(
    const WholeSceneOutputPlan& plan,
    const WholeSceneCompositorRecipe& recipe)
{
    WholeSceneCompositorRecipeBinding binding;
    binding.Evaluated = true;
    binding.Recipe = recipe;

    if (!plan.Valid)
    {
        binding.Reason = WholeSceneOutputPlanRecipeBindingReason::InvalidPlan;
        return binding;
    }
    if (plan.SelectedPath != WholeSceneRenderPath::StrictAffineHighRes ||
        recipe.Kind != WholeSceneCompositorRecipeKind::SemanticResolve)
    {
        binding.Reason = WholeSceneOutputPlanRecipeBindingReason::WrongPath;
        return binding;
    }
    if (!recipe.Ready)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::RecipeRejected;
        return binding;
    }
    if (!WholeSceneCompositorRecipeProductsCoverScope(recipe))
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::ScopeMismatch;
        return binding;
    }
    if (recipe.EffectOwner !=
            WholeSceneCompositorEffectOwner::SemanticResolver ||
        recipe.EffectState.StateHash !=
            plan.CompositorEffectState.StateHash)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::DependencyMismatch;
        return binding;
    }
    if (recipe.YStart != plan.ExecutionYStart ||
        recipe.YEnd != plan.ExecutionYEnd)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::ScopeMismatch;
        return binding;
    }
    if (recipe.OutputScale != plan.OutputScale)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::ScaleMismatch;
        return binding;
    }

    binding.Bound = true;
    binding.Reason = WholeSceneOutputPlanRecipeBindingReason::Bound;
    return binding;
}

WholeSceneCompositorExecutionOutcome ResolveWholeSceneCompositorExecution(
    const WholeSceneOutputPlan& plan,
    const WholeSceneCompositorRecipeBinding& binding,
    const WholeSceneCompositorRecipe& preparedRecipe)
{
    WholeSceneCompositorExecutionOutcome outcome;
    if (binding.Bound &&
        binding.Recipe.RecipeHash == preparedRecipe.RecipeHash)
    {
        outcome.Disposition =
            WholeSceneCompositorExecutionDisposition::PlannedRecipe;
        outcome.Reason =
            WholeSceneCompositorExecutionReason::PlannedRecipeMatched;
        outcome.EffectiveRecipe = binding.Recipe;
        return outcome;
    }

    if (plan.CompositorFallbackPolicy ==
        WholeSceneCompositorFallbackPolicy::None)
    {
        outcome.Disposition =
            WholeSceneCompositorExecutionDisposition::Unavailable;
        outcome.Reason =
            WholeSceneCompositorExecutionReason::FallbackNotAuthorized;
        return outcome;
    }

    outcome.EffectiveRecipe = preparedRecipe;
    if (!preparedRecipe.Ready &&
        plan.CompositorFallbackPolicy ==
            WholeSceneCompositorFallbackPolicy::
                PreparedRecipeOrStrictCandidateBindings)
    {
        outcome.Disposition =
            WholeSceneCompositorExecutionDisposition::
                StrictCandidateBindingsFallback;
        outcome.Reason =
            WholeSceneCompositorExecutionReason::PreparedRecipeRejected;
    }
    else if (preparedRecipe.Ready)
    {
        outcome.Disposition = WholeSceneCompositorExecutionDisposition::
            PreparedRecipeFallback;
        outcome.Reason = binding.Bound
            ? WholeSceneCompositorExecutionReason::PreparedRecipeMismatch
            : WholeSceneCompositorExecutionReason::PlannedRecipeNotBound;
    }
    else
    {
        outcome.Disposition =
            WholeSceneCompositorExecutionDisposition::Unavailable;
        outcome.Reason =
            WholeSceneCompositorExecutionReason::PreparedRecipeRejected;
    }
    return outcome;
}

void ObserveWholeSceneOutputPlanCompositorRecipeExecution(
    const WholeSceneCompositorRecipeBinding& binding,
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneCompositorExecutionOutcome& outcome)
{
    const WholeSceneCompositorRecipe& executedRecipe = outcome.EffectiveRecipe;
    trace.CompositorExecutionDisposition = outcome.Disposition;
    trace.CompositorExecutionReason = outcome.Reason;
    trace.PreparedCompositorRecipeHash = executedRecipe.RecipeHash;
    trace.CompositorRecipeExecutionObserved = true;
    trace.ExecutedCompositorRecipe = executedRecipe;
    trace.ExecutedCompositorRecipeHash = executedRecipe.RecipeHash;
    trace.CompositorRecipeParity =
        binding.Bound &&
        binding.Recipe.RecipeHash == executedRecipe.RecipeHash;
}

WholeSceneOrderedPresentationRecipeBinding
PrepareWholeSceneOrderedPresentationRecipeBinding(
    const WholeSceneOutputPlan& plan,
    const WholeSceneCompositorRecipeBinding& compositorBinding,
    const WholeSceneCompositorRecipe& semanticBaseRecipe,
    const WholeSceneCompositorRecipe& recipe)
{
    WholeSceneOrderedPresentationRecipeBinding binding;
    binding.Evaluated = true;
    binding.SemanticBaseRecipe = semanticBaseRecipe;
    binding.Recipe = recipe;

    if (!plan.Valid)
    {
        binding.Reason = WholeSceneOutputPlanRecipeBindingReason::InvalidPlan;
        return binding;
    }
    if (plan.SelectedPath != WholeSceneRenderPath::StrictAffineHighRes ||
        recipe.Kind !=
            WholeSceneCompositorRecipeKind::OrderedPresentation)
    {
        binding.Reason = WholeSceneOutputPlanRecipeBindingReason::WrongPath;
        return binding;
    }
    if (!recipe.Ready)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::RecipeRejected;
        return binding;
    }
    if (!WholeSceneCompositorRecipeProductsCoverScope(recipe) ||
        !WholeSceneCompositorRecipeProductsCoverScope(semanticBaseRecipe))
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::ScopeMismatch;
        return binding;
    }
    if (semanticBaseRecipe.EffectOwner !=
            WholeSceneCompositorEffectOwner::SemanticResolver ||
        (recipe.EffectOwner !=
             WholeSceneCompositorEffectOwner::OrderedResolver &&
         recipe.EffectOwner !=
             WholeSceneCompositorEffectOwner::SplitSemanticAndOrdered) ||
        semanticBaseRecipe.EffectState.StateHash !=
            plan.CompositorEffectState.StateHash ||
        recipe.EffectState.StateHash !=
            plan.CompositorEffectState.StateHash)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::DependencyMismatch;
        return binding;
    }
    if (!compositorBinding.Bound || !semanticBaseRecipe.Ready ||
        semanticBaseRecipe.Kind !=
            WholeSceneCompositorRecipeKind::SemanticResolve)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::DependencyMissing;
        return binding;
    }
    if (recipe.DependencyRecipeHash != semanticBaseRecipe.RecipeHash)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::DependencyMismatch;
        return binding;
    }
    if (recipe.RowEpochIdentity != semanticBaseRecipe.RowEpochIdentity)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::DependencyMismatch;
        return binding;
    }
    if (recipe.YStart != plan.ExecutionYStart ||
        recipe.YEnd != plan.ExecutionYEnd ||
        semanticBaseRecipe.YStart != plan.ExecutionYStart ||
        semanticBaseRecipe.YEnd != plan.ExecutionYEnd)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::ScopeMismatch;
        return binding;
    }
    if (recipe.OutputScale != plan.OutputScale ||
        semanticBaseRecipe.OutputScale != plan.OutputScale)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::ScaleMismatch;
        return binding;
    }

    binding.Bound = true;
    binding.Reason = WholeSceneOutputPlanRecipeBindingReason::Bound;
    return binding;
}

WholeSceneOrderedPresentationExecutionOutcome
ResolveWholeSceneOrderedPresentationExecution(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOrderedPresentationRecipeBinding& binding,
    const WholeSceneCompositorRecipe& preparedSemanticBaseRecipe,
    const WholeSceneCompositorRecipe& preparedRecipe,
    bool productsReady)
{
    WholeSceneOrderedPresentationExecutionOutcome outcome;
    outcome.EffectiveSemanticBaseRecipe = preparedSemanticBaseRecipe;
    outcome.EffectiveRecipe = preparedRecipe;

    if (!productsReady)
    {
        outcome.Disposition =
            WholeSceneCompositorExecutionDisposition::Unavailable;
        outcome.Reason = WholeSceneCompositorExecutionReason::
            PreparedProductsUnavailable;
        return outcome;
    }

    const bool baseMatches =
        binding.Bound &&
        binding.SemanticBaseRecipe.RecipeHash ==
            preparedSemanticBaseRecipe.RecipeHash;
    const bool recipeMatches =
        binding.Bound &&
        binding.Recipe.RecipeHash == preparedRecipe.RecipeHash;
    if (baseMatches && recipeMatches)
    {
        outcome.Disposition =
            WholeSceneCompositorExecutionDisposition::PlannedRecipe;
        outcome.Reason =
            WholeSceneCompositorExecutionReason::PlannedRecipeMatched;
        outcome.EffectiveSemanticBaseRecipe =
            binding.SemanticBaseRecipe;
        outcome.EffectiveRecipe = binding.Recipe;
        return outcome;
    }

    if (plan.OrderedPresentationFallbackPolicy !=
            WholeSceneCompositorFallbackPolicy::PreparedRecipe ||
        !preparedSemanticBaseRecipe.Ready || !preparedRecipe.Ready)
    {
        outcome.Disposition =
            WholeSceneCompositorExecutionDisposition::Unavailable;
        outcome.Reason = !preparedSemanticBaseRecipe.Ready ||
                !preparedRecipe.Ready
            ? WholeSceneCompositorExecutionReason::PreparedRecipeRejected
            : WholeSceneCompositorExecutionReason::FallbackNotAuthorized;
        return outcome;
    }

    if (preparedRecipe.DependencyRecipeHash !=
        preparedSemanticBaseRecipe.RecipeHash)
    {
        outcome.Disposition =
            WholeSceneCompositorExecutionDisposition::Unavailable;
        outcome.Reason =
            WholeSceneCompositorExecutionReason::DependencyMismatch;
        return outcome;
    }
    if (preparedRecipe.RowEpochIdentity !=
        preparedSemanticBaseRecipe.RowEpochIdentity)
    {
        outcome.Disposition =
            WholeSceneCompositorExecutionDisposition::Unavailable;
        outcome.Reason =
            WholeSceneCompositorExecutionReason::DependencyMismatch;
        return outcome;
    }

    outcome.Disposition = WholeSceneCompositorExecutionDisposition::
        PreparedRecipeFallback;
    outcome.Reason = binding.Bound
        ? WholeSceneCompositorExecutionReason::PreparedRecipeMismatch
        : WholeSceneCompositorExecutionReason::PlannedRecipeNotBound;
    return outcome;
}

void ObserveWholeSceneOutputPlanOrderedPresentationRecipeExecution(
    const WholeSceneOrderedPresentationRecipeBinding& binding,
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneOrderedPresentationExecutionOutcome& outcome)
{
    const WholeSceneCompositorRecipe& executedSemanticBaseRecipe =
        outcome.EffectiveSemanticBaseRecipe;
    const WholeSceneCompositorRecipe& executedRecipe = outcome.EffectiveRecipe;
    trace.OrderedPresentationExecutionDisposition = outcome.Disposition;
    trace.OrderedPresentationExecutionReason = outcome.Reason;
    trace.PreparedOrderedPresentationBaseRecipeHash =
        executedSemanticBaseRecipe.RecipeHash;
    trace.PreparedOrderedPresentationRecipeHash = executedRecipe.RecipeHash;
    trace.OrderedPresentationBaseRecipeExecutionObserved = true;
    trace.ExecutedOrderedPresentationBaseRecipe = executedSemanticBaseRecipe;
    trace.ExecutedOrderedPresentationBaseRecipeHash =
        executedSemanticBaseRecipe.RecipeHash;
    trace.OrderedPresentationBaseRecipeParity =
        binding.Bound &&
        binding.SemanticBaseRecipe.RecipeHash ==
            executedSemanticBaseRecipe.RecipeHash;
    trace.OrderedPresentationRecipeExecutionObserved = true;
    trace.ExecutedOrderedPresentationRecipe = executedRecipe;
    trace.ExecutedOrderedPresentationRecipeHash = executedRecipe.RecipeHash;
    trace.OrderedPresentationRecipeParity =
        binding.Bound &&
        binding.Recipe.RecipeHash == executedRecipe.RecipeHash;
}

WholeSceneOperandExcludedOverlayRecipeBinding
PrepareWholeSceneOperandExcludedOverlayRecipeBinding(
    const WholeSceneOutputPlan& plan,
    const WholeSceneCompositorRecipeBinding& compositorBinding,
    const WholeSceneOrderedPresentationRecipeBinding& orderedBinding,
    const WholeSceneOperandExcludedOverlayRecipe& recipe)
{
    WholeSceneOperandExcludedOverlayRecipeBinding binding;
    binding.Evaluated = true;
    binding.Recipe = recipe;

    if (!plan.Valid)
    {
        binding.Reason = WholeSceneOutputPlanRecipeBindingReason::InvalidPlan;
        return binding;
    }
    if (plan.SelectedPath != WholeSceneRenderPath::StrictAffineHighRes)
    {
        binding.Reason = WholeSceneOutputPlanRecipeBindingReason::WrongPath;
        return binding;
    }
    if (!recipe.Ready)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::RecipeRejected;
        return binding;
    }
    if (recipe.EffectState.StateHash !=
        plan.CompositorEffectState.StateHash)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::DependencyMismatch;
        return binding;
    }
    if (!compositorBinding.Bound || !orderedBinding.Bound)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::DependencyMissing;
        return binding;
    }
    if (recipe.SemanticBaseRecipeHash !=
            orderedBinding.SemanticBaseRecipe.RecipeHash ||
        recipe.OrderedPresentationRecipeHash !=
            orderedBinding.Recipe.RecipeHash ||
        recipe.RowEpochIdentity !=
            orderedBinding.Recipe.RowEpochIdentity)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::DependencyMismatch;
        return binding;
    }
    if (recipe.YStart != plan.ExecutionYStart ||
        recipe.YEnd != plan.ExecutionYEnd)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::ScopeMismatch;
        return binding;
    }
    if (recipe.OutputScale != plan.OutputScale)
    {
        binding.Reason =
            WholeSceneOutputPlanRecipeBindingReason::ScaleMismatch;
        return binding;
    }

    binding.Bound = true;
    binding.Reason = WholeSceneOutputPlanRecipeBindingReason::Bound;
    return binding;
}

WholeSceneOperandExcludedOverlayExecutionOutcome
ResolveWholeSceneOperandExcludedOverlayExecution(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOperandExcludedOverlayRecipeBinding& binding,
    const WholeSceneOperandExcludedOverlayRecipe& preparedRecipe)
{
    WholeSceneOperandExcludedOverlayExecutionOutcome outcome;
    outcome.EffectiveRecipe = preparedRecipe;

    if (binding.Bound &&
        binding.Recipe.RecipeHash ==
            preparedRecipe.RecipeHash)
    {
        outcome.Disposition =
            WholeSceneCompositorExecutionDisposition::PlannedRecipe;
        outcome.Reason =
            WholeSceneCompositorExecutionReason::PlannedRecipeMatched;
        outcome.EffectiveRecipe = binding.Recipe;
        return outcome;
    }

    if (!preparedRecipe.Ready)
    {
        outcome.Disposition =
            WholeSceneCompositorExecutionDisposition::Unavailable;
        outcome.Reason =
            (preparedRecipe.RejectionMask &
             OperandExcludedOverlayRejectOrderedRecipe) != 0
            ? WholeSceneCompositorExecutionReason::
                  PreparedProductsUnavailable
            : WholeSceneCompositorExecutionReason::PreparedRecipeRejected;
        return outcome;
    }

    if (plan.OperandExcludedOverlayFallbackPolicy !=
        WholeSceneCompositorFallbackPolicy::PreparedRecipe)
    {
        outcome.Disposition =
            WholeSceneCompositorExecutionDisposition::Unavailable;
        outcome.Reason =
            WholeSceneCompositorExecutionReason::FallbackNotAuthorized;
        return outcome;
    }

    outcome.Disposition = WholeSceneCompositorExecutionDisposition::
        PreparedRecipeFallback;
    outcome.Reason = binding.Bound
        ? WholeSceneCompositorExecutionReason::PreparedRecipeMismatch
        : WholeSceneCompositorExecutionReason::PlannedRecipeNotBound;
    return outcome;
}

void RecordWholeSceneOperandExcludedOverlayRenderFailure(
    WholeSceneOperandExcludedOverlayExecutionOutcome& outcome)
{
    outcome.Disposition =
        WholeSceneCompositorExecutionDisposition::Unavailable;
    outcome.Reason = WholeSceneCompositorExecutionReason::RenderFailure;
}

void ObserveWholeSceneOutputPlanOperandExcludedOverlayRecipeExecution(
    const WholeSceneOperandExcludedOverlayRecipeBinding& binding,
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneOperandExcludedOverlayExecutionOutcome& outcome,
    bool executionObserved)
{
    trace.OperandExcludedOverlayExecutionDisposition = outcome.Disposition;
    trace.OperandExcludedOverlayExecutionReason = outcome.Reason;
    trace.PreparedOperandExcludedOverlayRecipeHash =
        outcome.EffectiveRecipe.RecipeHash;
    trace.OperandExcludedOverlayRecipeExecutionObserved = executionObserved;
    if (executionObserved)
    {
        trace.ExecutedOperandExcludedOverlayRecipe = outcome.EffectiveRecipe;
        trace.ExecutedOperandExcludedOverlayRecipeHash =
            outcome.EffectiveRecipe.RecipeHash;
    }
    trace.OperandExcludedOverlayRecipeParity =
        executionObserved &&
        binding.Bound &&
        binding.Recipe.RecipeHash ==
            outcome.EffectiveRecipe.RecipeHash;
}

WholeSceneStrictAffinePresentationOutcome
ResolveWholeSceneStrictAffinePresentationExecution(
    const WholeSceneStrictAffineProductAssessment& assessment)
{
    WholeSceneStrictAffinePresentationOutcome outcome;
    if (assessment.OrderedOperandsReady)
    {
        outcome.Kind = WholeSceneStrictAffinePresentationKind::OrderedOperands;
        outcome.Reason =
            WholeSceneStrictAffinePresentationReason::OrderedProductsReady;
        return outcome;
    }
    if (assessment.AtomicOrdinaryBandReady)
    {
        outcome.Kind =
            WholeSceneStrictAffinePresentationKind::AtomicOrdinaryBand;
        outcome.Reason =
            WholeSceneStrictAffinePresentationReason::AtomicBandProductsReady;
        return outcome;
    }
    if (assessment.NativeStackReady)
    {
        outcome.Kind =
            WholeSceneStrictAffinePresentationKind::NativeStackComposite;
        outcome.Reason =
            WholeSceneStrictAffinePresentationReason::NativeStackReady;
        outcome.AffineOBJInsertion = assessment.AffineOBJInsertionReady;
        return outcome;
    }

    outcome.Kind = WholeSceneStrictAffinePresentationKind::StrictCandidate;
    outcome.Reason = WholeSceneStrictAffinePresentationReason::
        NoPresentationProductsReady;
    return outcome;
}

void ObserveWholeSceneStrictAffinePresentationExecution(
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneStrictAffineProductAssessment& assessment,
    const WholeSceneStrictAffinePresentationOutcome& outcome)
{
    trace.StrictAffineProductAssessment = assessment;
    trace.StrictAffinePresentationExecutionKind = outcome.Kind;
    trace.StrictAffinePresentationExecutionReason = outcome.Reason;
    trace.StrictAffinePresentationAffineOBJInsertion =
        outcome.AffineOBJInsertion;
}

bool WholeSceneOutputExecutionSuppressesDeferredFinalizer(
    const WholeSceneOutputPlan& plan)
{
    return plan.ExecutionKind == WholeSceneOutputExecutionKind::CaptureBackedPlan &&
           plan.CapturePlan.Kind ==
               WholeSceneCaptureBackedPlanKind::CaptureBackedHandoff;
}

const WholeSceneOutputTransform* ResolveWholeSceneOutputTransformForExecution(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputTransformKind kind,
    int ystart,
    int yend)
{
    if (!plan.Valid || kind == WholeSceneOutputTransformKind::None ||
        ystart >= yend)
    {
        return nullptr;
    }

    for (u32 i = 0; i < plan.TransformCount; i++)
    {
        const WholeSceneOutputTransform& transform = plan.Transforms[i];
        if (transform.Kind != kind)
            continue;

        // A transform may execute in smaller final-pass chunks, but it must
        // never escape the range captured when the plan was built.
        if (ystart < transform.YStart || yend > transform.YEnd)
            return nullptr;

        return &transform;
    }

    return nullptr;
}

bool MarkWholeSceneOutputTransformPlanExecuted(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputExecutionTrace& trace,
    WholeSceneOutputTransformKind kind,
    int ystart,
    int yend,
    u32 state,
    u32 auxState)
{
    const WholeSceneOutputTransform* resolved =
        ResolveWholeSceneOutputTransformForExecution(plan, kind, ystart, yend);
    if (!resolved || resolved->State != state || resolved->AuxState != auxState)
        return false;

    for (u32 i = 0; i < plan.TransformCount; i++)
    {
        const WholeSceneOutputTransform& transform = plan.Transforms[i];
        if (&transform != resolved)
            continue;

        trace.Transforms[i].PlanExecuted = true;
        trace.TransformsShadowOnly = false;
        trace.PlanExecutedTransformMask |= WholeSceneOutputTransformBit(kind);
        return true;
    }

    return false;
}

bool IsWholeSceneOutputTransformPlanExecuted(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOutputExecutionTrace& trace,
    WholeSceneOutputTransformKind kind,
    int ystart,
    int yend,
    u32 state,
    u32 auxState)
{
    const WholeSceneOutputTransform* transform =
        ResolveWholeSceneOutputTransformForExecution(plan, kind, ystart, yend);
    if (!transform || transform->State != state || transform->AuxState != auxState)
        return false;

    for (u32 i = 0; i < plan.TransformCount; i++)
    {
        if (&plan.Transforms[i] == transform)
            return trace.Transforms[i].PlanExecuted;
    }
    return false;
}

void ObserveWholeSceneOutputTransform(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneOutputTransformObservation& observation)
{
    const WholeSceneOutputTransform* planned = nullptr;
    u32 plannedIndex = 0;
    for (u32 i = 0; i < plan.TransformCount; i++)
    {
        if (plan.Transforms[i].Kind == observation.Kind)
        {
            planned = &plan.Transforms[i];
            plannedIndex = i;
            break;
        }
    }

    const u32 bit = WholeSceneOutputTransformBit(observation.Kind);
    if (!planned)
    {
        trace.UnexpectedObservedTransformMask |= bit;
        return;
    }

    auto& observed = trace.Transforms[plannedIndex];

    const int observedYStart = std::max(planned->YStart, observation.YStart);
    const int observedYEnd = std::min(planned->YEnd, observation.YEnd);
    const bool coversPlannedRange =
        observedYStart == planned->YStart &&
        observedYEnd == planned->YEnd &&
        observedYStart < observedYEnd;
    const bool executesWithinPlannedRange =
        observation.PlanApplied &&
        observation.YStart >= planned->YStart &&
        observation.YEnd <= planned->YEnd &&
        observation.YStart < observation.YEnd;

    observed.ExecutionObserved = true;
    observed.LegacyApplied = observation.LegacyApplied;
    observed.ObservedYStart = observedYStart;
    observed.ObservedYEnd = observedYEnd;
    observed.ObservedState = observation.State;
    observed.ObservedAuxState = observation.AuxState;
    observed.ObservedTarget = observation.Target;
    observed.PlanExecuted = observed.PlanExecuted || observation.PlanApplied;
    observed.TraceAgreement =
        (observation.LegacyApplied || observation.PlanApplied) &&
        (coversPlannedRange || executesWithinPlannedRange) &&
        planned->State == observation.State &&
        (observation.Kind != WholeSceneOutputTransformKind::PhysicalRouting ||
         planned->Target == observation.Target);

    trace.ObservedTransformMask |= bit;
    if (observation.PlanApplied)
    {
        trace.TransformsShadowOnly = false;
        trace.PlanExecutedTransformMask |= bit;
    }
    if (!observed.TraceAgreement)
        trace.TransformDisagreementMask |= bit;
}

u32 WholeSceneOutputUnobservedTransformMask(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOutputExecutionTrace& trace)
{
    return plan.PlannedTransformMask & ~trace.ObservedTransformMask;
}

void ObserveWholeSceneCaptureInput(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneCaptureInputTrace& captureInput)
{
    trace.CaptureInput = captureInput;
    trace.CaptureInput.PresentationRepresentation = plan.SelectedRepresentation;

    WholeSceneOutputTransformObservation observation = {};
    observation.Kind = WholeSceneOutputTransformKind::DisplayCapture;
    observation.YStart = captureInput.YStart;
    observation.YEnd = captureInput.YEnd;
    observation.State = captureInput.CaptureCnt;
    observation.AuxState = static_cast<u32>(captureInput.Kind);
    observation.LegacyApplied = captureInput.Observed;
    ObserveWholeSceneOutputTransform(plan, trace, observation);

    const auto& evidence = plan.RepresentationEvidence;
    trace.RepresentationEvidenceCaptureInputTraceComparable =
        evidence.CaptureInputProven;
    trace.RepresentationEvidenceCaptureInputTraceAgreement =
        trace.RepresentationEvidenceCaptureInputTraceComparable &&
        captureInput.Observed &&
        captureInput.CaptureCnt == evidence.CaptureCnt &&
        captureInput.UsesSelectedPresentation ==
            evidence.CaptureConsumesSelectedPresentation &&
        captureInput.UsesSourceB == evidence.CaptureUsesSourceB;
}

void ObserveWholeSceneCaptureRepresentationEvidence(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneCaptureRepresentationEvidence& observedEvidence)
{
    trace.RepresentationEvidenceTraceObserved = true;
    const auto& planned = plan.RepresentationEvidence;
    const auto& plannedProduct = planned.ProductRef;
    const auto& observedProduct = observedEvidence.ProductRef;

    trace.RepresentationEvidenceContentTraceAgreement =
        planned.Available && observedEvidence.Available &&
        planned.ContentProven == observedEvidence.ContentProven &&
        (!planned.ContentProven ||
         (plannedProduct.Kind == observedProduct.Kind &&
          plannedProduct.BackgroundSource == observedProduct.BackgroundSource &&
          plannedProduct.RouteSlot == observedProduct.RouteSlot &&
          plannedProduct.CaptureBank == observedProduct.CaptureBank &&
          plannedProduct.CaptureEventSerial ==
              observedProduct.CaptureEventSerial &&
          plannedProduct.BackgroundEpochSerial ==
              observedProduct.BackgroundEpochSerial &&
          planned.ProofKind == observedEvidence.ProofKind &&
          planned.Source3DSerial == observedEvidence.Source3DSerial &&
          planned.Source3DSceneHash == observedEvidence.Source3DSceneHash &&
          planned.SourceKind == observedEvidence.SourceKind &&
          planned.ProductMask == observedEvidence.ProductMask &&
          planned.AuthorizationProofKind ==
              observedEvidence.AuthorizationProofKind &&
          planned.AuthorizationEpochSerial ==
              observedEvidence.AuthorizationEpochSerial &&
          planned.AuthorizationCaptureBank ==
              observedEvidence.AuthorizationCaptureBank));

    trace.RepresentationEvidencePresentationTraceComparable =
        planned.PresentationProven && observedEvidence.PresentationProven;
    trace.RepresentationEvidencePresentationTraceAgreement =
        trace.RepresentationEvidencePresentationTraceComparable &&
        plannedProduct.CapturePresentationHash ==
            observedProduct.CapturePresentationHash &&
        plannedProduct.CurrentPresentationHash ==
            observedProduct.CurrentPresentationHash &&
        planned.AuthorizationPresentationHash ==
            observedEvidence.AuthorizationPresentationHash &&
        planned.OutputEffectOwner == observedEvidence.OutputEffectOwner &&
        planned.OutputEffectState == observedEvidence.OutputEffectState &&
        planned.OutputPreMaster == observedEvidence.OutputPreMaster &&
        planned.CaptureInputProven == observedEvidence.CaptureInputProven &&
        planned.CaptureCnt == observedEvidence.CaptureCnt &&
        planned.CaptureConsumesSelectedPresentation ==
            observedEvidence.CaptureConsumesSelectedPresentation &&
        planned.CaptureUsesSourceB == observedEvidence.CaptureUsesSourceB &&
        planned.CanPublishRouteProduct ==
            observedEvidence.CanPublishRouteProduct;
}

void ObserveWholeScenePresentation(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputExecutionTrace& trace,
    const WholeScenePresentationTrace& presentation,
    u16 masterBrightness)
{
    trace.Presentation = presentation;

    WholeSceneOutputTransformObservation observation = {};
    observation.Kind = WholeSceneOutputTransformKind::DisplaySelection;
    observation.YStart = presentation.YStart;
    observation.YEnd = presentation.YEnd;
    observation.State = presentation.DisplayMode;
    observation.AuxState = presentation.VRAMDisplayReplacement ? 1u : 0u;
    observation.LegacyApplied = presentation.Observed;
    ObserveWholeSceneOutputTransform(plan, trace, observation);

    const u32 brightnessMode = (masterBrightness >> 14) & 0x3u;
    const u32 brightnessFactor = std::min<u32>(masterBrightness & 0x1Fu, 16u);
    if ((brightnessMode == 1 || brightnessMode == 2) && brightnessFactor > 0)
    {
        observation = {};
        observation.Kind = WholeSceneOutputTransformKind::MasterBrightness;
        observation.YStart = presentation.YStart;
        observation.YEnd = presentation.YEnd;
        observation.State = masterBrightness;
        observation.AuxState =
            (presentation.MasterBrightnessAlreadyApplied ? 1u : 0u) |
            (presentation.MasterBrightnessAppliedInFinalPass ? 2u : 0u);
        observation.LegacyApplied =
            (presentation.MasterBrightnessAlreadyApplied ||
             presentation.MasterBrightnessAppliedInFinalPass) &&
            !presentation.MasterBrightnessExecutedByPlan;
        observation.PlanApplied =
            (presentation.MasterBrightnessAlreadyApplied ||
             presentation.MasterBrightnessAppliedInFinalPass) &&
            presentation.MasterBrightnessExecutedByPlan;
        ObserveWholeSceneOutputTransform(plan, trace, observation);
    }

    observation = {};
    observation.Kind = WholeSceneOutputTransformKind::PhysicalRouting;
    observation.YStart = presentation.YStart;
    observation.YEnd = presentation.YEnd;
    observation.State = static_cast<u32>(presentation.PhysicalTarget);
    observation.Target = presentation.PhysicalTarget;
    observation.LegacyApplied = presentation.Observed;
    ObserveWholeSceneOutputTransform(plan, trace, observation);
}

void ObserveWholeSceneOutputPlanExecution(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputExecutionTrace& trace,
    WholeSceneRenderPath executedPath,
    WholeSceneCurrentPathReason executedCurrentReason,
    WholeSceneOutputExecutionKind executedExecutionKind)
{
    trace.ExecutionObserved = true;
    trace.ExecutedPath = executedPath;
    trace.ExecutedCurrentReason = executedCurrentReason;
    trace.ExecutedExecutionKind = executedExecutionKind;
    trace.PathParity = plan.Valid && plan.SelectedPath == executedPath;
    trace.CurrentReasonParity = plan.CurrentReason == executedCurrentReason;
    trace.DecisionParity = trace.PathParity && trace.CurrentReasonParity;
}

WholeScenePhysicalPresentationRecord BuildWholeScenePhysicalPresentationRecord(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOutputExecutionTrace& trace,
    const WholeScenePresentationTrace& presentation,
    WholeScenePhysicalSourceEngine sourceEngine,
    u64 sequence,
    u64 frame,
    bool completionValid)
{
    WholeScenePhysicalPresentationRecord record = {};
    record.Valid = presentation.Observed &&
                   presentation.YStart >= 0 &&
                   presentation.YEnd > presentation.YStart &&
                   presentation.YEnd <= 192 &&
                   presentation.PhysicalTarget !=
                       WholeScenePhysicalTarget::Unknown;
    record.CompletionValid = record.Valid && completionValid;
    record.Sequence = sequence;
    record.Frame = frame;
    record.YStart = presentation.YStart;
    record.YEnd = presentation.YEnd;
    record.Target = presentation.PhysicalTarget;
    record.SourceEngine = sourceEngine;
    record.Purpose = presentation.Purpose;
    record.DisplayMode = presentation.DisplayMode;
    record.VRAMDisplayReplacement = presentation.VRAMDisplayReplacement;

    record.SelectedRepresentation = plan.SelectedRepresentation;
    record.InsertionStage = plan.InsertionStage;
    record.CorrectnessClass = plan.CorrectnessClass;
    if (plan.SelectedRepresentationProduct.Valid)
    {
        const auto& product = plan.SelectedRepresentationProduct;
        record.RepresentationCapabilityMask = product.CapabilityMask;
        record.ProductIdentity = product.ProductIdentity;
        record.SourceGeneration = product.SourceGeneration;
        record.RowEpochIdentity = product.RowEpochIdentity;
    }

    record.SelectedPath = plan.SelectedPath;
    record.SelectedExecutionKind = plan.ExecutionKind;
    record.AdmissionReason = plan.AdmissionReason;
    record.UnsupportedChannels = plan.UnsupportedChannels;
    record.EvidenceUnavailableChannels = plan.EvidenceUnavailableChannels;

    record.ExecutionObserved = trace.ExecutionObserved;
    record.ExecutedPath = trace.ExecutedPath;
    record.ExecutedExecutionKind = trace.ExecutedExecutionKind;
    record.DecisionParity = trace.DecisionParity;
    record.CompositorDisposition = trace.CompositorExecutionDisposition;
    record.CompositorReason = trace.CompositorExecutionReason;
    record.StrictAffinePresentationKind =
        trace.StrictAffinePresentationExecutionKind;
    record.StrictAffinePresentationReason =
        trace.StrictAffinePresentationExecutionReason;

    record.CaptureInputKind = trace.CaptureInput.Kind;
    record.CaptureEventValid = trace.CaptureInput.EventValid;
    record.CaptureEventSerial = trace.CaptureInput.EventSerial;
    record.CaptureEventSourceKind = trace.CaptureInput.EventSourceKind;
    record.CaptureEventProductMask = trace.CaptureInput.EventProductMask;
    record.CaptureEventRejectReason = trace.CaptureInput.EventRejectReason;

    const auto& captureEvidence = plan.CapturePlan.Evidence;
    const auto& captureProduct = captureEvidence.ProductRef;
    record.SelectedCaptureEvidenceAvailable = captureEvidence.Available;
    record.SelectedCaptureContentProven = captureEvidence.ContentProven;
    record.SelectedCapturePresentationProven =
        captureEvidence.PresentationProven;
    record.SelectedCaptureProductKind = captureProduct.Kind;
    record.SelectedCaptureProofKind = captureEvidence.ProofKind;
    record.SelectedCaptureBank = captureProduct.CaptureBank;
    record.SelectedCaptureEventSerial = captureProduct.CaptureEventSerial;
    record.SelectedCaptureBackgroundEpochSerial =
        captureProduct.BackgroundEpochSerial;
    record.SelectedCapturePresentationHash =
        captureProduct.CapturePresentationHash;
    record.SelectedCaptureCurrentPresentationHash =
        captureProduct.CurrentPresentationHash;
    record.SelectedCaptureAuthorizationProofKind =
        captureEvidence.AuthorizationProofKind;
    record.SelectedCaptureAuthorizationEpochSerial =
        captureEvidence.AuthorizationEpochSerial;
    record.SelectedCaptureAuthorizationBank =
        captureEvidence.AuthorizationCaptureBank;
    record.SelectedCaptureSource3DSerial = captureEvidence.Source3DSerial;
    record.SelectedCaptureSource3DSceneHash = captureEvidence.Source3DSceneHash;
    record.SelectedCaptureSourceKind = captureEvidence.SourceKind;
    record.SelectedCaptureProductMask = captureEvidence.ProductMask;

    record.PlannedTransformMask = plan.PlannedTransformMask;
    record.ObservedTransformMask = trace.ObservedTransformMask;
    record.PlanExecutedTransformMask = trace.PlanExecutedTransformMask;
    record.TransformDisagreementMask = trace.TransformDisagreementMask;
    return record;
}

u32 CompareWholeScenePhysicalPresentations(
    const WholeScenePhysicalPresentationRecord& previous,
    const WholeScenePhysicalPresentationRecord& current)
{
    u32 changes = PhysicalPresentationChangeNone;
    if (previous.YStart != current.YStart || previous.YEnd != current.YEnd)
        changes |= PhysicalPresentationChangeScope;
    if (previous.Target != current.Target ||
        previous.SourceEngine != current.SourceEngine ||
        previous.Purpose != current.Purpose ||
        previous.DisplayMode != current.DisplayMode ||
        previous.VRAMDisplayReplacement != current.VRAMDisplayReplacement)
    {
        changes |= PhysicalPresentationChangeRoute;
    }
    if (previous.SelectedRepresentation != current.SelectedRepresentation ||
        previous.InsertionStage != current.InsertionStage ||
        previous.CorrectnessClass != current.CorrectnessClass ||
        previous.RepresentationCapabilityMask !=
            current.RepresentationCapabilityMask)
    {
        changes |= PhysicalPresentationChangeRepresentation;
    }
    if (previous.ProductIdentity != current.ProductIdentity ||
        previous.RowEpochIdentity != current.RowEpochIdentity)
    {
        changes |= PhysicalPresentationChangeContentIdentity;
    }
    if (previous.SourceGeneration != current.SourceGeneration)
        changes |= PhysicalPresentationChangeSourceGeneration;
    if (previous.AdmissionReason != current.AdmissionReason ||
        previous.UnsupportedChannels != current.UnsupportedChannels ||
        previous.EvidenceUnavailableChannels !=
            current.EvidenceUnavailableChannels)
    {
        changes |= PhysicalPresentationChangeCapability;
    }
    if (previous.SelectedPath != current.SelectedPath ||
        previous.SelectedExecutionKind != current.SelectedExecutionKind ||
        previous.ExecutionObserved != current.ExecutionObserved ||
        previous.ExecutedPath != current.ExecutedPath ||
        previous.ExecutedExecutionKind != current.ExecutedExecutionKind ||
        previous.DecisionParity != current.DecisionParity ||
        previous.CompositorDisposition != current.CompositorDisposition ||
        previous.CompositorReason != current.CompositorReason ||
        previous.StrictAffinePresentationKind !=
            current.StrictAffinePresentationKind ||
        previous.StrictAffinePresentationReason !=
            current.StrictAffinePresentationReason)
    {
        changes |= PhysicalPresentationChangeExecution;
    }
    if (previous.CaptureInputKind != current.CaptureInputKind ||
        previous.CaptureEventValid != current.CaptureEventValid ||
        previous.CaptureEventSerial != current.CaptureEventSerial ||
        previous.CaptureEventSourceKind != current.CaptureEventSourceKind ||
        previous.CaptureEventProductMask != current.CaptureEventProductMask ||
        previous.CaptureEventRejectReason != current.CaptureEventRejectReason ||
        previous.SelectedCaptureEvidenceAvailable !=
            current.SelectedCaptureEvidenceAvailable ||
        previous.SelectedCaptureContentProven !=
            current.SelectedCaptureContentProven ||
        previous.SelectedCapturePresentationProven !=
            current.SelectedCapturePresentationProven ||
        previous.SelectedCaptureProductKind !=
            current.SelectedCaptureProductKind ||
        previous.SelectedCaptureProofKind !=
            current.SelectedCaptureProofKind ||
        previous.SelectedCaptureBank != current.SelectedCaptureBank ||
        previous.SelectedCaptureEventSerial !=
            current.SelectedCaptureEventSerial ||
        previous.SelectedCaptureBackgroundEpochSerial !=
            current.SelectedCaptureBackgroundEpochSerial ||
        previous.SelectedCapturePresentationHash !=
            current.SelectedCapturePresentationHash ||
        previous.SelectedCaptureCurrentPresentationHash !=
            current.SelectedCaptureCurrentPresentationHash ||
        previous.SelectedCaptureAuthorizationProofKind !=
            current.SelectedCaptureAuthorizationProofKind ||
        previous.SelectedCaptureAuthorizationEpochSerial !=
            current.SelectedCaptureAuthorizationEpochSerial ||
        previous.SelectedCaptureAuthorizationBank !=
            current.SelectedCaptureAuthorizationBank ||
        previous.SelectedCaptureSource3DSerial !=
            current.SelectedCaptureSource3DSerial ||
        previous.SelectedCaptureSource3DSceneHash !=
            current.SelectedCaptureSource3DSceneHash ||
        previous.SelectedCaptureSourceKind !=
            current.SelectedCaptureSourceKind ||
        previous.SelectedCaptureProductMask !=
            current.SelectedCaptureProductMask)
    {
        changes |= PhysicalPresentationChangeCapture;
    }
    if (previous.PlannedTransformMask != current.PlannedTransformMask ||
        previous.ObservedTransformMask != current.ObservedTransformMask ||
        previous.PlanExecutedTransformMask !=
            current.PlanExecutedTransformMask ||
        previous.TransformDisagreementMask !=
            current.TransformDisagreementMask)
    {
        changes |= PhysicalPresentationChangeTransforms;
    }
    if (previous.Valid != current.Valid ||
        previous.CompletionValid != current.CompletionValid)
    {
        changes |= PhysicalPresentationChangeCompletion;
    }
    return changes;
}

void RecordWholeScenePhysicalPresentation(
    WholeScenePhysicalPresentationLedger& ledger,
    const WholeScenePhysicalPresentationRecord& record)
{
    WholeScenePhysicalPresentationHistory* history = nullptr;
    if (record.Target == WholeScenePhysicalTarget::Top)
        history = &ledger.Top;
    else if (record.Target == WholeScenePhysicalTarget::Bottom)
        history = &ledger.Bottom;
    else if (record.Target == WholeScenePhysicalTarget::Mixed)
    {
        ledger.MixedTargetCount++;
        return;
    }
    else
    {
        ledger.UnknownTargetCount++;
        return;
    }

    const u32 priorCount = history->Count;
    const u32 changes = priorCount > 0
        ? CompareWholeScenePhysicalPresentations(history->Recent[0], record)
        : PhysicalPresentationChangeNone;
    u32 reversions = PhysicalPresentationChangeNone;
    if (priorCount > 1)
    {
        const u32 olderDifferences =
            CompareWholeScenePhysicalPresentations(history->Recent[1], record);
        reversions = changes & ~olderDifferences;
    }

    history->Recent[2] = history->Recent[1];
    history->Recent[1] = history->Recent[0];
    history->Recent[0] = record;
    history->Count = std::min<u32>(priorCount + 1u, 3u);
    history->LastChangeMask = changes;
    history->LastReversionMask = reversions;

    if (priorCount == 0)
        history->LastTransition =
            WholeScenePhysicalPresentationTransition::First;
    else if (reversions != PhysicalPresentationChangeNone)
        history->LastTransition =
            WholeScenePhysicalPresentationTransition::ABAReversion;
    else if (changes == PhysicalPresentationChangeNone)
        history->LastTransition =
            WholeScenePhysicalPresentationTransition::Stable;
    else if ((changes & PhysicalPresentationChangeCompletion) != 0)
        history->LastTransition =
            WholeScenePhysicalPresentationTransition::CompletionChange;
    else if ((changes & PhysicalPresentationChangeCapture) != 0)
        history->LastTransition =
            WholeScenePhysicalPresentationTransition::CaptureHandoff;
    else if ((changes & PhysicalPresentationChangeRepresentation) != 0)
        history->LastTransition =
            WholeScenePhysicalPresentationTransition::RepresentationChange;
    else if ((changes & PhysicalPresentationChangeRoute) != 0)
        history->LastTransition =
            WholeScenePhysicalPresentationTransition::RouteHandoff;
    else if ((changes & PhysicalPresentationChangeTransforms) != 0)
        history->LastTransition =
            WholeScenePhysicalPresentationTransition::TransformChange;
    else
        history->LastTransition =
            WholeScenePhysicalPresentationTransition::Changed;
}

const WholeScenePhysicalPresentationHistory*
GetWholeScenePhysicalPresentationHistory(
    const WholeScenePhysicalPresentationLedger& ledger,
    WholeScenePhysicalTarget target)
{
    if (target == WholeScenePhysicalTarget::Top)
        return &ledger.Top;
    if (target == WholeScenePhysicalTarget::Bottom)
        return &ledger.Bottom;
    return nullptr;
}

void AppendWholeScenePhysicalPresentationCSVHeader(
    std::string& header,
    const char* prefix)
{
    const auto add = [&](const char* name)
    {
        if (!header.empty())
            header += ',';
        if (prefix && *prefix)
        {
            header += prefix;
            header += '_';
        }
        header += name;
    };

    add("physical_history_count");
    add("physical_transition");
    add("physical_change_mask");
    add("physical_reversion_mask");
    add("physical_valid");
    add("physical_completion_valid");
    add("physical_sequence");
    add("physical_frame");
    add("physical_y_start");
    add("physical_y_end");
    add("physical_target");
    add("physical_source_engine");
    add("physical_purpose");
    add("physical_display_mode");
    add("physical_vram_replacement");
    add("physical_representation");
    add("physical_insertion_stage");
    add("physical_correctness_class");
    add("physical_representation_capabilities");
    add("physical_product_identity");
    add("physical_source_generation");
    add("physical_row_epoch_identity");
    add("physical_selected_path");
    add("physical_selected_execution");
    add("physical_admission_reason");
    add("physical_unsupported_channels");
    add("physical_evidence_unavailable_channels");
    add("physical_execution_observed");
    add("physical_executed_path");
    add("physical_executed_execution");
    add("physical_decision_parity");
    add("physical_compositor_disposition");
    add("physical_compositor_reason");
    add("physical_strict_affine_kind");
    add("physical_strict_affine_reason");
    add("physical_capture_kind");
    add("physical_capture_event_valid");
    add("physical_capture_event_serial");
    add("physical_capture_source_kind");
    add("physical_capture_product_mask");
    add("physical_capture_reject_reason");
    add("physical_selected_capture_evidence_available");
    add("physical_selected_capture_content_proven");
    add("physical_selected_capture_presentation_proven");
    add("physical_selected_capture_product_kind");
    add("physical_selected_capture_proof_kind");
    add("physical_selected_capture_bank");
    add("physical_selected_capture_event_serial");
    add("physical_selected_capture_background_epoch_serial");
    add("physical_selected_capture_presentation_hash");
    add("physical_selected_capture_current_presentation_hash");
    add("physical_selected_capture_authorization_proof_kind");
    add("physical_selected_capture_authorization_epoch_serial");
    add("physical_selected_capture_authorization_bank");
    add("physical_selected_capture_source_3d_serial");
    add("physical_selected_capture_source_3d_scene_hash");
    add("physical_selected_capture_source_kind");
    add("physical_selected_capture_product_mask");
    add("physical_planned_transform_mask");
    add("physical_observed_transform_mask");
    add("physical_executed_transform_mask");
    add("physical_transform_disagreement_mask");
}

void AppendWholeScenePhysicalPresentationCSVRow(
    std::string& row,
    const WholeScenePhysicalPresentationHistory& history)
{
    const auto add = [&](u64 value)
    {
        if (!row.empty())
            row += ',';
        row += std::to_string(value);
    };
    const WholeScenePhysicalPresentationRecord empty = {};
    const auto& record = history.Count > 0 ? history.Recent[0] : empty;

    add(history.Count);
    add(static_cast<u32>(history.LastTransition));
    add(history.LastChangeMask);
    add(history.LastReversionMask);
    add(record.Valid);
    add(record.CompletionValid);
    add(record.Sequence);
    add(record.Frame);
    add(record.YStart);
    add(record.YEnd);
    add(static_cast<u32>(record.Target));
    add(static_cast<u32>(record.SourceEngine));
    add(static_cast<u32>(record.Purpose));
    add(record.DisplayMode);
    add(record.VRAMDisplayReplacement);
    add(static_cast<u32>(record.SelectedRepresentation));
    add(static_cast<u32>(record.InsertionStage));
    add(static_cast<u32>(record.CorrectnessClass));
    add(record.RepresentationCapabilityMask);
    add(record.ProductIdentity);
    add(record.SourceGeneration);
    add(record.RowEpochIdentity);
    add(static_cast<u32>(record.SelectedPath));
    add(static_cast<u32>(record.SelectedExecutionKind));
    add(static_cast<u32>(record.AdmissionReason));
    add(record.UnsupportedChannels);
    add(record.EvidenceUnavailableChannels);
    add(record.ExecutionObserved);
    add(static_cast<u32>(record.ExecutedPath));
    add(static_cast<u32>(record.ExecutedExecutionKind));
    add(record.DecisionParity);
    add(static_cast<u32>(record.CompositorDisposition));
    add(static_cast<u32>(record.CompositorReason));
    add(static_cast<u32>(record.StrictAffinePresentationKind));
    add(static_cast<u32>(record.StrictAffinePresentationReason));
    add(static_cast<u32>(record.CaptureInputKind));
    add(record.CaptureEventValid);
    add(record.CaptureEventSerial);
    add(record.CaptureEventSourceKind);
    add(record.CaptureEventProductMask);
    add(record.CaptureEventRejectReason);
    add(record.SelectedCaptureEvidenceAvailable);
    add(record.SelectedCaptureContentProven);
    add(record.SelectedCapturePresentationProven);
    add(static_cast<u32>(record.SelectedCaptureProductKind));
    add(static_cast<u32>(record.SelectedCaptureProofKind));
    add(record.SelectedCaptureBank);
    add(record.SelectedCaptureEventSerial);
    add(record.SelectedCaptureBackgroundEpochSerial);
    add(record.SelectedCapturePresentationHash);
    add(record.SelectedCaptureCurrentPresentationHash);
    add(static_cast<u32>(record.SelectedCaptureAuthorizationProofKind));
    add(record.SelectedCaptureAuthorizationEpochSerial);
    add(record.SelectedCaptureAuthorizationBank);
    add(record.SelectedCaptureSource3DSerial);
    add(record.SelectedCaptureSource3DSceneHash);
    add(record.SelectedCaptureSourceKind);
    add(record.SelectedCaptureProductMask);
    add(record.PlannedTransformMask);
    add(record.ObservedTransformMask);
    add(record.PlanExecutedTransformMask);
    add(record.TransformDisagreementMask);
}

void AppendWholeSceneOutputPlanCSVHeader(std::string& header, const char* prefix)
{
    auto add = [&](const std::string& name)
    {
        if (!header.empty())
            header += ',';
        if (prefix)
        {
            header += prefix;
            header += '_';
        }
        header += name;
    };

    add("output_plan_version");
    add("output_plan_valid");
    add("output_plan_transforms_shadow_only");
    add("output_plan_ystart");
    add("output_plan_yend");
    add("output_plan_output_scale");
    add("output_plan_decision_source");
    add("output_plan_path_reason");
    add("output_plan_scale_reason");
    add("output_plan_strict_affine_block_reason");
    add("output_plan_selected_path");
    add("output_plan_current_reason");
    add("output_plan_execution_kind");
    add("output_plan_execution_ystart");
    add("output_plan_execution_yend");
    add("output_plan_execution_current_fragmentation_fallback");
    add("output_plan_capture_kind");
    add("output_plan_capture_stage");
    add("output_plan_capture_role");
    add("output_plan_capture_request_kind");
    add("output_plan_capture_route_slot");
    add("output_execution_source_a_selection_observed");
    add("output_execution_source_a_primary_resolution");
    add("output_execution_source_a_primary_reason");
    add("output_execution_source_a_overlay_failure_resolution");
    add("output_execution_source_a_overlay_failure_reason");
    add("output_execution_source_a_full_product_presentation_proof");
    add("output_plan_representation_evidence_available");
    add("output_plan_representation_evidence_content_proven");
    add("output_plan_representation_evidence_presentation_proven");
    add("output_plan_representation_evidence_product_kind");
    add("output_plan_representation_evidence_background_source");
    add("output_plan_representation_evidence_route_slot");
    add("output_plan_representation_evidence_capture_bank");
    add("output_plan_representation_evidence_event_serial");
    add("output_plan_representation_evidence_background_epoch_serial");
    add("output_plan_representation_evidence_capture_presentation_hash");
    add("output_plan_representation_evidence_current_presentation_hash");
    add("output_plan_representation_evidence_proof_kind");
    add("output_plan_representation_evidence_authorization_proof_kind");
    add("output_plan_representation_evidence_authorization_epoch_serial");
    add("output_plan_representation_evidence_authorization_capture_bank");
    add("output_plan_representation_evidence_authorization_presentation_hash");
    add("output_plan_representation_evidence_source_3d_serial");
    add("output_plan_representation_evidence_source_3d_scene_hash");
    add("output_plan_representation_evidence_source_kind");
    add("output_plan_representation_evidence_product_mask");
    add("output_plan_representation_evidence_output_effect_owner");
    add("output_plan_representation_evidence_output_effect_state");
    add("output_plan_representation_evidence_output_pre_master");
    add("output_plan_representation_evidence_capture_input_proven");
    add("output_plan_representation_evidence_capture_cnt");
    add("output_plan_representation_evidence_capture_uses_presentation");
    add("output_plan_representation_evidence_capture_uses_source_b");
    add("output_plan_representation_evidence_can_publish_route_product");
    add("output_plan_representation_evidence_trace_observed");
    add("output_plan_representation_evidence_content_trace_agreement");
    add("output_plan_representation_evidence_presentation_trace_comparable");
    add("output_plan_representation_evidence_presentation_trace_agreement");
    add("output_plan_representation_evidence_capture_input_trace_comparable");
    add("output_plan_representation_evidence_capture_input_trace_agreement");
    add("output_plan_considered_repr_mask");
    add("output_plan_selected_repr");
    add("output_plan_selected_product_valid");
    add("output_plan_selected_product_kind");
    add("output_plan_selected_product_terminal");
    add("output_plan_selected_product_compositor_input_eligible");
    add("output_plan_selected_product_coordinate_domain");
    add("output_plan_selected_product_coverage_authority");
    add("output_plan_selected_product_capability_mask");
    add("output_plan_selected_product_ystart");
    add("output_plan_selected_product_yend");
    add("output_plan_selected_product_lifetime");
    add("output_plan_selected_product_source_generation");
    add("output_plan_selected_product_row_epoch_identity");
    add("output_plan_selected_product_identity");
    add("output_plan_insertion_stage");
    add("output_plan_correctness_class");
    add("output_plan_admission_evaluated");
    add("output_plan_admission_changed_selection");
    add("output_plan_admission_reason");
    add("output_plan_initial_selected_path");
    add("output_plan_initial_current_reason");
    add("output_plan_initial_selected_repr");
    add("output_plan_admission_rejected_unsupported_channels");
    add("output_plan_admission_rejected_evidence_channels");
    add("output_plan_channels_assessed");
    add("output_plan_required_channels");
    add("output_plan_reproduced_channels");
    add("output_plan_inactive_channels");
    add("output_plan_unsupported_channels");
    add("output_plan_evidence_unavailable_channels");
    add("output_plan_effect_evidence_available");
    add("output_plan_effect_evidence_operand_source");
    add("output_plan_effect_evidence_proof_mask");
    add("output_plan_effect_evidence_candidate_channels");
    add("output_plan_effect_evidence_proven_channels");
    add("output_plan_effect_evidence_blendcnt");
    add("output_plan_effect_evidence_blend_mode");
    add("output_plan_effect_evidence_target1_mask");
    add("output_plan_effect_evidence_target2_mask");
    add("output_plan_effect_evidence_eva");
    add("output_plan_effect_evidence_evb");
    add("output_plan_effect_evidence_evy");
    add("output_plan_effect_evidence_direct3d_target1");
    add("output_plan_effect_evidence_direct3d_target2");
    add("output_plan_effect_evidence_semitransparent_obj_mode_mask");
    add("output_plan_effect_evidence_obj_target1");
    add("output_plan_effect_evidence_obj_target2");
    add("output_plan_compositor_effect_channel_mask");
    add("output_plan_compositor_effect_blendcnt");
    add("output_plan_compositor_effect_eva");
    add("output_plan_compositor_effect_evb");
    add("output_plan_compositor_effect_evy");
    add("output_plan_compositor_effect_semitransparent_obj_mask");
    add("output_plan_compositor_effect_state_hash");
    add("output_plan_transform_count");
    add("output_plan_planned_transform_mask");
    add("output_plan_observed_transform_mask");
    add("output_plan_plan_executed_transform_mask");
    add("output_plan_unobserved_transform_mask");
    add("output_plan_transform_disagreement_mask");
    add("output_plan_unexpected_transform_mask");
    for (u32 i = 0; i < WholeSceneMaxOutputTransforms; i++)
    {
        const std::string tag = "output_plan_transform" + std::to_string(i);
        add(tag + "_kind");
        add(tag + "_stage");
        add(tag + "_ystart");
        add(tag + "_yend");
        add(tag + "_state");
        add(tag + "_aux_state");
        add(tag + "_target");
        add(tag + "_execution_observed");
        add(tag + "_legacy_applied");
        add(tag + "_observed_ystart");
        add(tag + "_observed_yend");
        add(tag + "_observed_state");
        add(tag + "_observed_aux_state");
        add(tag + "_observed_target");
        add(tag + "_plan_executed");
        add(tag + "_trace_agreement");
    }
    add("output_plan_capture_input_observed");
    add("output_plan_capture_input_ystart");
    add("output_plan_capture_input_yend");
    add("output_plan_capture_input_cnt");
    add("output_plan_capture_input_kind");
    add("output_plan_capture_input_presentation_repr");
    add("output_plan_capture_input_uses_presentation");
    add("output_plan_capture_input_authoritative_pre_master");
    add("output_plan_capture_input_native_sized");
    add("output_plan_capture_input_uses_source_b");
    add("output_plan_capture_input_source_b_tracked");
    add("output_plan_capture_input_source_b_same_dst");
    add("output_plan_capture_input_event_valid");
    add("output_plan_capture_input_event_serial");
    add("output_plan_capture_input_event_source_kind");
    add("output_plan_capture_input_event_product_mask");
    add("output_plan_capture_input_event_reject_reason");
    add("output_plan_presentation_observed");
    add("output_plan_presentation_purpose");
    add("output_plan_presentation_ystart");
    add("output_plan_presentation_yend");
    add("output_plan_presentation_display_mode");
    add("output_plan_presentation_vram_replacement");
    add("output_plan_presentation_brightness_preapplied");
    add("output_plan_presentation_brightness_final_pass");
    add("output_plan_presentation_brightness_plan_executed");
    add("output_plan_presentation_physical_target");
    add("output_plan_selector_configured");
    add("output_plan_selector_kind");
    add("output_plan_selector_legacy_reason");
    add("output_plan_selector_conservative_requested");
    add("output_plan_selector_conservative_effective");
    add("output_plan_selector_suppressed_3d_overlay");
    add("output_plan_selector_active_3d");
    add("output_plan_selector_native_fallback");
    add("output_plan_selector_foreground_2d_base");
    add("output_plan_selector_foreground_candidate");
    add("output_plan_selector_window_edge_assist");
    add("output_plan_selector_target2_alpha_assist");
    add("output_plan_selector_native_effect_guard");
    add("output_plan_selector_clean_legacy_enabled");
    add("output_plan_selector_clean_legacy_block_reason");
    add("output_plan_execution_observed");
    add("output_plan_executed_path");
    add("output_plan_executed_current_reason");
    add("output_plan_executed_execution_kind");
    add("output_plan_path_parity");
    add("output_plan_current_reason_parity");
    add("output_plan_compositor_recipe_binding_evaluated");
    add("output_plan_compositor_recipe_bound");
    add("output_plan_compositor_recipe_binding_reason");
    add("output_plan_compositor_fallback_policy");
    add("output_plan_compositor_execution_disposition");
    add("output_plan_compositor_execution_reason");
    add("output_plan_prepared_compositor_recipe_hash");
    add("output_plan_compositor_recipe_ready");
    add("output_plan_compositor_recipe_rejection_mask");
    add("output_plan_compositor_recipe_kind");
    add("output_plan_compositor_recipe_input_count");
    add("output_plan_compositor_recipe_hash");
    add("output_plan_compositor_recipe_effect_owner");
    add("output_plan_compositor_recipe_effect_state_hash");
    add("output_plan_compositor_recipe_row_epoch_identity");
    add("output_plan_compositor_recipe_products_cover_scope");
    add("output_plan_compositor_recipe_execution_observed");
    add("output_plan_executed_compositor_recipe_hash");
    add("output_plan_executed_compositor_recipe_ready");
    add("output_plan_executed_compositor_recipe_rejection_mask");
    add("output_plan_executed_compositor_recipe_input_count");
    add("output_plan_executed_compositor_recipe_resolved_obj");
    add("output_plan_executed_compositor_recipe_enhanced_bg_mask");
    add("output_plan_executed_compositor_recipe_coverage_bg_mask");
    add("output_plan_compositor_recipe_parity");
    add("output_plan_ordered_recipe_binding_evaluated");
    add("output_plan_ordered_recipe_bound");
    add("output_plan_ordered_recipe_binding_reason");
    add("output_plan_ordered_fallback_policy");
    add("output_plan_ordered_execution_disposition");
    add("output_plan_ordered_execution_reason");
    add("output_plan_prepared_ordered_base_recipe_hash");
    add("output_plan_prepared_ordered_recipe_hash");
    add("output_plan_ordered_recipe_ready");
    add("output_plan_ordered_recipe_rejection_mask");
    add("output_plan_ordered_recipe_base_hash");
    add("output_plan_ordered_recipe_dependency_hash");
    add("output_plan_ordered_recipe_input_count");
    add("output_plan_ordered_recipe_hash");
    add("output_plan_ordered_recipe_effect_owner");
    add("output_plan_ordered_recipe_effect_state_hash");
    add("output_plan_ordered_recipe_row_epoch_identity");
    add("output_plan_ordered_recipe_products_cover_scope");
    add("output_plan_ordered_recipe_base_execution_observed");
    add("output_plan_executed_ordered_recipe_base_hash");
    add("output_plan_ordered_recipe_base_parity");
    add("output_plan_ordered_recipe_execution_observed");
    add("output_plan_executed_ordered_recipe_hash");
    add("output_plan_executed_ordered_recipe_ready");
    add("output_plan_executed_ordered_recipe_rejection_mask");
    add("output_plan_executed_ordered_recipe_input_count");
    add("output_plan_ordered_recipe_parity");
    add("output_plan_overlay_base_recipe_binding_evaluated");
    add("output_plan_overlay_base_recipe_bound");
    add("output_plan_overlay_base_recipe_binding_reason");
    add("output_plan_overlay_base_fallback_policy");
    add("output_plan_overlay_base_execution_disposition");
    add("output_plan_overlay_base_execution_reason");
    add("output_plan_prepared_overlay_base_recipe_hash");
    add("output_plan_overlay_base_recipe_ready");
    add("output_plan_overlay_base_recipe_rejection_mask");
    add("output_plan_overlay_base_recipe_semantic_dependency_hash");
    add("output_plan_overlay_base_recipe_ordered_dependency_hash");
    add("output_plan_overlay_base_recipe_first_excluded_operand");
    add("output_plan_overlay_base_recipe_hash");
    add("output_plan_overlay_base_recipe_effect_state_hash");
    add("output_plan_overlay_base_recipe_row_epoch_identity");
    add("output_plan_overlay_base_recipe_execution_observed");
    add("output_plan_executed_overlay_base_recipe_hash");
    add("output_plan_overlay_base_recipe_parity");
    add("output_plan_strict_affine_presentation_kind");
    add("output_plan_strict_affine_presentation_reason");
    add("output_plan_strict_affine_presentation_affine_obj_insertion");
    add("output_plan_strict_affine_product_semantic_recipe_ready");
    add("output_plan_strict_affine_product_resolved_obj_surface_ready");
    add("output_plan_strict_affine_product_ordered_operands_ready");
    add("output_plan_strict_affine_product_operand_excluded_overlay_ready");
    add("output_plan_strict_affine_product_atomic_ordinary_band_ready");
    add("output_plan_strict_affine_product_native_stack_ready");
    add("output_plan_strict_affine_product_affine_obj_insertion_ready");
    add("output_plan_strict_affine_product_affine_obj_subpixel_2x_ready");
    add("output_plan_ordered_affine_output_grid_operand_count");
    add("output_plan_ordered_affine_subpixel_2x_operand_count");
    add("output_plan_ordered_opaque_assembly_count");
    add("output_plan_ordered_opaque_assembly_rebuild_count");
    add("output_plan_decision_parity");
}

void AppendWholeSceneOutputPlanCSVRow(
    std::string& row,
    const WholeSceneOutputPlan& plan,
    const WholeSceneOutputRecipePreparation& recipes,
    const WholeSceneOutputExecutionTrace& trace)
{
    auto add = [&](auto value)
    {
        if (!row.empty())
            row += ',';
        row += std::to_string(static_cast<long long>(value));
    };

    add(plan.Version);
    add(plan.Valid);
    add(trace.TransformsShadowOnly);
    add(plan.YStart);
    add(plan.YEnd);
    add(plan.OutputScale);
    add(plan.DecisionSource);
    add(plan.PathReason);
    add(plan.ScaleReason);
    add(plan.StrictAffineBlockReason);
    add(plan.SelectedPath);
    add(plan.CurrentReason);
    add(plan.ExecutionKind);
    add(plan.ExecutionYStart);
    add(plan.ExecutionYEnd);
    add(plan.ExecutionCurrentFragmentationFallback);
    add(plan.CapturePlan.Kind);
    add(plan.CapturePlan.Stage);
    add(plan.CapturePlan.Role);
    add(plan.CapturePlan.RequestKind);
    add(plan.CapturePlan.CaptureEpochOverlayRouteSlot);
    add(trace.SourceASelectionObserved);
    add(trace.SourceASelection.Primary);
    add(trace.SourceASelection.PrimaryReason);
    add(trace.SourceASelection.AfterOverlayFailure);
    add(trace.SourceASelection.AfterOverlayFailureReason);
    add(trace.SourceASelection.FullProductPresentationProof);
    const WholeSceneCaptureRepresentationEvidence& evidence =
        plan.RepresentationEvidence;
    add(evidence.Available);
    add(evidence.ContentProven);
    add(evidence.PresentationProven);
    add(evidence.ProductRef.Kind);
    add(evidence.ProductRef.BackgroundSource);
    add(evidence.ProductRef.RouteSlot);
    add(evidence.ProductRef.CaptureBank);
    add(evidence.ProductRef.CaptureEventSerial);
    add(evidence.ProductRef.BackgroundEpochSerial);
    add(evidence.ProductRef.CapturePresentationHash);
    add(evidence.ProductRef.CurrentPresentationHash);
    add(evidence.ProofKind);
    add(evidence.AuthorizationProofKind);
    add(evidence.AuthorizationEpochSerial);
    add(evidence.AuthorizationCaptureBank);
    add(evidence.AuthorizationPresentationHash);
    add(evidence.Source3DSerial);
    add(evidence.Source3DSceneHash);
    add(evidence.SourceKind);
    add(evidence.ProductMask);
    add(evidence.OutputEffectOwner);
    add(evidence.OutputEffectState);
    add(evidence.OutputPreMaster);
    add(evidence.CaptureInputProven);
    add(evidence.CaptureCnt);
    add(evidence.CaptureConsumesSelectedPresentation);
    add(evidence.CaptureUsesSourceB);
    add(evidence.CanPublishRouteProduct);
    add(trace.RepresentationEvidenceTraceObserved);
    add(trace.RepresentationEvidenceContentTraceAgreement);
    add(trace.RepresentationEvidencePresentationTraceComparable);
    add(trace.RepresentationEvidencePresentationTraceAgreement);
    add(trace.RepresentationEvidenceCaptureInputTraceComparable);
    add(trace.RepresentationEvidenceCaptureInputTraceAgreement);
    add(plan.ConsideredRepresentationMask);
    add(plan.SelectedRepresentation);
    add(plan.SelectedRepresentationProduct.Valid);
    add(plan.SelectedRepresentationProduct.Kind);
    add(plan.SelectedRepresentationProduct.TerminalCompleted);
    add(plan.SelectedRepresentationProduct.CompositorInputEligible);
    add(plan.SelectedRepresentationProduct.CoordinateDomain);
    add(plan.SelectedRepresentationProduct.CoverageAuthority);
    add(plan.SelectedRepresentationProduct.CapabilityMask);
    add(plan.SelectedRepresentationProduct.ValidYStart);
    add(plan.SelectedRepresentationProduct.ValidYEnd);
    add(plan.SelectedRepresentationProduct.Lifetime);
    add(plan.SelectedRepresentationProduct.SourceGeneration);
    add(plan.SelectedRepresentationProduct.RowEpochIdentity);
    add(plan.SelectedRepresentationProduct.ProductIdentity);
    add(plan.InsertionStage);
    add(plan.CorrectnessClass);
    add(plan.AdmissionEvaluated);
    add(plan.AdmissionChangedSelection);
    add(plan.AdmissionReason);
    add(plan.InitialSelectedPath);
    add(plan.InitialCurrentReason);
    add(plan.InitialSelectedRepresentation);
    add(plan.AdmissionRejectedUnsupportedChannels);
    add(plan.AdmissionRejectedEvidenceUnavailableChannels);
    add(plan.ChannelsAssessed);
    add(plan.RequiredChannels);
    add(plan.ReproducedChannels);
    add(plan.InactiveChannels);
    add(plan.UnsupportedChannels);
    add(plan.EvidenceUnavailableChannels);
    add(plan.EffectEvidence.Available);
    add(plan.EffectEvidence.OperandSource);
    add(plan.EffectEvidence.ProofMask);
    add(plan.EffectEvidence.CandidateChannelMask);
    add(plan.EffectEvidence.ProvenOwnershipChannelMask);
    add(plan.EffectEvidence.BlendCnt);
    add(plan.EffectEvidence.BlendMode);
    add(plan.EffectEvidence.Target1Mask);
    add(plan.EffectEvidence.Target2Mask);
    add(plan.EffectEvidence.EVA);
    add(plan.EffectEvidence.EVB);
    add(plan.EffectEvidence.EVY);
    add(plan.EffectEvidence.Direct3DTarget1);
    add(plan.EffectEvidence.Direct3DTarget2);
    add(plan.EffectEvidence.SemiTransparentOBJModeMask);
    add(plan.EffectEvidence.OBJTarget1);
    add(plan.EffectEvidence.OBJTarget2);
    add(plan.CompositorEffectState.ChannelMask);
    add(plan.CompositorEffectState.BlendCnt);
    add(plan.CompositorEffectState.EVA);
    add(plan.CompositorEffectState.EVB);
    add(plan.CompositorEffectState.EVY);
    add(plan.CompositorEffectState.SemiTransparentOBJModeMask);
    add(plan.CompositorEffectState.StateHash);
    add(plan.TransformCount);
    add(plan.PlannedTransformMask);
    add(trace.ObservedTransformMask);
    add(trace.PlanExecutedTransformMask);
    add(WholeSceneOutputUnobservedTransformMask(plan, trace));
    add(trace.TransformDisagreementMask);
    add(trace.UnexpectedObservedTransformMask);
    for (u32 i = 0; i < WholeSceneMaxOutputTransforms; i++)
    {
        const WholeSceneOutputTransform& transform = plan.Transforms[i];
        const WholeSceneOutputTransformExecutionTrace& observed =
            trace.Transforms[i];
        add(transform.Kind);
        add(transform.OwningStage);
        add(transform.YStart);
        add(transform.YEnd);
        add(transform.State);
        add(transform.AuxState);
        add(transform.Target);
        add(observed.ExecutionObserved);
        add(observed.LegacyApplied);
        add(observed.ObservedYStart);
        add(observed.ObservedYEnd);
        add(observed.ObservedState);
        add(observed.ObservedAuxState);
        add(observed.ObservedTarget);
        add(observed.PlanExecuted);
        add(observed.TraceAgreement);
    }
    add(trace.CaptureInput.Observed);
    add(trace.CaptureInput.YStart);
    add(trace.CaptureInput.YEnd);
    add(trace.CaptureInput.CaptureCnt);
    add(trace.CaptureInput.Kind);
    add(trace.CaptureInput.PresentationRepresentation);
    add(trace.CaptureInput.UsesSelectedPresentation);
    add(trace.CaptureInput.AuthoritativePreMaster);
    add(trace.CaptureInput.NativeSized);
    add(trace.CaptureInput.UsesSourceB);
    add(trace.CaptureInput.SourceBTrackedCapture);
    add(trace.CaptureInput.SourceBSameDestination);
    add(trace.CaptureInput.EventValid);
    add(trace.CaptureInput.EventSerial);
    add(trace.CaptureInput.EventSourceKind);
    add(trace.CaptureInput.EventProductMask);
    add(trace.CaptureInput.EventRejectReason);
    add(trace.Presentation.Observed);
    add(trace.Presentation.Purpose);
    add(trace.Presentation.YStart);
    add(trace.Presentation.YEnd);
    add(trace.Presentation.DisplayMode);
    add(trace.Presentation.VRAMDisplayReplacement);
    add(trace.Presentation.MasterBrightnessAlreadyApplied);
    add(trace.Presentation.MasterBrightnessAppliedInFinalPass);
    add(trace.Presentation.MasterBrightnessExecutedByPlan);
    add(trace.Presentation.PhysicalTarget);
    add(plan.Selector.Configured);
    add(plan.Selector.Kind);
    add(plan.Selector.LegacyReason);
    add(plan.Selector.ConservativeHybridRequested);
    add(plan.Selector.EffectiveConservativeHybrid);
    add(plan.Selector.OverlaySuppressedDirect3D);
    add(plan.Selector.ActiveDirect3D);
    add(plan.Selector.RenderNativeFallback);
    add(plan.Selector.RenderForeground2DBase);
    add(plan.Selector.RenderForegroundCandidate);
    add(plan.Selector.WindowEdgeAssist);
    add(plan.Selector.Target2AlphaBlendAssist);
    add(plan.Selector.NativeEffectGuard);
    add(plan.Selector.CleanLegacyCandidateEnabled);
    add(plan.Selector.CleanLegacyBlockReason);
    add(trace.ExecutionObserved);
    add(trace.ExecutedPath);
    add(trace.ExecutedCurrentReason);
    add(trace.ExecutedExecutionKind);
    add(trace.PathParity);
    add(trace.CurrentReasonParity);
    const auto& compositorBinding = recipes.Compositor;
    const auto& orderedBinding = recipes.OrderedPresentation;
    const auto& overlayBinding = recipes.OperandExcludedOverlay;
    add(compositorBinding.Evaluated);
    add(compositorBinding.Bound);
    add(compositorBinding.Reason);
    add(plan.CompositorFallbackPolicy);
    add(trace.CompositorExecutionDisposition);
    add(trace.CompositorExecutionReason);
    add(trace.PreparedCompositorRecipeHash);
    add(compositorBinding.Recipe.Ready);
    add(compositorBinding.Recipe.RejectionMask);
    add(compositorBinding.Recipe.Kind);
    add(compositorBinding.Recipe.Inputs.size());
    add(compositorBinding.Recipe.RecipeHash);
    add(compositorBinding.Recipe.EffectOwner);
    add(compositorBinding.Recipe.EffectState.StateHash);
    add(compositorBinding.Recipe.RowEpochIdentity);
    add(WholeSceneCompositorRecipeProductsCoverScope(
        compositorBinding.Recipe));
    add(trace.CompositorRecipeExecutionObserved);
    add(trace.ExecutedCompositorRecipeHash);
    add(trace.ExecutedCompositorRecipe.Ready);
    add(trace.ExecutedCompositorRecipe.RejectionMask);
    add(trace.ExecutedCompositorRecipe.Inputs.size());
    add(WholeSceneCompositorRecipeHasInput(
        trace.ExecutedCompositorRecipe,
        WholeSceneCompositorInputKind::ResolvedOBJSurface));
    add(WholeSceneCompositorRecipeEnhancedBGMask(
        trace.ExecutedCompositorRecipe));
    add(WholeSceneCompositorRecipeReconstructedBGCoverageMask(
        trace.ExecutedCompositorRecipe));
    add(trace.CompositorRecipeParity);
    add(orderedBinding.Evaluated);
    add(orderedBinding.Bound);
    add(orderedBinding.Reason);
    add(plan.OrderedPresentationFallbackPolicy);
    add(trace.OrderedPresentationExecutionDisposition);
    add(trace.OrderedPresentationExecutionReason);
    add(trace.PreparedOrderedPresentationBaseRecipeHash);
    add(trace.PreparedOrderedPresentationRecipeHash);
    add(orderedBinding.Recipe.Ready);
    add(orderedBinding.Recipe.RejectionMask);
    add(orderedBinding.SemanticBaseRecipe.RecipeHash);
    add(orderedBinding.Recipe.DependencyRecipeHash);
    add(orderedBinding.Recipe.Inputs.size());
    add(orderedBinding.Recipe.RecipeHash);
    add(orderedBinding.Recipe.EffectOwner);
    add(orderedBinding.Recipe.EffectState.StateHash);
    add(orderedBinding.Recipe.RowEpochIdentity);
    add(WholeSceneCompositorRecipeProductsCoverScope(
        orderedBinding.Recipe));
    add(trace.OrderedPresentationBaseRecipeExecutionObserved);
    add(trace.ExecutedOrderedPresentationBaseRecipeHash);
    add(trace.OrderedPresentationBaseRecipeParity);
    add(trace.OrderedPresentationRecipeExecutionObserved);
    add(trace.ExecutedOrderedPresentationRecipeHash);
    add(trace.ExecutedOrderedPresentationRecipe.Ready);
    add(trace.ExecutedOrderedPresentationRecipe.RejectionMask);
    add(trace.ExecutedOrderedPresentationRecipe.Inputs.size());
    add(trace.OrderedPresentationRecipeParity);
    add(overlayBinding.Evaluated);
    add(overlayBinding.Bound);
    add(overlayBinding.Reason);
    add(plan.OperandExcludedOverlayFallbackPolicy);
    add(trace.OperandExcludedOverlayExecutionDisposition);
    add(trace.OperandExcludedOverlayExecutionReason);
    add(trace.PreparedOperandExcludedOverlayRecipeHash);
    add(overlayBinding.Recipe.Ready);
    add(overlayBinding.Recipe.RejectionMask);
    add(overlayBinding.Recipe.SemanticBaseRecipeHash);
    add(overlayBinding.Recipe.OrderedPresentationRecipeHash);
    add(overlayBinding.Recipe.FirstExcludedOperand);
    add(overlayBinding.Recipe.RecipeHash);
    add(overlayBinding.Recipe.EffectState.StateHash);
    add(overlayBinding.Recipe.RowEpochIdentity);
    add(trace.OperandExcludedOverlayRecipeExecutionObserved);
    add(trace.ExecutedOperandExcludedOverlayRecipeHash);
    add(trace.OperandExcludedOverlayRecipeParity);
    add(trace.StrictAffinePresentationExecutionKind);
    add(trace.StrictAffinePresentationExecutionReason);
    add(trace.StrictAffinePresentationAffineOBJInsertion);
    add(trace.StrictAffineProductAssessment.SemanticRecipeReady);
    add(trace.StrictAffineProductAssessment.ResolvedOBJSurfaceReady);
    add(trace.StrictAffineProductAssessment.OrderedOperandsReady);
    add(trace.StrictAffineProductAssessment.OperandExcludedOverlayReady);
    add(trace.StrictAffineProductAssessment.AtomicOrdinaryBandReady);
    add(trace.StrictAffineProductAssessment.NativeStackReady);
    add(trace.StrictAffineProductAssessment.AffineOBJInsertionReady);
    add(trace.StrictAffineProductAssessment.AffineOBJSubpixel2xReady);
    add(trace.OrderedAffineOutputGridOperandCount);
    add(trace.OrderedAffineSubpixel2xOperandCount);
    add(trace.OrderedOpaqueAssemblyCount);
    add(trace.OrderedOpaqueAssemblyRebuildCount);
    add(trace.DecisionParity);
}

}
