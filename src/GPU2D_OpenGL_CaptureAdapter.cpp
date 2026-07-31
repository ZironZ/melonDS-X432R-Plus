/*
    Copyright 2026 ZironZ

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <algorithm>

#include "GPU_OpenGL.h"
#include "GPU2D_OpenGL.h"

namespace melonDS
{
namespace
{

u32 PackedMasterBrightnessEffectState(u16 masterBrightness)
{
    const u32 mode = (masterBrightness >> 14) & 0x3;
    const u32 factor = std::min<u32>(masterBrightness & 0x1F, 16);
    return (mode << 8) | factor;
}

WholeSceneCaptureEffectOwner ConsumeEffectOwnerForCaptureRequest(
    WholeSceneCaptureRequestKind requestKind,
    bool sourceEngineIsSub)
{
    switch (requestKind)
    {
    case WholeSceneCaptureRequestKind::CapturedLayerConsumer:
        return sourceEngineIsSub
            ? WholeSceneCaptureEffectOwner::SourceA
            : WholeSceneCaptureEffectOwner::CurrentEngine;
    case WholeSceneCaptureRequestKind::DirectFinalConsumer:
    case WholeSceneCaptureRequestKind::MainVRAMDisplayConsumer:
        return WholeSceneCaptureEffectOwner::FinalDisplay;
    case WholeSceneCaptureRequestKind::LiveOverlayProducer:
    case WholeSceneCaptureRequestKind::HandoffConsumer:
        return WholeSceneCaptureEffectOwner::CurrentEngine;
    case WholeSceneCaptureRequestKind::None:
    default:
        return WholeSceneCaptureEffectOwner::None;
    }
}

enum CaptureBackedRouteEventPublishRejectReason : u32
{
    RouteEventPublishRejectNone = 0,
    RouteEventPublishRejectInvalidSlot = 1,
    RouteEventPublishRejectPendingMissingSerial = 2,
    RouteEventPublishRejectPendingInvalidBank = 3,
    RouteEventPublishRejectPendingMissingPresentationHash = 4,
    RouteEventPublishRejectPendingMissingSource3D = 5,
    RouteEventPublishRejectMissingProduct = 6,
    RouteEventPublishRejectMissingProductTex = 7,
    RouteEventPublishRejectMissingEventProductTex = 8,
    RouteEventPublishRejectMissingEventProductFB = 9,
    RouteEventPublishRejectCaptureBankMismatch = 10,
    RouteEventPublishRejectPresentationHashMismatch = 11,
    RouteEventPublishRejectEventIdentityMismatch = 12,
};

} // namespace

bool GLRenderer2D::StoreCaptureBackedRouteProduct(const CaptureBackedRouteProductWrite& write)
{
    if (write.RouteSlot < 0 ||
        write.RouteSlot >= kCaptureBackedHandoffRouteSlots ||
        !write.SourceTex ||
        !CaptureBackedRouteGL[write.RouteSlot].ProductTex ||
        !CaptureBackedRouteGL[write.RouteSlot].ProductFB ||
        write.YStart != 0 ||
        write.YEnd != 192 ||
        !IsStorableCaptureBackedRouteProductClass(write.PresentationClass) ||
        !IsValidCaptureBackedRouteProductIdentity(write.Identity))
    {
        return false;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, WholeSceneSourceABlitFB);
    glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, write.SourceTex, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CaptureBackedRouteGL[write.RouteSlot].ProductFB);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glBlitFramebuffer(0, 0, ScreenW, ScreenH,
                      0, 0, ScreenW, ScreenH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    auto& product = CaptureBackedRoute[write.RouteSlot].Product;
    const bool sameProduct =
        product.Valid &&
        SameCaptureBackedRouteProductIdentity(product.Identity, write.Identity);
    const u32 stableFrames = sameProduct ? std::min<u32>(product.StableFrames + 1, 0xFFFFu) : 0;

    product.Valid = true;
    product.Identity = write.Identity;
    product.CapturedEventSerial = 0;
    product.StableFrames = stableFrames;
    product.PresentationClass = write.PresentationClass;
    product.HasStoredEffectState = true;
    product.StoredMasterBrightness =
        GPU2D.Num ? GPU.MasterBrightnessB : GPU.MasterBrightnessA;
    RecordSourceARouteProductTrace(product);
    TryStoreCaptureBackedRouteEventProduct(write.RouteSlot);
    return true;
}

bool GLRenderer2D::StoreRawCaptureBackedRouteProduct(int routeSlot,
                                                     GLuint sourceTex,
                                                     u64 backgroundEpochSerial,
                                                     u64 source3DSerial,
                                                     u32 source3DSceneHash,
                                                     u32 captureBank,
                                                     u32 capturePresentationHash,
                                                     u32 currentOverlayPresentationHash,
                                                     int ystart,
                                                     int yend)
{
    CaptureBackedRouteProductWrite routeWrite = {};
    routeWrite.RouteSlot = routeSlot;
    routeWrite.SourceTex = sourceTex;
    routeWrite.Identity.BackgroundEpochSerial = backgroundEpochSerial;
    routeWrite.Identity.Source3DSerial = source3DSerial;
    routeWrite.Identity.Source3DSceneHash = source3DSceneHash;
    routeWrite.Identity.CaptureBank = captureBank;
    routeWrite.Identity.CapturePresentationHash = capturePresentationHash;
    routeWrite.Identity.CurrentOverlayPresentationHash = currentOverlayPresentationHash;
    routeWrite.YStart = ystart;
    routeWrite.YEnd = yend;
    routeWrite.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
    return StoreCaptureBackedRouteProduct(routeWrite);
}

bool GLRenderer2D::StoreCurrentOverlayCaptureBackedRouteProduct(
    int routeSlot,
    GLuint sourceTex,
    u64 backgroundEpochSerial,
    u64 source3DSerial,
    u32 source3DSceneHash,
    u32 captureBank,
    u32 capturePresentationHash,
    u32 currentOverlayPresentationHash,
    int ystart,
    int yend)
{
    return StoreRawCaptureBackedRouteProduct(routeSlot,
                                             sourceTex,
                                             backgroundEpochSerial,
                                             source3DSerial,
                                             source3DSceneHash,
                                             captureBank,
                                             capturePresentationHash,
                                             currentOverlayPresentationHash,
                                             ystart,
                                             yend);
}

bool GLRenderer2D::TryStoreCaptureBackedRouteEventProduct(int slot)
{
    if (slot < 0 ||
        slot >= kCaptureBackedHandoffRouteSlots)
    {
        WholeSceneTrace.RouteEventPublishAttempted = true;
        WholeSceneTrace.RouteEventPublishSuccess = false;
        WholeSceneTrace.RouteEventPublishRejectReason = RouteEventPublishRejectInvalidSlot;
        WholeSceneTrace.RouteEventPublishSlot = slot;
        return false;
    }

    auto& pending = CaptureBackedRoute[slot].PendingEvent;
    auto& product = CaptureBackedRoute[slot].Product;
    auto record = [&](u32 rejectReason, bool success)
    {
        WholeSceneTrace.RouteEventPublishAttempted = true;
        WholeSceneTrace.RouteEventPublishSuccess = success;
        WholeSceneTrace.RouteEventPublishRejectReason = rejectReason;
        WholeSceneTrace.RouteEventPublishSlot = slot;
        WholeSceneTrace.RouteEventPublishPendingEventSerial = pending.CaptureEventSerial;
        WholeSceneTrace.RouteEventPublishPendingCaptureBank = pending.CaptureBank;
        WholeSceneTrace.RouteEventPublishPendingPresentationHash = pending.CapturePresentationHash;
        WholeSceneTrace.RouteEventPublishPendingSource3DSerial = pending.Source3DSerial;
        WholeSceneTrace.RouteEventPublishPendingSource3DSceneHash = pending.Source3DSceneHash;
        WholeSceneTrace.RouteEventPublishProductValid = product.Valid;
        WholeSceneTrace.RouteEventPublishProductTexValid =
            CaptureBackedRouteGL[slot].ProductTex != 0;
        WholeSceneTrace.RouteEventPublishEventProductTexValid =
            CaptureBackedRouteGL[slot].EventProductTex != 0;
        WholeSceneTrace.RouteEventPublishEventProductFBValid =
            CaptureBackedRouteGL[slot].EventProductFB != 0;
        WholeSceneTrace.RouteEventPublishProductBackgroundEpochSerial =
            product.Identity.BackgroundEpochSerial;
        WholeSceneTrace.RouteEventPublishProductSource3DSerial =
            product.Identity.Source3DSerial;
        WholeSceneTrace.RouteEventPublishProductSource3DSceneHash =
            product.Identity.Source3DSceneHash;
        WholeSceneTrace.RouteEventPublishProductCapturedEventSerial =
            product.CapturedEventSerial;
        WholeSceneTrace.RouteEventPublishProductCaptureBank =
            product.Identity.CaptureBank;
        WholeSceneTrace.RouteEventPublishProductCapturePresentationHash =
            product.Identity.CapturePresentationHash;
        WholeSceneTrace.RouteEventPublishProductCurrentPresentationHash =
            product.Identity.CurrentOverlayPresentationHash;
        WholeSceneTrace.RouteEventPublishProductPresentationClass =
            static_cast<u32>(product.PresentationClass);
    };

    if (!pending.Valid)
    {
        return false;
    }
    if (pending.CaptureEventSerial == 0)
    {
        record(RouteEventPublishRejectPendingMissingSerial, false);
        return false;
    }
    if (pending.CaptureBank >= 4)
    {
        record(RouteEventPublishRejectPendingInvalidBank, false);
        return false;
    }
    if (pending.CapturePresentationHash == 0)
    {
        record(RouteEventPublishRejectPendingMissingPresentationHash, false);
        return false;
    }

    if (!product.Valid)
    {
        record(RouteEventPublishRejectMissingProduct, false);
        return false;
    }
    if (!CaptureBackedRouteGL[slot].ProductTex)
    {
        record(RouteEventPublishRejectMissingProductTex, false);
        return false;
    }
    if (!CaptureBackedRouteGL[slot].EventProductTex)
    {
        record(RouteEventPublishRejectMissingEventProductTex, false);
        return false;
    }
    if (!CaptureBackedRouteGL[slot].EventProductFB)
    {
        record(RouteEventPublishRejectMissingEventProductFB, false);
        return false;
    }
    if (product.Identity.CaptureBank != pending.CaptureBank)
    {
        record(RouteEventPublishRejectCaptureBankMismatch, false);
        return false;
    }
    if (product.Identity.CurrentOverlayPresentationHash != pending.CapturePresentationHash)
    {
        record(RouteEventPublishRejectPresentationHashMismatch, false);
        return false;
    }
    if (!CaptureBackedRouteProductIdentityMatchesEvent(product.Identity,
                                                       pending.CaptureEventSerial,
                                                       pending.Source3DSerial,
                                                       pending.Source3DSceneHash))
    {
        record(RouteEventPublishRejectEventIdentityMismatch, false);
        return false;
    }

    product.CapturedEventSerial = pending.CaptureEventSerial;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, WholeSceneSourceABlitFB);
    glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureBackedRouteGL[slot].ProductTex, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CaptureBackedRouteGL[slot].EventProductFB);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glBlitFramebuffer(0, 0, ScreenW, ScreenH,
                      0, 0, ScreenW, ScreenH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    auto& eventProduct = CaptureBackedRoute[slot].EventProduct;
    eventProduct.Valid = true;
    eventProduct.Identity = product.Identity;
    eventProduct.CapturedEventSerial = pending.CaptureEventSerial;
    eventProduct.StableFrames = product.StableFrames;
    eventProduct.PresentationClass = product.PresentationClass;
    eventProduct.StoredMasterBrightness = product.StoredMasterBrightness;
    eventProduct.HasStoredEffectState = product.HasStoredEffectState;
    RecordSourceARouteProductTrace(eventProduct.Identity,
                                   eventProduct.CapturedEventSerial,
                                   eventProduct.StableFrames,
                                   eventProduct.PresentationClass);
    record(RouteEventPublishRejectNone, true);

    pending = {};
    return true;
}

void GLRenderer2D::NoteCaptureBackedRouteProductCaptured(int slot,
                                                         u64 captureEventSerial,
                                                         u32 captureBank,
                                                         u32 capturePresentationHash,
                                                         u64 source3DSerial,
                                                         u32 source3DSceneHash)
{
    u32 rejectReason = RouteEventPublishRejectNone;
    if (slot < 0 ||
        slot >= kCaptureBackedHandoffRouteSlots)
    {
        rejectReason = RouteEventPublishRejectInvalidSlot;
    }
    else if (captureEventSerial == 0)
    {
        rejectReason = RouteEventPublishRejectPendingMissingSerial;
    }
    else if (captureBank >= 4)
    {
        rejectReason = RouteEventPublishRejectPendingInvalidBank;
    }
    else if (capturePresentationHash == 0)
    {
        rejectReason = RouteEventPublishRejectPendingMissingPresentationHash;
    }
    else if (source3DSerial == 0 ||
             source3DSceneHash == 0)
    {
        rejectReason = RouteEventPublishRejectPendingMissingSource3D;
    }

    if (rejectReason != RouteEventPublishRejectNone)
    {
        WholeSceneTrace.RouteEventPublishAttempted = true;
        WholeSceneTrace.RouteEventPublishSuccess = false;
        WholeSceneTrace.RouteEventPublishRejectReason = rejectReason;
        WholeSceneTrace.RouteEventPublishSlot = slot;
        WholeSceneTrace.RouteEventPublishPendingEventSerial = captureEventSerial;
        WholeSceneTrace.RouteEventPublishPendingCaptureBank = captureBank;
        WholeSceneTrace.RouteEventPublishPendingPresentationHash = capturePresentationHash;
        WholeSceneTrace.RouteEventPublishPendingSource3DSerial = source3DSerial;
        WholeSceneTrace.RouteEventPublishPendingSource3DSceneHash = source3DSceneHash;
        if (slot >= 0 &&
            slot < kCaptureBackedHandoffRouteSlots)
        {
            const auto& product = CaptureBackedRoute[slot].Product;
            WholeSceneTrace.RouteEventPublishProductValid = product.Valid;
            WholeSceneTrace.RouteEventPublishProductTexValid =
                CaptureBackedRouteGL[slot].ProductTex != 0;
            WholeSceneTrace.RouteEventPublishEventProductTexValid =
                CaptureBackedRouteGL[slot].EventProductTex != 0;
            WholeSceneTrace.RouteEventPublishEventProductFBValid =
                CaptureBackedRouteGL[slot].EventProductFB != 0;
            WholeSceneTrace.RouteEventPublishProductBackgroundEpochSerial =
                product.Identity.BackgroundEpochSerial;
            WholeSceneTrace.RouteEventPublishProductSource3DSerial =
                product.Identity.Source3DSerial;
            WholeSceneTrace.RouteEventPublishProductSource3DSceneHash =
                product.Identity.Source3DSceneHash;
            WholeSceneTrace.RouteEventPublishProductCapturedEventSerial =
                product.CapturedEventSerial;
            WholeSceneTrace.RouteEventPublishProductCaptureBank =
                product.Identity.CaptureBank;
            WholeSceneTrace.RouteEventPublishProductCapturePresentationHash =
                product.Identity.CapturePresentationHash;
            WholeSceneTrace.RouteEventPublishProductCurrentPresentationHash =
                product.Identity.CurrentOverlayPresentationHash;
            WholeSceneTrace.RouteEventPublishProductPresentationClass =
                static_cast<u32>(product.PresentationClass);
        }
        return;
    }

    auto& pending = CaptureBackedRoute[slot].PendingEvent;
    pending.Valid = true;
    pending.CaptureEventSerial = captureEventSerial;
    pending.CaptureBank = captureBank;
    pending.CapturePresentationHash = capturePresentationHash;
    pending.Source3DSerial = source3DSerial;
    pending.Source3DSceneHash = source3DSceneHash;
    TryStoreCaptureBackedRouteEventProduct(slot);
}

GLRenderer2D::CaptureBackedRouteProductLookup GLRenderer2D::FindCaptureBackedRouteProductForEvent(
    const CaptureBackedRouteProductEventQuery& query) const
{
    CaptureBackedRouteProductLookup lookup = {};

    if (!IsCaptureBackedRouteProductEventQueryUsable(
            query,
            kCaptureBackedHandoffRouteSlots,
            query.RouteSlot >= 0 &&
                query.RouteSlot < kCaptureBackedHandoffRouteSlots &&
                CaptureBackedRouteGL[query.RouteSlot].ProductTex != 0))
    {
        return lookup;
    }

    const auto& eventProduct = CaptureBackedRoute[query.RouteSlot].EventProduct;
    if (DoesCaptureBackedRouteEventProductMatchQuery(eventProduct, query) &&
        CaptureBackedRouteGL[query.RouteSlot].EventProductTex)
    {
        lookup.Tex = CaptureBackedRouteGL[query.RouteSlot].EventProductTex;
        lookup.Valid = true;
        lookup.Source = CaptureBackedRouteProductLookupSource::ExactEventProduct;
        lookup.Identity = eventProduct.Identity;
        lookup.CapturedEventSerial = eventProduct.CapturedEventSerial;
        lookup.StableFrames = eventProduct.StableFrames;
        lookup.PresentationClass = eventProduct.PresentationClass;
        lookup.StoredMasterBrightness = eventProduct.StoredMasterBrightness;
        lookup.HasStoredEffectState = eventProduct.HasStoredEffectState;
        return lookup;
    }

    const auto& product = CaptureBackedRoute[query.RouteSlot].Product;
    if (!DoesCaptureBackedRouteProductMatchEventQuery(product, query))
    {
        return lookup;
    }

    lookup.Tex = CaptureBackedRouteGL[query.RouteSlot].ProductTex;
    lookup.Valid = true;
    lookup.Source = CaptureBackedRouteProductLookupSource::ExactEventRouteProduct;
    lookup.Identity = product.Identity;
    lookup.CapturedEventSerial = product.CapturedEventSerial;
    lookup.StableFrames = product.StableFrames;
    lookup.PresentationClass = product.PresentationClass;
    lookup.StoredMasterBrightness = product.StoredMasterBrightness;
    lookup.HasStoredEffectState = product.HasStoredEffectState;
    return lookup;
}

GLRenderer2D::CaptureBackedRouteProductLookup GLRenderer2D::FindCaptureBackedRouteProductForSource3DScene(
    const CaptureBackedRouteProductEventQuery& query) const
{
    CaptureBackedRouteProductLookup lookup = {};

    if (!IsCaptureBackedRouteProductEventQueryUsable(
            query,
            kCaptureBackedHandoffRouteSlots,
            query.RouteSlot >= 0 &&
                query.RouteSlot < kCaptureBackedHandoffRouteSlots &&
                CaptureBackedRouteGL[query.RouteSlot].ProductTex != 0))
    {
        return lookup;
    }

    const auto& product = CaptureBackedRoute[query.RouteSlot].Product;
    if (!DoesCaptureBackedRouteProductMatchSource3DSceneQuery(product, query))
    {
        return lookup;
    }

    lookup.Tex = CaptureBackedRouteGL[query.RouteSlot].ProductTex;
    lookup.Valid = true;
    lookup.Source = CaptureBackedRouteProductLookupSource::Source3DSceneProduct;
    lookup.Identity = product.Identity;
    lookup.CapturedEventSerial = product.CapturedEventSerial;
    lookup.StableFrames = product.StableFrames;
    lookup.PresentationClass = product.PresentationClass;
    lookup.StoredMasterBrightness = product.StoredMasterBrightness;
    lookup.HasStoredEffectState = product.HasStoredEffectState;
    return lookup;
}

GLRenderer2D::CaptureBackedRouteProductLookup GLRenderer2D::FindCaptureBackedRouteProductForState(
    const CaptureBackedRouteProductStateQuery& query) const
{
    CaptureBackedRouteProductLookup lookup = {};

    if (query.RouteSlot < 0 ||
        query.RouteSlot >= kCaptureBackedHandoffRouteSlots ||
        query.YStart != 0 ||
        query.YEnd != 192 ||
        query.Identity.BackgroundEpochSerial == 0 ||
        !IsValidCaptureBackedRouteProductIdentity(query.Identity) ||
        !CaptureBackedRouteGL[query.RouteSlot].ProductTex)
    {
        return lookup;
    }

    const auto& product = CaptureBackedRoute[query.RouteSlot].Product;
    if (!product.Valid ||
        !SameCaptureBackedRouteProductIdentity(product.Identity, query.Identity))
    {
        return lookup;
    }

    lookup.Tex = CaptureBackedRouteGL[query.RouteSlot].ProductTex;
    lookup.Valid = true;
    lookup.Source = CaptureBackedRouteProductLookupSource::RouteStateProduct;
    lookup.Identity = product.Identity;
    lookup.CapturedEventSerial = product.CapturedEventSerial;
    lookup.StableFrames = product.StableFrames;
    lookup.PresentationClass = product.PresentationClass;
    lookup.StoredMasterBrightness = product.StoredMasterBrightness;
    lookup.HasStoredEffectState = product.HasStoredEffectState;
    return lookup;
}

void GLRenderer2D::ResolveSourceARouteProductChoice(
    SourceACaptureReplacementChoice& choice,
    u64 captureEventSerial,
    u64 source3DSerial,
    u32 source3DSceneHash,
    int ystart,
    int yend) const
{
    if (choice.MainRenderer &&
        choice.CapturePresentationHash)
    {
        choice.RouteProductLookupAttempted = true;
        choice.RouteProductLookupSlot = choice.RouteSlot;
        choice.RouteProductLookupEventSerial = captureEventSerial;
        choice.RouteProductLookupCaptureBank = static_cast<u32>(choice.CaptureBank);
        choice.RouteProductLookupCapturePresentationHash = choice.CapturePresentationHash;
        choice.RouteProductLookupSource3DSerial = source3DSerial;
        choice.RouteProductLookupSource3DSceneHash = source3DSceneHash;
        if (choice.RouteSlot >= 0 &&
            choice.RouteSlot < kCaptureBackedHandoffRouteSlots)
        {
            const auto& route = choice.MainRenderer->CaptureBackedRoute[choice.RouteSlot];
            choice.RouteProductLookupEventProductValid = route.EventProduct.Valid;
            choice.RouteProductLookupEventProductCapturedSerial =
                route.EventProduct.CapturedEventSerial;
            choice.RouteProductLookupEventProductCaptureBank =
                route.EventProduct.Identity.CaptureBank;
            choice.RouteProductLookupEventProductCurrentPresentationHash =
                route.EventProduct.Identity.CurrentOverlayPresentationHash;
            choice.RouteProductLookupEventProductSource3DSerial =
                route.EventProduct.Identity.Source3DSerial;
            choice.RouteProductLookupEventProductSource3DSceneHash =
                route.EventProduct.Identity.Source3DSceneHash;
            choice.RouteProductLookupProductValid = route.Product.Valid;
            choice.RouteProductLookupProductCapturedSerial =
                route.Product.CapturedEventSerial;
            choice.RouteProductLookupProductCaptureBank =
                route.Product.Identity.CaptureBank;
            choice.RouteProductLookupProductCurrentPresentationHash =
                route.Product.Identity.CurrentOverlayPresentationHash;
            choice.RouteProductLookupProductSource3DSerial =
                route.Product.Identity.Source3DSerial;
            choice.RouteProductLookupProductSource3DSceneHash =
                route.Product.Identity.Source3DSceneHash;
        }

        const CaptureBackedRouteProductEventQuery routeQuery =
            MakeCaptureBackedRouteProductEventQuery(choice.RouteSlot,
                                                    captureEventSerial,
                                                    source3DSerial,
                                                    source3DSceneHash,
                                                    static_cast<u32>(choice.CaptureBank),
                                                    choice.CapturePresentationHash,
                                                    ystart,
                                                    yend);

        const CaptureBackedRouteProductLookup routeProduct =
            choice.MainRenderer->FindCaptureBackedRouteProductForEvent(routeQuery);
        choice.MainRenderer->ApplyRouteProductLookupToSourceAChoice(choice, routeProduct);
        choice.RouteProductLookupSuccess = routeProduct.Valid;
        choice.RouteProductLookupResultSource = static_cast<u32>(routeProduct.Source);

        if (!choice.RouteProductTex)
        {
            const CaptureBackedRouteProductLookup sceneProduct =
                choice.MainRenderer->FindCaptureBackedRouteProductForSource3DScene(routeQuery);
            choice.MainRenderer->ApplyRouteProductLookupToSourceAChoice(choice, sceneProduct);
            if (sceneProduct.Valid)
            {
                choice.RouteProductLookupSuccess = true;
                choice.RouteProductLookupResultSource =
                    static_cast<u32>(sceneProduct.Source);
            }
        }
    }
    if (!choice.RouteProductTex &&
        choice.MainRenderer &&
        choice.BackgroundEpochSerial &&
        choice.CapturePresentationHash)
    {
        const CaptureBackedRouteProductStateQuery routeQuery =
            MakeCaptureBackedRouteProductStateQuery(choice.RouteSlot,
                                                    choice.BackgroundEpochSerial,
                                                    choice.BackgroundSource3DSerial,
                                                    choice.BackgroundSource3DSceneHash,
                                                    static_cast<u32>(choice.CaptureBank),
                                                    choice.CapturePresentationHash,
                                                    choice.CapturePresentationHash,
                                                    ystart,
                                                    yend);

        const CaptureBackedRouteProductLookup routeProduct =
            choice.MainRenderer->FindCaptureBackedRouteProductForState(routeQuery);
        choice.MainRenderer->ApplyRouteProductLookupToSourceAChoice(choice, routeProduct);
    }
    if (!choice.RouteProductTex &&
        choice.MainRenderer &&
        choice.BackgroundEpochSerial &&
        choice.CapturePresentationHash &&
        choice.CurrentPresentationHash)
    {
        const CaptureBackedRouteProductStateQuery routeQuery =
            MakeCaptureBackedRouteProductStateQuery(choice.RouteSlot,
                                                    choice.BackgroundEpochSerial,
                                                    choice.BackgroundSource3DSerial,
                                                    choice.BackgroundSource3DSceneHash,
                                                    static_cast<u32>(choice.CaptureBank),
                                                    choice.CapturePresentationHash,
                                                    choice.CurrentPresentationHash,
                                                    ystart,
                                                    yend);

        const CaptureBackedRouteProductLookup routeProduct =
            choice.MainRenderer->FindCaptureBackedRouteProductForState(routeQuery);
        choice.MainRenderer->ApplyRouteProductLookupToSourceAChoice(choice, routeProduct);
    }
}

GLRenderer2D::CaptureBackedRouteProductLookup GLRenderer2D::ResolveHandoffExactRouteProduct(
    int routeSlot,
    u64 captureEventSerial,
    u64 eventSource3DSerial,
    u32 eventSource3DSceneHash,
    u32 captureBank,
    u64 backgroundEpochSerial,
    u64 backgroundSource3DSerial,
    u32 backgroundSource3DSceneHash,
    u32 capturePresentationHash,
    u32 currentPresentationHash,
    int ystart,
    int yend) const
{
    const CaptureBackedRouteProductEventQuery eventQuery =
        MakeCaptureBackedRouteProductEventQuery(routeSlot,
                                                captureEventSerial,
                                                eventSource3DSerial,
                                                eventSource3DSceneHash,
                                                captureBank,
                                                capturePresentationHash,
                                                ystart,
                                                yend);

    CaptureBackedRouteProductLookup routeProduct =
        FindCaptureBackedRouteProductForEvent(eventQuery);
    if (routeProduct.Tex)
        return routeProduct;

    routeProduct = FindCaptureBackedRouteProductForSource3DScene(eventQuery);
    if (routeProduct.Tex)
        return routeProduct;

    const CaptureBackedRouteProductStateQuery stateQuery =
        MakeCaptureBackedRouteProductStateQuery(routeSlot,
                                                backgroundEpochSerial,
                                                backgroundSource3DSerial,
                                                backgroundSource3DSceneHash,
                                                captureBank,
                                                capturePresentationHash,
                                                currentPresentationHash,
                                                ystart,
                                                yend);

    routeProduct = FindCaptureBackedRouteProductForState(stateQuery);
    return routeProduct;
}

GLRenderer2D::VisibleOBJCaptureDebug GLRenderer2D::BuildVisibleOBJCaptureDebug() const
{
    VisibleOBJCaptureDebug debug = {};
    debug.Bank = -1;

    constexpr u32 objMask = 1u << 4;
    const bool objVisible = (LayerEnable & objMask) && OBJEnable && NumSprites > 0;
    if (!objVisible)
    {
        debug.RejectReason = 1;
        return debug;
    }

    if ((LayerEnable & 0x0Fu) != 0)
        debug.RejectReason = 2;

    int minX = 256;
    int minY = 192;
    int maxX = 0;
    int maxY = 0;
    int sourceAOnlyCount = 0;
    int fullSourceACount = 0;

    for (int i = 0; i < NumSprites; i++)
    {
        const auto& sprite = SpriteConfig.uOAM[i];
        int captureBank = -1;
        if (sprite.Type == 3)
        {
            captureBank = static_cast<int>((sprite.TileStride >> 2) & 0x3);
            debug.Type3Count++;
        }
        else if (sprite.Type == 4)
        {
            captureBank = static_cast<int>(sprite.TileStride & 0x3);
            debug.Type4Count++;
        }
        else
        {
            const int x0 = std::max(0, sprite.Position[0]);
            const int y0 = std::max(0, sprite.Position[1]);
            const int x1 = std::min(256, sprite.Position[0] + sprite.BoundSize[0]);
            const int y1 = std::min(192, sprite.Position[1] + sprite.BoundSize[1]);
            if (x1 > x0 && y1 > y0)
                debug.NonCaptureSpriteCount++;
            continue;
        }

        debug.Found = true;
        debug.CaptureSpriteCount++;
        if (debug.Bank < 0)
            debug.Bank = captureBank;
        else if (debug.Bank != captureBank)
            debug.MixedBank = true;

        if (Parent.IsCurrentSourceAOnlyFullDisplayCaptureOBJ(sprite.Type, sprite.TileStride))
            sourceAOnlyCount++;
        if (captureBank >= 0 &&
            Parent.IsCurrentFullDisplayCaptureFromSourceABlock(static_cast<u32>(captureBank)))
        {
            fullSourceACount++;
        }

        const int x0 = std::max(0, sprite.Position[0]);
        const int y0 = std::max(0, sprite.Position[1]);
        const int x1 = std::min(256, sprite.Position[0] + sprite.BoundSize[0]);
        const int y1 = std::min(192, sprite.Position[1] + sprite.BoundSize[1]);
        if (x1 <= x0 || y1 <= y0)
            continue;

        minX = std::min(minX, x0);
        minY = std::min(minY, y0);
        maxX = std::max(maxX, x1);
        maxY = std::max(maxY, y1);
        debug.CoverageArea += (x1 - x0) * (y1 - y0);
    }

    if (!debug.Found)
    {
        if (debug.RejectReason == 0)
            debug.RejectReason = 3;
        return debug;
    }

    debug.MinX = minX == 256 ? 0 : minX;
    debug.MinY = minY == 192 ? 0 : minY;
    debug.MaxX = maxX;
    debug.MaxY = maxY;

    const int boundsWidth = maxX - minX;
    const int boundsHeight = maxY - minY;
    constexpr int screenArea = 256 * 192;
    constexpr int stripArea = 256 * 128;
    debug.FullScreen =
        (boundsWidth >= 240 && boundsHeight >= 176) ||
        (debug.CoverageArea >= (screenArea * 3) / 4);
    debug.FullWidthTopStrip =
        minX <= 0 &&
        minY <= 0 &&
        maxX >= 256 &&
        boundsHeight >= 128 &&
        debug.CoverageArea >= stripArea;
    debug.CurrentSourceAOnly = sourceAOnlyCount == debug.CaptureSpriteCount;
    debug.CurrentFullSourceA = fullSourceACount == debug.CaptureSpriteCount;

    if (debug.Bank >= 0 && !debug.MixedBank)
    {
        const auto& event = Parent.HighResDisplayCapture256Event[debug.Bank];
        debug.EventSerial = event.Serial;
        debug.EventSourceOBJ = event.SourceOBJVisible;
        debug.EventProductMask = event.ProductMask;
        debug.EventRejectReason = static_cast<u32>(event.RejectReason);
        debug.EventValid =
            Parent.IsFullDisplayHighResCaptureEventRecord(event, static_cast<u32>(debug.Bank));
        debug.ProductAvailable =
            debug.EventValid &&
            (event.ProductMask & GLRenderer::HighResCaptureProductFullEquivalent) &&
            Parent.HighResDisplayCaptureFullTex[debug.Bank] != 0;
    }

    if (debug.RejectReason != 0)
        return debug;
    if (debug.MixedBank)
        debug.RejectReason = 4;
    else if (debug.NonCaptureSpriteCount != 0)
        debug.RejectReason = 10;
    else if (!debug.FullScreen)
        debug.RejectReason = 5;
    else if (!debug.CurrentSourceAOnly)
        debug.RejectReason = 6;
    else if (!debug.EventValid)
        debug.RejectReason = 7;
    else if ((debug.EventProductMask & GLRenderer::HighResCaptureProductFullEquivalent) == 0)
        debug.RejectReason = 8;
    else if (!Parent.HighResDisplayCaptureFullTex[debug.Bank])
        debug.RejectReason = 9;

    return debug;
}

GLRenderer2D::WholeSceneCaptureRequest GLRenderer2D::MakeSourceAConsumerCaptureRequest(
    const SourceACaptureReplacementChoice& choice,
    WholeSceneCaptureRequestKind kind,
    int ystart,
    int yend,
    u64 captureEventSerial)
{
    if (kind == WholeSceneCaptureRequestKind::DirectFinalConsumer)
    {
        return ::melonDS::MakeDirectFinalConsumerCaptureRequest(
            ystart,
            yend,
            choice.RouteSlot,
            static_cast<u32>(choice.CaptureBank),
            captureEventSerial,
            choice.BackgroundEpochSerial,
            choice.CapturePresentationHash,
            choice.CurrentPresentationHash,
            choice.DirectFinalBottomConsumer);
    }

    return ::melonDS::MakeCapturedLayerConsumerCaptureRequest(
        ystart,
        yend,
        choice.RouteSlot,
        static_cast<u32>(choice.CaptureBank),
        captureEventSerial,
        choice.BackgroundEpochSerial,
        choice.CapturePresentationHash,
        choice.CurrentPresentationHash,
        choice.DirectFinalBottomConsumer);
}

GLRenderer2D::SourceACaptureResolution GLRenderer2D::MakeSourceARouteProductResolution(
    const SourceACaptureReplacementChoice& choice,
    int ystart,
    int yend)
{
    SourceACaptureResolution resolution = {};
    resolution.Request = MakeSourceAConsumerCaptureRequest(
        choice,
        choice.DirectFinalBottomConsumer
            ? WholeSceneCaptureRequestKind::DirectFinalConsumer
            : WholeSceneCaptureRequestKind::CapturedLayerConsumer,
        ystart,
        yend,
        choice.RouteProductCapturedEventSerial);
    resolution.Result = ::melonDS::MakeRouteProductCapturePolicyResult(
        choice.RouteProductKind != WholeSceneCaptureProductKind::None
            ? choice.RouteProductKind
            : WholeSceneCaptureProductKind::RouteProduct,
        choice.RouteProductProof != WholeSceneCaptureProofKind::None
            ? choice.RouteProductProof
            : WholeSceneCaptureProofKind::RouteStateIdentity,
        WholeSceneCaptureAuthority::SourceABackgroundCurrentOverlay);
    return resolution;
}

GLRenderer2D::SourceACaptureResolution GLRenderer2D::MakeSourceABackgroundOverlayResolution(
    const SourceACaptureReplacementChoice& choice,
    int ystart,
    int yend)
{
    SourceACaptureResolution resolution = {};
    resolution.Request = MakeSourceAConsumerCaptureRequest(
        choice,
        choice.DirectFinalBottomConsumer
            ? WholeSceneCaptureRequestKind::DirectFinalConsumer
            : WholeSceneCaptureRequestKind::CapturedLayerConsumer,
        ystart,
        yend);
    resolution.Result = ::melonDS::MakeBackgroundCapturePolicyResult(
        SourceABackgroundSource::CaptureEventBackgroundTex,
        WholeSceneCaptureRenderAction::CompositeCurrentOverlay,
        WholeSceneCaptureAuthority::SourceABackgroundCurrentOverlay,
        WholeSceneCaptureProofKind::CurrentOverlayEligibility);
    return resolution;
}

GLRenderer2D::SourceACaptureResolution GLRenderer2D::MakeSourceARejectedResolution(
    const SourceACaptureReplacementChoice& choice,
    int ystart,
    int yend)
{
    SourceACaptureResolution resolution = {};
    resolution.Request = MakeSourceAConsumerCaptureRequest(
        choice,
        WholeSceneCaptureRequestKind::CapturedLayerConsumer,
        ystart,
        yend);
    resolution.Result = ::melonDS::MakeRejectedCapturePolicyResult(
        WholeSceneCaptureRenderAction::RenderNormalHybridFallback);
    return resolution;
}

GLRenderer2D::SourceACaptureResolution GLRenderer2D::MakeSourceAFullProductResolution(
    const SourceACaptureReplacementChoice& choice,
    int ystart,
    int yend)
{
    SourceACaptureResolution resolution = {};
    resolution.Request = MakeSourceAConsumerCaptureRequest(
        choice,
        choice.DirectFinalBottomConsumer
            ? WholeSceneCaptureRequestKind::DirectFinalConsumer
            : WholeSceneCaptureRequestKind::CapturedLayerConsumer,
        ystart,
        yend,
        choice.FullProductEventSerial);
    resolution.Result = ::melonDS::MakeFullProductCapturePolicyResult(
        WholeSceneCaptureAuthority::SourceAFullProduct,
        choice.DirectFinalBottomConsumer
            ? WholeSceneCaptureProofKind::DirectFinalPresentationMatch
            : WholeSceneCaptureProofKind::ExactCaptureEvent);
    if (choice.AllowExactFullProductCapturePresentation)
        resolution.Request.CurrentPresentationHash = choice.CapturePresentationHash;
    return resolution;
}

GLRenderer2D::HandoffCaptureResolution GLRenderer2D::MakeHandoffRouteProductResolution(
    const WholeSceneCaptureRequest& baseRequest,
    const CaptureBackedRouteProductLookup& routeProduct,
    u32 captureBank,
    u64 captureEventSerial,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash)
{
    HandoffCaptureResolution resolution = {};
    resolution.Request = ::melonDS::MakeHandoffConsumerCaptureRequest(
        baseRequest,
        captureBank,
        captureEventSerial,
        backgroundEpochSerial,
        capturePresentationHash,
        currentPresentationHash);
    resolution.Result = ::melonDS::MakeRouteProductCapturePolicyResult(
        routeProduct.Source,
        WholeSceneCaptureAuthority::CaptureEventBackground);
    return resolution;
}

GLRenderer2D::HandoffCaptureResolution GLRenderer2D::MakeHandoffFullProductResolution(
    const WholeSceneCaptureRequest& baseRequest,
    u32 captureBank,
    u64 captureEventSerial,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash)
{
    HandoffCaptureResolution resolution = {};
    resolution.Request = ::melonDS::MakeHandoffConsumerCaptureRequest(
        baseRequest,
        captureBank,
        captureEventSerial,
        backgroundEpochSerial,
        capturePresentationHash,
        currentPresentationHash);
    resolution.Result = ::melonDS::MakeFullProductCapturePolicyResult(
        WholeSceneCaptureAuthority::CaptureEventFullProduct);
    return resolution;
}

GLRenderer2D::HandoffCaptureResolution GLRenderer2D::MakeHandoffBackgroundOverlayResolution(
    const WholeSceneCaptureRequest& baseRequest,
    SourceABackgroundSource backgroundSource,
    WholeSceneCaptureAuthority authority,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash)
{
    HandoffCaptureResolution resolution = {};
    resolution.Request = ::melonDS::MakeHandoffConsumerCaptureRequest(
        baseRequest,
        0xFFFFFFFFu,
        0,
        backgroundEpochSerial,
        capturePresentationHash,
        currentPresentationHash);
    resolution.Result = ::melonDS::MakeBackgroundCapturePolicyResult(
        backgroundSource,
        WholeSceneCaptureRenderAction::CompositeCurrentOverlay,
        authority);
    return resolution;
}

GLRenderer2D::HandoffCaptureResolution GLRenderer2D::MakeHandoffHybridResolution(
    const WholeSceneCaptureRequest& baseRequest,
    SourceABackgroundSource backgroundSource,
    WholeSceneCaptureAuthority authority,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    bool hasHighResBackground)
{
    HandoffCaptureResolution resolution = {};
    resolution.Request = ::melonDS::MakeHandoffConsumerCaptureRequest(baseRequest,
                                                                      0xFFFFFFFFu,
                                                                      0,
                                                                      backgroundEpochSerial,
                                                                      capturePresentationHash);
    resolution.Result = ::melonDS::MakeBackgroundCapturePolicyResult(
        backgroundSource,
        WholeSceneCaptureRenderAction::RenderHandoffHybrid,
        authority,
        hasHighResBackground
            ? WholeSceneCaptureProofKind::None
            : WholeSceneCaptureProofKind::ActiveBackgroundEpoch);
    return resolution;
}

void GLRenderer2D::RecordWholeSceneCaptureResolution(const WholeSceneCaptureRequest& request,
                                                     WholeSceneCapturePolicyResult result)
{
    ::melonDS::AttachCaptureRequestIdentityToProductRef(result, request);
    WholeSceneTrace.CaptureAuthority = result.Authority;
    RecordWholeSceneCaptureSemantics(request.Role,
                                     request.Kind,
                                     result.ProductRef.Kind,
                                     result.ProofKind,
                                     result.RenderAction);
}

void GLRenderer2D::RecordSourceACaptureResolution(const SourceACaptureResolution& resolution)
{
    RecordWholeSceneCaptureResolution(resolution.Request, resolution.Result);
}

void GLRenderer2D::RecordHandoffCaptureResolution(const HandoffCaptureResolution& resolution)
{
    RecordWholeSceneCaptureResolution(resolution.Request, resolution.Result);
}

void GLRenderer2D::RecordSourceACaptureChoiceDebug(const SourceACaptureReplacementChoice& choice)
{
    WholeSceneTrace.SourceAFullProductCaptureBank = choice.FullProductCaptureBank;
    WholeSceneTrace.SourceAFullProductTex = choice.FullProductTexID;
    WholeSceneTrace.SourceAFullProductEventValid = choice.FullProductEventValid;
    WholeSceneTrace.SourceAFullProductEventSerial = choice.FullProductEventSerial;
    WholeSceneTrace.SourceAFullProductEventSource3DSerial = choice.FullProductEventSource3DSerial;
    WholeSceneTrace.SourceAFullProductEventSource3DSceneHash =
        choice.FullProductEventSource3DSceneHash;
    WholeSceneTrace.SourceAFullProductEventSourcePresentationHash =
        choice.FullProductEventSourcePresentationHash;
    WholeSceneTrace.SourceAFullProductEventSourceKind = choice.FullProductEventSourceKind;
    WholeSceneTrace.SourceAFullProductEventProductMask = choice.FullProductEventProductMask;
    WholeSceneTrace.SourceAFullProductEventRejectReason = choice.FullProductEventRejectReason;
    WholeSceneTrace.SourceAFullProductEventDstBlock = choice.FullProductEventDstBlock;
    WholeSceneTrace.SourceAFullProductEventDstOffset = choice.FullProductEventDstOffset;
    WholeSceneTrace.SourceAFullProductEventSourceOBJ = choice.FullProductEventSourceOBJ;
    WholeSceneTrace.SourceAFullProductEventScreenSwap = choice.FullProductEventScreenSwap;
    WholeSceneTrace.SourceAFullProductEventMainFinalBottom =
        choice.FullProductEventMainFinalBottom;
    WholeSceneTrace.SourceAPreferExactRouteProduct =
        choice.PreferExactRouteProduct;
    WholeSceneTrace.RouteProductLookupAttempted = choice.RouteProductLookupAttempted;
    WholeSceneTrace.RouteProductLookupSuccess = choice.RouteProductLookupSuccess;
    WholeSceneTrace.RouteProductLookupResultSource = choice.RouteProductLookupResultSource;
    WholeSceneTrace.RouteProductLookupSlot = choice.RouteProductLookupSlot;
    WholeSceneTrace.RouteProductLookupEventSerial = choice.RouteProductLookupEventSerial;
    WholeSceneTrace.RouteProductLookupCaptureBank = choice.RouteProductLookupCaptureBank;
    WholeSceneTrace.RouteProductLookupCapturePresentationHash =
        choice.RouteProductLookupCapturePresentationHash;
    WholeSceneTrace.RouteProductLookupSource3DSerial =
        choice.RouteProductLookupSource3DSerial;
    WholeSceneTrace.RouteProductLookupSource3DSceneHash =
        choice.RouteProductLookupSource3DSceneHash;
    WholeSceneTrace.RouteProductLookupEventProductValid =
        choice.RouteProductLookupEventProductValid;
    WholeSceneTrace.RouteProductLookupEventProductCapturedSerial =
        choice.RouteProductLookupEventProductCapturedSerial;
    WholeSceneTrace.RouteProductLookupEventProductCaptureBank =
        choice.RouteProductLookupEventProductCaptureBank;
    WholeSceneTrace.RouteProductLookupEventProductCurrentPresentationHash =
        choice.RouteProductLookupEventProductCurrentPresentationHash;
    WholeSceneTrace.RouteProductLookupEventProductSource3DSerial =
        choice.RouteProductLookupEventProductSource3DSerial;
    WholeSceneTrace.RouteProductLookupEventProductSource3DSceneHash =
        choice.RouteProductLookupEventProductSource3DSceneHash;
    WholeSceneTrace.RouteProductLookupProductValid = choice.RouteProductLookupProductValid;
    WholeSceneTrace.RouteProductLookupProductCapturedSerial =
        choice.RouteProductLookupProductCapturedSerial;
    WholeSceneTrace.RouteProductLookupProductCaptureBank =
        choice.RouteProductLookupProductCaptureBank;
    WholeSceneTrace.RouteProductLookupProductCurrentPresentationHash =
        choice.RouteProductLookupProductCurrentPresentationHash;
    WholeSceneTrace.RouteProductLookupProductSource3DSerial =
        choice.RouteProductLookupProductSource3DSerial;
    WholeSceneTrace.RouteProductLookupProductSource3DSceneHash =
        choice.RouteProductLookupProductSource3DSceneHash;
    WholeSceneTrace.DirectFinalDisplayConsumer = choice.DirectFinalDisplayConsumer;
    WholeSceneTrace.DirectFinalBottomConsumer = choice.DirectFinalBottomConsumer;
    WholeSceneTrace.ActiveDisplayCaptureSourceA2D = choice.ActiveDisplayCaptureSourceA2D;
    WholeSceneTrace.ActiveFullDisplayCaptureSourceA = choice.ActiveFullDisplayCaptureSourceA;
    WholeSceneTrace.ActiveDisplayCaptureDstBank = choice.ActiveDisplayCaptureDstBank;
    WholeSceneTrace.ActiveDisplayCaptureDstOffset = choice.ActiveDisplayCaptureDstOffset;
}

void GLRenderer2D::RecordSourceABackgroundTrace(
    u64 requestBackgroundEpochSerial,
    SourceABackgroundSource effectiveBackgroundSource,
    u64 effectiveBackgroundEpochSerial,
    u32 capturePresentationHash)
{
    WholeSceneTrace.SourceABackgroundEpochSerial = requestBackgroundEpochSerial;
    WholeSceneTrace.EffectiveSourceABackgroundSource = effectiveBackgroundSource;
    WholeSceneTrace.EffectiveSourceABackgroundEpochSerial = effectiveBackgroundEpochSerial;
    WholeSceneTrace.SourceACapturePresentationHash = capturePresentationHash;
}

void GLRenderer2D::RecordSourceACaptureProductTrace(
    SourceACaptureReplacementMode mode,
    u64 requestBackgroundEpochSerial,
    SourceABackgroundSource effectiveBackgroundSource,
    u64 effectiveBackgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash,
    SourceAProductChoiceReason productChoice,
    bool fullProductKeyMatch)
{
    WholeSceneTrace.SourceACaptureMode = mode;
    RecordSourceABackgroundTrace(requestBackgroundEpochSerial,
                                 effectiveBackgroundSource,
                                 effectiveBackgroundEpochSerial,
                                 capturePresentationHash);
    WholeSceneTrace.SourceACurrentPresentationHash = currentPresentationHash;
    WholeSceneTrace.SourceAFullProductKeyMatch = fullProductKeyMatch;
    WholeSceneTrace.SourceAProductChoice = productChoice;
}

void GLRenderer2D::RecordSourceARouteProductTrace(const CaptureBackedRouteProductIdentity& identity,
                                                  u64 capturedEventSerial,
                                                  u32 stableFrames,
                                                  WholeSceneCaptureProductPresentationClass presentationClass)
{
    WholeSceneTrace.SourceARouteProductBackgroundEpochSerial = identity.BackgroundEpochSerial;
    WholeSceneTrace.SourceARouteProductSource3DSerial = identity.Source3DSerial;
    WholeSceneTrace.SourceARouteProductSource3DSceneHash = identity.Source3DSceneHash;
    WholeSceneTrace.SourceARouteProductCapturedEventSerial = capturedEventSerial;
    WholeSceneTrace.SourceARouteProductCapturePresentationHash =
        identity.CapturePresentationHash;
    WholeSceneTrace.SourceARouteProductCurrentPresentationHash =
        identity.CurrentOverlayPresentationHash;
    WholeSceneTrace.SourceARouteProductStableFrames = stableFrames;
    WholeSceneTrace.SourceARouteProductPresentationClass = static_cast<u32>(presentationClass);
}

void GLRenderer2D::RecordSourceARouteProductTrace(const CaptureBackedRouteProductState& product)
{
    if (!product.Valid)
        return;

    RecordSourceARouteProductTrace(product.Identity,
                                   product.CapturedEventSerial,
                                   product.StableFrames,
                                   product.PresentationClass);
}

void GLRenderer2D::RecordSourceARouteProductTrace(const SourceACaptureReplacementChoice& choice)
{
    WholeSceneTrace.SourceARouteProductBackgroundEpochSerial = choice.RouteProductBackgroundEpochSerial;
    WholeSceneTrace.SourceARouteProductSource3DSerial = choice.RouteProductSource3DSerial;
    WholeSceneTrace.SourceARouteProductSource3DSceneHash = choice.RouteProductSource3DSceneHash;
    WholeSceneTrace.SourceARouteProductCapturedEventSerial = choice.RouteProductCapturedEventSerial;
    WholeSceneTrace.SourceARouteProductCapturePresentationHash =
        choice.RouteProductCapturePresentationHash;
    WholeSceneTrace.SourceARouteProductCurrentPresentationHash =
        choice.RouteProductCurrentPresentationHash;
    WholeSceneTrace.SourceARouteProductStableFrames = choice.RouteProductStableFrames;
    WholeSceneTrace.SourceARouteProductPresentationClass =
        static_cast<u32>(choice.RouteProductPresentationClass);
}

GLRenderer2D::GLCaptureProductSources GLRenderer2D::SourceACaptureProductSources(
    const SourceACaptureReplacementChoice& choice)
{
    GLCaptureProductSources sources = {};
    sources.RouteProductTex = choice.RouteProductTex;
    sources.RouteProductPresentationClass = choice.RouteProductPresentationClass;
    sources.RouteProductStoredMasterBrightness = choice.RouteProductStoredMasterBrightness;
    sources.RouteProductHasStoredEffectState = choice.RouteProductHasStoredEffectState;
    sources.FullProductTex = choice.FullProductTex;
    sources.BackgroundTex = choice.BackgroundTex;
    return sources;
}

GLRenderer2D::GLCaptureProductSources GLRenderer2D::RouteCaptureProductSources(
    GLuint routeProductTex,
    WholeSceneCaptureProductPresentationClass presentationClass,
    u16 storedMasterBrightness,
    bool hasStoredEffectState)
{
    GLCaptureProductSources sources = {};
    sources.RouteProductTex = routeProductTex;
    sources.RouteProductPresentationClass = presentationClass;
    sources.RouteProductStoredMasterBrightness = storedMasterBrightness;
    sources.RouteProductHasStoredEffectState = hasStoredEffectState;
    return sources;
}

GLRenderer2D::GLCaptureProductSources GLRenderer2D::FullCaptureProductSources(
    GLuint fullProductTex)
{
    GLCaptureProductSources sources = {};
    sources.FullProductTex = fullProductTex;
    return sources;
}

GLRenderer2D::GLCaptureProductSources GLRenderer2D::BackgroundCaptureProductSources(
    GLuint backgroundTex,
    u16 storedMasterBrightness,
    bool hasStoredEffectState)
{
    GLCaptureProductSources sources = {};
    sources.BackgroundTex = backgroundTex;
    sources.Direct3DTex = backgroundTex;
    sources.BackgroundStoredMasterBrightness = storedMasterBrightness;
    sources.BackgroundHasStoredEffectState = hasStoredEffectState;
    return sources;
}

GLRenderer2D::GLCaptureProductResolution GLRenderer2D::ResolveCaptureProduct(
    const WholeSceneCapturePolicyResult& result,
    const WholeSceneCaptureRequest& request,
    const GLCaptureProductSources& sources)
{
    GLCaptureProductResolution resolved = {};
    resolved.Accepted = result.Accepted;
    resolved.ProductKind = result.ProductRef.Kind;
    resolved.BackgroundSource = result.ProductRef.BackgroundSource;
    resolved.RenderAction = result.RenderAction;
    resolved.PresentationClass =
        ::melonDS::CaptureProductPresentationClassForProduct(resolved.ProductKind,
                                                             resolved.RenderAction);

    switch (result.ProductRef.Kind)
    {
    case WholeSceneCaptureProductKind::RouteProduct:
    case WholeSceneCaptureProductKind::RouteEventProduct:
    case WholeSceneCaptureProductKind::RouteStateProduct:
        resolved.Tex = sources.RouteProductTex;
        resolved.PresentationClass = sources.RouteProductPresentationClass;
        break;
    case WholeSceneCaptureProductKind::BackgroundProduct:
        resolved.Tex = sources.BackgroundTex;
        break;
    case WholeSceneCaptureProductKind::FullCaptureProduct:
        resolved.Tex = sources.FullProductTex;
        break;
    case WholeSceneCaptureProductKind::ParentOutput3D:
        resolved.Tex = sources.Direct3DTex ? sources.Direct3DTex : Parent.OutputTex3D;
        break;
    case WholeSceneCaptureProductKind::HandoffSnapshot:
        resolved.Tex = sources.Direct3DTex;
        break;
    case WholeSceneCaptureProductKind::None:
        break;
    }

    const WholeSceneCaptureEffectOwner consumeEffectOwner =
        ConsumeEffectOwnerForCaptureRequest(request.Kind, GPU2D.Num != 0);
    const u16 consumeMasterBrightness =
        consumeEffectOwner == WholeSceneCaptureEffectOwner::SourceA
            ? GPU.MasterBrightnessA
            : (GPU2D.Num ? GPU.MasterBrightnessB : GPU.MasterBrightnessA);
    const bool routeProductSelected =
        resolved.ProductKind == WholeSceneCaptureProductKind::RouteProduct ||
        resolved.ProductKind == WholeSceneCaptureProductKind::RouteEventProduct ||
        resolved.ProductKind == WholeSceneCaptureProductKind::RouteStateProduct;
    const bool backgroundProductSelected =
        resolved.ProductKind == WholeSceneCaptureProductKind::BackgroundProduct;
    const u16 storedMasterBrightness =
        routeProductSelected
            ? sources.RouteProductStoredMasterBrightness
            : (backgroundProductSelected
                   ? sources.BackgroundStoredMasterBrightness
                   : 0);
    const bool hasStoredEffectState =
        routeProductSelected
            ? sources.RouteProductHasStoredEffectState
            : (backgroundProductSelected &&
               sources.BackgroundHasStoredEffectState);

    WholeSceneCaptureProductUseInputs useInputs = {};
    useInputs.PolicyAccepted = result.Accepted;
    useInputs.HasTexture = resolved.Tex != 0;
    useInputs.RequestKind = request.Kind;
    useInputs.ProductKind = resolved.ProductKind;
    useInputs.ProofKind = result.ProofKind;
    useInputs.RenderAction = resolved.RenderAction;
    useInputs.PresentationClass = resolved.PresentationClass;
    useInputs.ProductPresentationHash = request.CapturePresentationHash;
    useInputs.RequestPresentationHash = request.CurrentPresentationHash;
    useInputs.HasStoredEffectState = hasStoredEffectState;
    useInputs.StoredEffectActive =
        IsMasterBrightnessEffectActive(storedMasterBrightness);
    useInputs.ConsumeEffectActive =
        IsMasterBrightnessEffectActive(consumeMasterBrightness);
    const WholeSceneCaptureProductUseDecision useDecision =
        CanUseWholeSceneCaptureProduct(useInputs);
    resolved.Accepted = useDecision.Accepted;
    resolved.PresentationCompatible = useDecision.PresentationCompatible;
    resolved.RequiresRePresentation = useDecision.RequiresRePresentation;

    const bool presentationHashMatch =
        DoesCaptureProductPresentationMatchRequest(request.CapturePresentationHash,
                                                   request.CurrentPresentationHash);
    const bool applySourceAEffect =
        ShouldApplySourceAReplacementPresentationEffect(resolved.PresentationClass,
                                                       request.Kind,
                                                       GPU2D.Num != 0);
    const bool applyHandoffEffect =
        ShouldApplyHandoffPresentationEffect(resolved.PresentationClass,
                                            request.Kind);
    const bool applyEffectOnBlit = resolved.Accepted &&
                                   (applySourceAEffect || applyHandoffEffect);

    WholeSceneCaptureEffectAction effectAction = WholeSceneCaptureEffectAction::None;
    if (!resolved.Accepted)
    {
        effectAction = result.Accepted
            ? WholeSceneCaptureEffectAction::Reject
            : WholeSceneCaptureEffectAction::Fallback;
    }
    else if (resolved.RenderAction == WholeSceneCaptureRenderAction::CompositeCurrentOverlay)
    {
        effectAction = WholeSceneCaptureEffectAction::CompositeCurrentOverlay;
    }
    else if (applyEffectOnBlit)
    {
        effectAction = WholeSceneCaptureEffectAction::ApplyOnBlit;
    }
    else if (resolved.RequiresRePresentation)
    {
        effectAction = WholeSceneCaptureEffectAction::NeedsRePresentation;
    }
    else
    {
        effectAction = WholeSceneCaptureEffectAction::DisplayAsIs;
    }

    WholeSceneTrace.CaptureProductUseAccepted = resolved.Accepted;
    WholeSceneTrace.CaptureProductUsePresentationCompatible =
        useDecision.PresentationCompatible;
    WholeSceneTrace.CaptureProductPresentationHashMatch = presentationHashMatch;
    WholeSceneTrace.CaptureProductStoredEffectOwner =
        static_cast<u32>(resolved.PresentationClass ==
                             WholeSceneCaptureProductPresentationClass::AlreadyPresented
                         ? WholeSceneCaptureEffectOwner::CapturedPresentation
                         : WholeSceneCaptureEffectOwner::None);
    WholeSceneTrace.CaptureProductStoredEffectState =
        useInputs.HasStoredEffectState
            ? PackedMasterBrightnessEffectState(storedMasterBrightness)
            : 0;
    WholeSceneTrace.CaptureProductEffectPhaseIncompatible =
        useDecision.EffectPhaseIncompatible;
    WholeSceneTrace.CaptureProductConsumeEffectOwner =
        static_cast<u32>(consumeEffectOwner);
    WholeSceneTrace.CaptureProductConsumeEffectState =
        PackedMasterBrightnessEffectState(consumeMasterBrightness);
    WholeSceneTrace.CaptureProductEffectAction = static_cast<u32>(effectAction);
    WholeSceneTrace.CaptureProductFinalPassEffectOwner =
        static_cast<u32>(applyEffectOnBlit
                         ? WholeSceneCaptureEffectOwner::None
                         : consumeEffectOwner);
    WholeSceneTrace.CaptureProductApplyEffectOnBlit = applyEffectOnBlit;

    return resolved;
}

void GLRenderer2D::RecordChosenCaptureProductTrace(
    const GLCaptureProductResolution& product,
    const GLCaptureProductTraceIdentity& identity)
{
    WholeSceneTrace.SourceAChosenProductTex = static_cast<int>(product.Tex);
    WholeSceneTrace.SourceAChosenProductCaptureBank = identity.CaptureBank;
    WholeSceneTrace.SourceAChosenProductBackgroundEpochSerial = identity.BackgroundEpochSerial;
    WholeSceneTrace.SourceAChosenProductSource3DSerial = identity.Source3DSerial;
    WholeSceneTrace.SourceAChosenProductSource3DSceneHash = identity.Source3DSceneHash;
    WholeSceneTrace.SourceAChosenProductCaptureEventSerial = identity.CaptureEventSerial;
    WholeSceneTrace.SourceAChosenProductCapturePresentationHash = identity.CapturePresentationHash;
    WholeSceneTrace.SourceAChosenProductCurrentPresentationHash = identity.CurrentPresentationHash;
    WholeSceneTrace.SourceAChosenProductKind = static_cast<u32>(product.ProductKind);
    WholeSceneTrace.SourceAChosenProductRenderAction = static_cast<u32>(product.RenderAction);
    WholeSceneTrace.SourceAChosenProductPresentationClass = static_cast<u32>(product.PresentationClass);
}

GLRenderer2D::HandoffBackgroundChoice GLRenderer2D::MakeHandoffSnapshotBackgroundChoice(
    GLuint texture)
{
    HandoffBackgroundChoice choice = {};
    choice.Tex = texture;
    choice.Source = SourceABackgroundSource::HandoffSnapshot;
    choice.Authority = WholeSceneCaptureAuthority::HandoffSnapshot;
    return choice;
}

GLRenderer2D::HandoffBackgroundChoice GLRenderer2D::MakeCaptureEventBackgroundChoice(
    GLuint texture,
    SourceABackgroundSource source,
    u64 backgroundEpochSerial,
    u64 source3DSerial,
    u32 source3DSceneHash,
    u32 presentationHash)
{
    HandoffBackgroundChoice choice = {};
    choice.Tex = texture;
    choice.BackgroundEpochSerial = backgroundEpochSerial;
    choice.Source3DSerial = source3DSerial;
    choice.Source3DSceneHash = source3DSceneHash;
    choice.PresentationHash = presentationHash;
    choice.Source = source;
    choice.Authority = WholeSceneCaptureAuthority::CaptureEventBackground;
    return choice;
}

GLRenderer2D::HandoffBackgroundChoice GLRenderer2D::UseActiveEpochHandoffBackground(
    int handoffSlot,
    u64 backgroundEpochSerial,
    u32 captureBank,
    u64 source3DSerial,
    u32 source3DSceneHash,
    u32 presentationHash)
{
    HandoffBackgroundChoice background =
        MakeCaptureEventBackgroundChoice(Parent.ActiveCaptureBackgroundEpochTex[handoffSlot],
                                         SourceABackgroundSource::ActiveCaptureEpochTex,
                                         backgroundEpochSerial,
                                         source3DSerial,
                                         source3DSceneHash,
                                         presentationHash);
    if (handoffSlot >= 0 && handoffSlot < 2)
    {
        const auto& epoch = Parent.ActiveCaptureBackgroundEpoch[handoffSlot];
        background.StoredMasterBrightness = epoch.StoredMasterBrightness;
        background.HasStoredEffectState = epoch.HasStoredEffectState;
    }
    CaptureBackedHandoff.ReuseDecision = CaptureBackedHandoffReuseReason::UsedCaptureEventBackground;
    UpdateCaptureBackedRoutePresentation(handoffSlot,
                                         CaptureBackedRoutePresentationMode::BackgroundCurrentOverlay,
                                         backgroundEpochSerial,
                                         captureBank,
                                         source3DSceneHash,
                                         presentationHash);
    return background;
}

GLRenderer2D::GLCaptureProductResolution GLRenderer2D::RecordSourceARouteProductChoiceTrace(
    const SourceACaptureReplacementChoice& choice,
    int ystart,
    int yend)
{
    ResetWholeSceneRenderTrace();
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::SourceACaptureReplacement, ystart, yend,
                                ScaleFactor > 1, false, false, choice.RouteProductTex);
    RecordSourceACaptureProductTrace(SourceACaptureReplacementMode::CurrentOverlay,
                                     choice.BackgroundEpochSerial,
                                     SourceABackgroundSource::RouteProduct,
                                     choice.BackgroundEpochSerial,
                                     choice.CapturePresentationHash,
                                     choice.CurrentPresentationHash,
                                     choice.PreferExactRouteProduct
                                         ? SourceAProductChoiceReason::UsedExactRouteProductSameEvent
                                         : SourceAProductChoiceReason::ReusedPreviousRouteProduct,
                                     choice.FullProductKeyMatch);
    RecordSourceARouteProductTrace(choice);
    const SourceACaptureResolution resolution =
        MakeSourceARouteProductResolution(choice, ystart, yend);
    RecordSourceACaptureResolution(resolution);
    RecordSourceACaptureChoiceDebug(choice);
    const GLCaptureProductResolution product =
        ResolveCaptureProduct(resolution.Result,
                              resolution.Request,
                              SourceACaptureProductSources(choice));
    RecordChosenCaptureProductTrace(product,
                                    {choice.CaptureBank,
                                     choice.RouteProductBackgroundEpochSerial,
                                     choice.RouteProductSource3DSerial,
                                     choice.RouteProductSource3DSceneHash,
                                     choice.RouteProductCapturedEventSerial,
                                     choice.RouteProductCapturePresentationHash,
                                     choice.RouteProductCurrentPresentationHash});
    return product;
}

GLRenderer2D::GLCaptureProductResolution GLRenderer2D::RecordSourceABackgroundOverlayChoiceTrace(
    const SourceACaptureReplacementChoice& choice,
    int ystart,
    int yend)
{
    ResetWholeSceneRenderTrace();
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::SourceACaptureReplacement, ystart, yend,
                                ScaleFactor > 1, false, ScaleFactor > 1, choice.BackgroundTex);
    RecordSourceACaptureProductTrace(SourceACaptureReplacementMode::None,
                                     choice.BackgroundEpochSerial,
                                     SourceABackgroundSource::CaptureEventBackgroundTex,
                                     choice.BackgroundEpochSerial,
                                     choice.CapturePresentationHash,
                                     choice.CurrentPresentationHash,
                                     SourceAProductChoiceReason::UsedBackgroundUnderlayCurrentOverlay,
                                     choice.FullProductKeyMatch);
    const SourceACaptureResolution resolution =
        MakeSourceABackgroundOverlayResolution(choice, ystart, yend);
    RecordSourceACaptureResolution(resolution);
    RecordSourceACaptureChoiceDebug(choice);
    const GLCaptureProductResolution product =
        ResolveCaptureProduct(resolution.Result,
                              resolution.Request,
                              SourceACaptureProductSources(choice));
    RecordChosenCaptureProductTrace(product,
                                    {choice.CaptureBank,
                                     choice.BackgroundEpochSerial,
                                     choice.BackgroundSource3DSerial,
                                     choice.BackgroundSource3DSceneHash,
                                     choice.FullProductEventSerial,
                                     choice.CapturePresentationHash,
                                     choice.CurrentPresentationHash});
    return product;
}

GLRenderer2D::GLCaptureProductResolution GLRenderer2D::RecordSourceAFullProductChoiceTrace(
    const SourceACaptureReplacementChoice& choice,
    int ystart,
    int yend)
{
    ResetWholeSceneRenderTrace();
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::SourceACaptureReplacement, ystart, yend,
                                ScaleFactor > 1, false, false, choice.FullProductTex);
    RecordSourceACaptureProductTrace(
        choice.SubEngineCapturedSourceAOnly
            ? SourceACaptureReplacementMode::FullProductAfterOverlayFailed
            : SourceACaptureReplacementMode::FullProduct,
        choice.BackgroundEpochSerial,
        SourceABackgroundSource::FullCaptureProduct,
        choice.BackgroundEpochSerial,
        choice.CapturePresentationHash,
        choice.AllowExactFullProductCapturePresentation
            ? choice.CapturePresentationHash
            : choice.CurrentPresentationHash,
        choice.SubEngineCapturedSourceAOnly
            ? SourceAProductChoiceReason::UsedFullProductNoOverlayVisible
            : SourceAProductChoiceReason::UsedFullProductKeyMatch,
        choice.FullProductKeyMatch);
    const SourceACaptureResolution resolution =
        MakeSourceAFullProductResolution(choice, ystart, yend);
    RecordSourceACaptureResolution(resolution);
    RecordSourceACaptureChoiceDebug(choice);
    const GLCaptureProductResolution product =
        ResolveCaptureProduct(resolution.Result,
                              resolution.Request,
                              SourceACaptureProductSources(choice));
    RecordChosenCaptureProductTrace(product,
                                    {choice.FullProductCaptureBank,
                                     choice.BackgroundEpochSerial,
                                     choice.FullProductEventSource3DSerial,
                                     choice.FullProductEventSource3DSceneHash,
                                     choice.FullProductEventSerial,
                                     choice.FullProductEventSourcePresentationHash,
                                     resolution.Request.CurrentPresentationHash});
    return product;
}

void GLRenderer2D::RecordSourceARejectedChoiceTrace(
    const SourceACaptureReplacementChoice& choice,
    int ystart,
    int yend)
{
    RecordSourceACaptureResolution(MakeSourceARejectedResolution(choice, ystart, yend));
    RecordSourceACaptureChoiceDebug(choice);
}

GLRenderer2D::HandoffBackgroundResolveResult GLRenderer2D::ResolveCapturedHandoffBackgroundChoice(
    const WholeSceneCaptureRequest& request,
    int handoffSlot,
    int ystart,
    int yend)
{
    HandoffBackgroundResolveResult result = {};

    if (CaptureBackedHandoff.CurrentKey.Phase == CaptureBackedHandoffPhase::CapturedBitmap)
        MarkCaptureBackedRouteCapturedPhase(handoffSlot);

    if (CaptureBackedHandoff.CurrentKey.Phase == CaptureBackedHandoffPhase::CapturedBitmap)
    {
        CaptureBackedHandoffReuseReason reason = CaptureBackedHandoffReuseReason::None;
        const int visibleDisplayCaptureBank = VisibleSingleDisplayCaptureBank();
        const bool visibleDisplayCaptureBankValid =
            visibleDisplayCaptureBank >= 0 && visibleDisplayCaptureBank < 4;
        const auto* visibleEvent =
            visibleDisplayCaptureBankValid
                ? &Parent.HighResDisplayCapture256Event[visibleDisplayCaptureBank]
                : nullptr;
        const auto& epoch = Parent.ActiveCaptureBackgroundEpoch[handoffSlot];
        const u32 epochVisibleBGLayers = epoch.SourceLayerEnable & 0x0Fu;
        const u32 visibleDisplayCaptureBankValue =
            visibleDisplayCaptureBankValid
                ? static_cast<u32>(visibleDisplayCaptureBank)
                : 0xFFFFFFFFu;

        HandoffVisibleEpochMatchInputs epochMatchInputs = {};
        epochMatchInputs.VisibleDisplayCaptureBankValid = visibleDisplayCaptureBankValid;
        epochMatchInputs.EpochValid = epoch.Valid;
        epochMatchInputs.EpochCaptureBank = epoch.CaptureBank;
        epochMatchInputs.VisibleDisplayCaptureBank = visibleDisplayCaptureBankValue;
        epochMatchInputs.ConsumerRouteSlotMatches =
            epoch.ConsumerRouteSlot == static_cast<u32>(handoffSlot);
        epochMatchInputs.HasBackgroundEpochTexture =
            Parent.ActiveCaptureBackgroundEpochTex[handoffSlot] != 0;
        epochMatchInputs.ProductHasBackground3DUnderlay =
            (epoch.ProductMask & GLRenderer::HighResCaptureProductBackground3DUnderlay) != 0;
        epochMatchInputs.SourceIsCleanEngineA2DOutput =
            epoch.SourceKind == GLRenderer::HighResCaptureSourceKind::CleanEngineA2DOutput;
        epochMatchInputs.SourceDirect3DVisible = epoch.SourceDirect3DVisible;
        epochMatchInputs.SourceOnlyBG0Visible = epochVisibleBGLayers == (1u << 0);
        epochMatchInputs.SourceBGModeMatchesCurrent =
            epoch.SourceBGMode == (DispCnt & 0x7u);
        epochMatchInputs.SourceHasNoVisibleBitmap = epoch.SourceVisibleBitmapMask == 0;
        const bool epochMatchesVisibleBank =
            DoesHandoffEpochMatchVisibleBank(epochMatchInputs);

        HandoffEventRecencyInputs recencyInputs = {};
        recencyInputs.EpochMatchesVisibleBank = epochMatchesVisibleBank;
        recencyInputs.VisibleEventValid = visibleEvent && visibleEvent->Valid;
        recencyInputs.VisibleEventSerial = visibleEvent ? visibleEvent->Serial : 0;
        recencyInputs.EpochSerial = epoch.Serial;
        const bool recentEpochForVisibleBank =
            IsHandoffEventRecentForEpoch(recencyInputs);

        HandoffExactEpochInputs exactEpochInputs = {};
        exactEpochInputs.RecentEpochForVisibleBank = recentEpochForVisibleBank;
        exactEpochInputs.VisibleEventSerial = visibleEvent ? visibleEvent->Serial : 0;
        exactEpochInputs.EpochSerial = epoch.Serial;
        exactEpochInputs.VisibleEventAccepted =
            visibleEvent &&
            visibleEvent->RejectReason == GLRenderer::HighResCaptureRejectReason::None;
        exactEpochInputs.VisibleEventFullEquivalent =
            visibleEvent &&
            (visibleEvent->ProductMask & GLRenderer::HighResCaptureProductFullEquivalent);
        const bool exactEpochForVisibleBank =
            IsHandoffExactEpochForVisibleBank(exactEpochInputs);

        const auto& routePresentation = CaptureBackedRoute[handoffSlot].Presentation;
        HandoffRoutePresentationStableInputs routePresentationInputs = {};
        routePresentationInputs.PresentationValid = routePresentation.Valid;
        routePresentationInputs.PresentationCaptureBank = routePresentation.CaptureBank;
        routePresentationInputs.VisibleDisplayCaptureBank = visibleDisplayCaptureBankValue;
        routePresentationInputs.PresentationSourceHash =
            routePresentation.SourcePresentationHash;
        routePresentationInputs.EpochSourceHash = epoch.SourcePresentationHash;
        routePresentationInputs.StableFrames = routePresentation.StableFrames;
        const bool routePresentationStable =
            IsHandoffRoutePresentationStable(routePresentationInputs);

        const bool capturedBitmapUpdatedThisFrame =
            CaptureBackedHandoff.CurrentKey.BGUploadRows >= 192;
        HandoffRouteBackgroundOverlayInputs routeOverlayInputs = {};
        routeOverlayInputs.RecentEpochForVisibleBank = recentEpochForVisibleBank;
        routeOverlayInputs.RoutePresentationStable = routePresentationStable;
        routeOverlayInputs.CapturedBitmapUpdatedThisFrame = capturedBitmapUpdatedThisFrame;
        const bool useRouteBackgroundOverlay =
            ShouldUseHandoffRouteBackgroundOverlay(routeOverlayInputs);

        if (useRouteBackgroundOverlay)
        {
            result.Background = UseActiveEpochHandoffBackground(handoffSlot,
                                                                epoch.Serial,
                                                                epoch.CaptureBank,
                                                                epoch.Source3DSerial,
                                                                epoch.Source3DSceneHash,
                                                                epoch.SourcePresentationHash);
        }
        else if (exactEpochForVisibleBank)
        {
            const u32 currentPresentationHash = CapturePresentationHash();
            const CaptureBackedRouteProductLookup routeProduct =
                ResolveHandoffExactRouteProduct(handoffSlot,
                                                visibleEvent->Serial,
                                                visibleEvent->Source3DSerial,
                                                visibleEvent->Source3DSceneHash,
                                                epoch.CaptureBank,
                                                epoch.Serial,
                                                epoch.Source3DSerial,
                                                epoch.Source3DSceneHash,
                                                visibleEvent->SourcePresentationHash,
                                                currentPresentationHash,
                                                ystart,
                                                yend);
            const GLuint routeProductTex = routeProduct.Tex;
            if (routeProductTex)
            {
                const GLCaptureProductResolution product =
                    RecordHandoffRouteProductTrace(request,
                                                   routeProduct,
                                                   routeProductTex,
                                                   epoch.CaptureBank,
                                                   visibleEvent->Serial,
                                                   epoch.Serial,
                                                   visibleEvent->SourcePresentationHash,
                                                   currentPresentationHash,
                                                   ystart,
                                                   yend);
                if (product.Accepted)
                {
                    CaptureBackedHandoff.ReuseDecision =
                        CaptureBackedHandoffReuseReason::UsedCaptureEventBackground;
                    UpdateCaptureBackedRoutePresentation(handoffSlot,
                                                         CaptureBackedRoutePresentationMode::BackgroundCurrentOverlay,
                                                         epoch.Serial,
                                                         epoch.CaptureBank,
                                                         epoch.Source3DSceneHash,
                                                         visibleEvent->SourcePresentationHash,
                                                         currentPresentationHash);

                    BlitWholeSceneHandoffProduct(product, ystart, yend);
                    result.Finished = true;
                    return result;
                }
            }

            const GLuint fullProductTex = VisibleHighResCaptureFullTex();
            if (fullProductTex)
            {
                const GLCaptureProductResolution product =
                    RecordHandoffFullProductTrace(request,
                                                  fullProductTex,
                                                  epoch.CaptureBank,
                                                  visibleEvent->Serial,
                                                  epoch.Serial,
                                                  visibleEvent->SourcePresentationHash,
                                                  currentPresentationHash,
                                                  ystart,
                                                  yend);
                if (product.Accepted)
                {
                    CaptureBackedHandoff.ReuseDecision =
                        CaptureBackedHandoffReuseReason::UsedCaptureEventFullProduct;
                    UpdateCaptureBackedRoutePresentation(handoffSlot,
                                                         CaptureBackedRoutePresentationMode::FullProduct,
                                                         epoch.Serial,
                                                         epoch.CaptureBank,
                                                         epoch.Source3DSceneHash,
                                                         visibleEvent->SourcePresentationHash);

                    BlitWholeSceneHandoffProduct(product, ystart, yend);
                    result.Finished = true;
                    return result;
                }
            }
        }

        if (!result.Background.Tex)
        {
            if (recentEpochForVisibleBank && !capturedBitmapUpdatedThisFrame)
            {
                result.Background = UseActiveEpochHandoffBackground(handoffSlot,
                                                                    epoch.Serial,
                                                                    epoch.CaptureBank,
                                                                    epoch.Source3DSerial,
                                                                    epoch.Source3DSceneHash,
                                                                    epoch.SourcePresentationHash);
            }
            else if (CanReuseCaptureBackedHandoffSnapshot(CaptureBackedHandoff.CurrentKey, handoffSlot, reason))
            {
                CaptureBackedHandoff.ReuseDecision = reason;
                result.Background =
                    MakeHandoffSnapshotBackgroundChoice(CaptureBackedRouteGL[handoffSlot].Handoff3DTex);
                UpdateCaptureBackedRoutePresentation(handoffSlot,
                                                     CaptureBackedRoutePresentationMode::HandoffSnapshot,
                                                     0,
                                                     0xFFFFFFFFu,
                                                     0,
                                                     0);
            }
            else
            {
                const int visibleCaptureBank = VisibleSingleHighResCaptureBank();
                u32 presentationHash = 0;
                if (visibleCaptureBank >= 0 && visibleCaptureBank < 4)
                    presentationHash = Parent.HighResDisplayCapture256Event[visibleCaptureBank].SourcePresentationHash;
                result.Background =
                    MakeCaptureEventBackgroundChoice(VisibleHighResCaptureBackgroundTex(),
                                                     SourceABackgroundSource::CaptureEventBackgroundTex,
                                                     0,
                                                     0,
                                                     0,
                                                     presentationHash);
                if (!result.Background.Tex)
                {
                    CaptureBackedHandoff.ReuseDecision = reason;
                    RenderScreenWholeSceneCaptureBackedHybridFallback(ystart, yend);
                    result.Finished = true;
                    return result;
                }

                CaptureBackedHandoff.ReuseDecision = CaptureBackedHandoffReuseReason::UsedCaptureEventBackground;
            }
        }
    }
    else
    {
        CaptureBackedHandoffReuseReason reason = CaptureBackedHandoffReuseReason::None;
        if (CanReuseCaptureBackedHandoffSnapshot(CaptureBackedHandoff.CurrentKey, handoffSlot, reason))
        {
            CaptureBackedHandoff.ReuseDecision = reason;
            result.Background =
                MakeHandoffSnapshotBackgroundChoice(CaptureBackedRouteGL[handoffSlot].Handoff3DTex);
        }
        else
        {
            CaptureBackedHandoff.ReuseDecision = reason;
            RenderScreenWholeSceneCaptureBackedHybridFallback(ystart, yend);
            result.Finished = true;
            return result;
        }
    }

    return result;
}


GLRenderer2D::GLCaptureProductResolution GLRenderer2D::RecordHandoffRouteProductTrace(
    const WholeSceneCaptureRequest& baseRequest,
    const CaptureBackedRouteProductLookup& routeProduct,
    GLuint routeProductTex,
    u32 captureBank,
    u64 captureEventSerial,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash,
    int ystart,
    int yend)
{
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::CaptureBackedHandoff, ystart, yend,
                                ScaleFactor > 1, false, false, routeProductTex);
    RecordSourceACaptureProductTrace(SourceACaptureReplacementMode::CurrentOverlay,
                                     backgroundEpochSerial,
                                     SourceABackgroundSource::RouteProduct,
                                     backgroundEpochSerial,
                                     capturePresentationHash,
                                     currentPresentationHash,
                                     SourceAProductChoiceReason::ReusedPreviousRouteProduct);
    RecordSourceARouteProductTrace(routeProduct.Identity,
                                   routeProduct.CapturedEventSerial,
                                   routeProduct.StableFrames,
                                   routeProduct.PresentationClass);
    const HandoffCaptureResolution resolution =
        MakeHandoffRouteProductResolution(baseRequest,
                                          routeProduct,
                                          captureBank,
                                          captureEventSerial,
                                          backgroundEpochSerial,
                                          capturePresentationHash,
                                          currentPresentationHash);
    RecordHandoffCaptureResolution(resolution);
    const GLCaptureProductResolution product =
        ResolveCaptureProduct(resolution.Result,
                              resolution.Request,
                              RouteCaptureProductSources(routeProductTex,
                                                         routeProduct.PresentationClass,
                                                         routeProduct.StoredMasterBrightness,
                                                         routeProduct.HasStoredEffectState));
    RecordChosenCaptureProductTrace(product,
                                    {static_cast<int>(captureBank),
                                     routeProduct.Identity.BackgroundEpochSerial,
                                     routeProduct.Identity.Source3DSerial,
                                     routeProduct.Identity.Source3DSceneHash,
                                     captureEventSerial,
                                     capturePresentationHash,
                                     currentPresentationHash});
    return product;
}

GLRenderer2D::GLCaptureProductResolution GLRenderer2D::RecordHandoffFullProductTrace(
    const WholeSceneCaptureRequest& baseRequest,
    GLuint fullProductTex,
    u32 captureBank,
    u64 captureEventSerial,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash,
    int ystart,
    int yend)
{
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::CaptureBackedHandoff, ystart, yend,
                                ScaleFactor > 1, false, false, fullProductTex);
    RecordSourceACaptureProductTrace(SourceACaptureReplacementMode::FullProduct,
                                     backgroundEpochSerial,
                                     SourceABackgroundSource::FullCaptureProduct,
                                     backgroundEpochSerial,
                                     capturePresentationHash,
                                     currentPresentationHash,
                                     SourceAProductChoiceReason::UsedFullProductRouteBridge);
    const HandoffCaptureResolution resolution =
        MakeHandoffFullProductResolution(baseRequest,
                                         captureBank,
                                         captureEventSerial,
                                         backgroundEpochSerial,
                                         capturePresentationHash,
                                         currentPresentationHash);
    RecordHandoffCaptureResolution(resolution);
    const GLCaptureProductResolution product =
        ResolveCaptureProduct(resolution.Result,
                              resolution.Request,
                              FullCaptureProductSources(fullProductTex));
    RecordChosenCaptureProductTrace(product,
                                    {static_cast<int>(captureBank),
                                     backgroundEpochSerial,
                                     0,
                                     0,
                                     captureEventSerial,
                                     capturePresentationHash,
                                     currentPresentationHash});
    return product;
}

GLRenderer2D::GLCaptureProductResolution GLRenderer2D::RecordHandoffBackgroundOverlayTrace(
    const WholeSceneCaptureRequest& baseRequest,
    SourceABackgroundSource backgroundSource,
    WholeSceneCaptureAuthority authority,
    GLuint backgroundTex,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash,
    u16 storedMasterBrightness,
    bool hasStoredEffectState,
    int ystart,
    int yend)
{
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::CaptureBackedHandoff, ystart, yend,
                                ScaleFactor > 1, false, ScaleFactor > 1, backgroundTex);
    RecordSourceACaptureProductTrace(SourceACaptureReplacementMode::CurrentOverlay,
                                     backgroundEpochSerial,
                                     backgroundSource,
                                     backgroundEpochSerial,
                                     capturePresentationHash,
                                     currentPresentationHash,
                                     SourceAProductChoiceReason::UsedBackgroundUnderlayCurrentOverlay);
    const HandoffCaptureResolution resolution =
        MakeHandoffBackgroundOverlayResolution(baseRequest,
                                               backgroundSource,
                                               authority,
                                               backgroundEpochSerial,
                                               capturePresentationHash,
                                               currentPresentationHash);
    RecordHandoffCaptureResolution(resolution);
    const GLCaptureProductResolution product =
        ResolveCaptureProduct(resolution.Result,
                              resolution.Request,
                              BackgroundCaptureProductSources(backgroundTex,
                                                             storedMasterBrightness,
                                                             hasStoredEffectState));
    RecordChosenCaptureProductTrace(product,
                                    {baseRequest.CaptureBank < 4
                                         ? static_cast<int>(baseRequest.CaptureBank)
                                         : -1,
                                     backgroundEpochSerial,
                                     0,
                                     0,
                                     baseRequest.CaptureEventSerial,
                                     capturePresentationHash,
                                     currentPresentationHash});
    return product;
}

void GLRenderer2D::RecordHandoffHybridTrace(const WholeSceneCaptureRequest& baseRequest,
                                            SourceABackgroundSource backgroundSource,
                                            WholeSceneCaptureAuthority authority,
                                            GLuint direct3DTex,
                                            bool highRes3D,
                                            u64 backgroundEpochSerial,
                                            u32 capturePresentationHash,
                                            bool hasHighResBackground,
                                            int ystart,
                                            int yend)
{
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::CaptureBackedHandoff, ystart, yend,
                                highRes3D, false, highRes3D, direct3DTex);
    RecordSourceABackgroundTrace(backgroundEpochSerial,
                                 backgroundSource,
                                 backgroundEpochSerial,
                                 capturePresentationHash);
    RecordHandoffCaptureResolution(
        MakeHandoffHybridResolution(baseRequest,
                                    backgroundSource,
                                    authority,
                                    backgroundEpochSerial,
                                    capturePresentationHash,
                                    hasHighResBackground));
}

GLRenderer2D::GLCaptureProductResolution GLRenderer2D::RecordCaptureEpochOverlayTrace(
    int routeSlot,
    u32 captureBank,
    SourceABackgroundSource backgroundSource,
    GLuint backgroundTex,
    u64 requestBackgroundEpochSerial,
    u64 routeProductBackgroundSerial,
    u32 routeProductPresentationHash,
    u32 currentPresentationHash,
    u16 storedMasterBrightness,
    bool hasStoredEffectState,
    int ystart,
    int yend)
{
    ResetWholeSceneRenderTrace();
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::CaptureEpochOverlay, ystart, yend,
                                ScaleFactor > 1, false, ScaleFactor > 1, backgroundTex);
    RecordSourceACaptureProductTrace(SourceACaptureReplacementMode::CurrentOverlay,
                                     requestBackgroundEpochSerial,
                                     backgroundSource,
                                     routeProductBackgroundSerial,
                                     routeProductPresentationHash,
                                     currentPresentationHash,
                                     SourceAProductChoiceReason::UsedBackgroundUnderlayCurrentOverlay);
    WholeSceneCaptureRequest request = ::melonDS::MakeLiveOverlayProducerCaptureRequest(
        ystart,
        yend,
        routeSlot,
        captureBank,
        routeProductBackgroundSerial,
        routeProductPresentationHash,
        currentPresentationHash);
    const WholeSceneCapturePolicyResult result =
        ::melonDS::MakeBackgroundCapturePolicyResult(
            backgroundSource,
            WholeSceneCaptureRenderAction::CompositeCurrentOverlay,
            WholeSceneCaptureAuthority::CaptureEpochBackgroundCurrentOverlay);
    RecordWholeSceneCaptureResolution(request, result);
    const GLCaptureProductResolution product =
        ResolveCaptureProduct(result,
                              request,
                              BackgroundCaptureProductSources(backgroundTex,
                                                             storedMasterBrightness,
                                                             hasStoredEffectState));
    RecordChosenCaptureProductTrace(product,
                                    {static_cast<int>(captureBank),
                                     routeProductBackgroundSerial,
                                     0,
                                     0,
                                     0,
                                     routeProductPresentationHash,
                                     currentPresentationHash});
    return product;
}

}
