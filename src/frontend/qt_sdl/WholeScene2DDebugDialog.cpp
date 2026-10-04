// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include "EmuInstance.h"
#include "EmuThread.h"
#include "GPU.h"
#include "RendererDebug.h"
#include "Window.h"
#include "WholeScene2DDebugDialog.h"

#include <utility>
#include <vector>

WholeScene2DDebugDialog* WholeScene2DDebugDialog::currentDlg = nullptr;

namespace
{
struct DebugViewSpec
{
    const char* Category;
    const char* Label;
    melonDS::WholeScene2DDebugView View;
};

const std::vector<DebugViewSpec>& DebugViewSpecs()
{
    using DebugView = melonDS::WholeScene2DDebugView;

    static const std::vector<DebugViewSpec> views = {
        {"Native final/state", "Native final", DebugView::NativeFinal},
        {"Native final/state", "Native exact final", DebugView::NativeExactFinal},
        {"Native final/state", "Native top", DebugView::NativeTopColor},
        {"Native final/state", "Native second", DebugView::NativeSecondColor},
        {"Native final/state", "Native meta", DebugView::NativeMeta},

        {"Native BG/OBJ layers", "Native BG0", DebugView::NativeBG0Color},
        {"Native BG/OBJ layers", "Native BG1", DebugView::NativeBG1Color},
        {"Native BG/OBJ layers", "Native BG2", DebugView::NativeBG2Color},
        {"Native BG/OBJ layers", "Native BG3", DebugView::NativeBG3Color},
        {"Native BG/OBJ layers", "Native OBJ color", DebugView::NativeOBJColor},
        {"Native BG/OBJ layers", "Native OBJ flags", DebugView::NativeOBJFlags},
        {"Native BG/OBJ layers", "Native OBJ coverage", DebugView::NativeOBJCoverage},

        {"3D role/coverage", "Native 3D resolve", DebugView::Native3DResolve},
        {"3D role/coverage", "Native 3D semantics", DebugView::Native3DSemantics},
        {"3D role/coverage", "Final-native 3D input", DebugView::FinalNative3DInput},
        {"3D role/coverage", "Native 3D stack role", DebugView::Native3DStackRole},
        {"3D role/coverage", "Upscaled 3D stack role", DebugView::Upscaled3DStackRole},
        {"3D role/coverage", "Direct 3D", DebugView::Direct3D},

        {"Upscaled stack", "Upscaled top", DebugView::UpscaledTopColor},
        {"Upscaled stack", "Upscaled second", DebugView::UpscaledSecondColor},
        {"Upscaled stack", "Upscaled meta", DebugView::UpscaledMeta},
        {"Upscaled stack", "Upscaled coverage", DebugView::UpscaledCoverage},

        {"High-res compositor", "High-res BG0", DebugView::HighResBG0Color},
        {"High-res compositor", "High-res BG1", DebugView::HighResBG1Color},
        {"High-res compositor", "High-res BG2", DebugView::HighResBG2Color},
        {"High-res compositor", "High-res BG3", DebugView::HighResBG3Color},
        {"High-res compositor", "High-res BG0 meta", DebugView::HighResBG0Meta},
        {"High-res compositor", "High-res BG1 meta", DebugView::HighResBG1Meta},
        {"High-res compositor", "High-res BG2 meta", DebugView::HighResBG2Meta},
        {"High-res compositor", "High-res BG3 meta", DebugView::HighResBG3Meta},
        {"High-res compositor", "Compositor OBJ color", DebugView::HighResOBJColor},
        {"High-res compositor", "Compositor OBJ flags", DebugView::HighResOBJFlags},
        {"High-res compositor", "Compositor OBJ coverage", DebugView::HighResOBJCoverage},

        {"Strict affine", "Candidate color", DebugView::StrictAffineCandidate},
        {"Strict affine", "Native reference", DebugView::StrictAffineNativeReference},
        {"Strict affine", "Downsample difference", DebugView::StrictAffineDownsampleDifference},
        {"Strict affine", "Enhanced BG2 source", DebugView::StrictAffineEnhancedBG2Source},
        {"Strict affine", "Enhanced BG3 source", DebugView::StrictAffineEnhancedBG3Source},
        {"Strict affine", "Enhanced transformed OBJ source", DebugView::StrictAffineEnhancedOBJSource},
        {"Strict affine", "Native OBJ source atlas (RGB / alpha)", DebugView::NativeOBJSourceAtlas},
        {"Strict affine", "Enhanced OBJ source atlas (RGB / alpha)", DebugView::StrictAffineEnhancedOBJSourceAtlas},
        {"Strict affine", "Assembled OBJ source atlas (RGB / alpha)", DebugView::StrictAffineAssembledOBJSourceAtlas},
        {"Strict affine", "Transformed OBJ color", DebugView::StrictAffineTransformedOBJColor},
        {"Strict affine", "Transformed OBJ coverage", DebugView::StrictAffineTransformedOBJCoverage},
        {"Strict affine", "Ordinary OBJ band 0 color", DebugView::StrictAffineOrdinaryOBJBand0Color},
        {"Strict affine", "Ordinary OBJ band 0 coverage", DebugView::StrictAffineOrdinaryOBJBand0Coverage},
        {"Strict affine", "Ordinary OBJ band 0 scaled", DebugView::StrictAffineOrdinaryOBJBand0Scaled},
        {"Strict affine", "Unified ordinary OBJ reconstruction", DebugView::StrictAffineResolvedOrdinaryOBJ},

        {"Overlay operator", "Overlay operator color", DebugView::OverlayOperatorColor},
        {"Overlay operator", "Overlay underlay weight", DebugView::OverlayUnderlayWeight},
        {"Overlay operator", "Overlay reconstructed native", DebugView::OverlayReconstructedNative},
        {"Overlay operator", "Overlay reconstruction error", DebugView::OverlayReconstructionError},
        {"Overlay operator", "Overlay validity/confidence", DebugView::OverlayValidityConfidence},
        {"Overlay operator", "Overlay ownership/reason", DebugView::OverlayOwnershipReason},
        {"Overlay operator", "Enhanced underlay", DebugView::OverlayEnhancedUnderlay},
        {"Overlay operator", "Overlay true native final", DebugView::OverlayTrueNativeFinal},
        {"Overlay operator", "Final operator result", DebugView::OverlayFinalResult},

        {"Hybrid selector", "Hybrid selector", DebugView::HybridSelector},
        {"Hybrid selector", "Hybrid coverage miss", DebugView::HybridCoverageMiss},
        {"Hybrid selector", "Hybrid foreground alpha", DebugView::HybridForegroundAlpha},
        {"Hybrid selector", "Hybrid final source", DebugView::HybridFinalSource},

        {"Sandwich probe", "Lower 2D candidate", DebugView::SandwichLower2D},
        {"Sandwich probe", "Upper 2D candidate", DebugView::SandwichUpper2D},
        {"Sandwich probe", "Eligibility mask", DebugView::SandwichEligibility},

        {"Final output", "Final top", DebugView::FinalTop},
        {"Final output", "Final bottom", DebugView::FinalBottom},
        {"Final output", "Main VRAM display raw", DebugView::MainVRAMDisplayRaw},

        {"Capture banks", "Raw VRAM bank A", DebugView::MainVRAMDisplayRawBank0},
        {"Capture banks", "Raw VRAM bank B", DebugView::MainVRAMDisplayRawBank1},
        {"Capture banks", "Raw VRAM bank C", DebugView::MainVRAMDisplayRawBank2},
        {"Capture banks", "Raw VRAM bank D", DebugView::MainVRAMDisplayRawBank3},
        {"Capture banks", "Capture output bank A", DebugView::CaptureOutput256Bank0},
        {"Capture banks", "Capture output bank B", DebugView::CaptureOutput256Bank1},
        {"Capture banks", "Capture output bank C", DebugView::CaptureOutput256Bank2},
        {"Capture banks", "Capture output bank D", DebugView::CaptureOutput256Bank3},
        {"Capture banks", "Full product bank A", DebugView::HighResDisplayCaptureFullBank0},
        {"Capture banks", "Full product bank B", DebugView::HighResDisplayCaptureFullBank1},
        {"Capture banks", "Full product bank C", DebugView::HighResDisplayCaptureFullBank2},
        {"Capture banks", "Full product bank D", DebugView::HighResDisplayCaptureFullBank3},
        {"Capture banks", "Background product bank A", DebugView::HighResDisplayCaptureBackgroundBank0},
        {"Capture banks", "Background product bank B", DebugView::HighResDisplayCaptureBackgroundBank1},
        {"Capture banks", "Background product bank C", DebugView::HighResDisplayCaptureBackgroundBank2},
        {"Capture banks", "Background product bank D", DebugView::HighResDisplayCaptureBackgroundBank3},
        {"Capture banks", "Main VRAM epoch bank A", DebugView::MainVRAMDisplayEpochBank0},
        {"Capture banks", "Main VRAM epoch bank B", DebugView::MainVRAMDisplayEpochBank1},
        {"Capture banks", "Main VRAM epoch bank C", DebugView::MainVRAMDisplayEpochBank2},
        {"Capture banks", "Main VRAM epoch bank D", DebugView::MainVRAMDisplayEpochBank3},
        {"Affine overlap", "Overlap semantic scene", DebugView::StrictAffineOverlapSemantic},
        {"Affine overlap", "Overlap exposed underlay", DebugView::StrictAffineOverlapUnderlay},
        {"Affine overlap", "Overlap tap decisions", DebugView::StrictAffineOverlapDecisions},
        {"Affine overlap", "Overlap subpixel RGBA", DebugView::StrictAffineOverlapSubpixelColor},
        {"Affine overlap", "Overlap subpixel flags", DebugView::StrictAffineOverlapSubpixelFlags},
        {"Affine overlap", "Overlap subpixel coverage", DebugView::StrictAffineOverlapSubpixelCoverage},
    };

    return views;
}

QString SafeExportName(QString text)
{
    text = text.trimmed().toLower();
    for (int i = 0; i < text.size(); i++)
    {
        QChar ch = text.at(i);
        if (!ch.isLetterOrNumber())
            text[i] = '-';
    }

    while (text.contains("--"))
        text.replace("--", "-");

    while (text.startsWith('-'))
        text.remove(0, 1);
    while (text.endsWith('-'))
        text.chop(1);

    if (text.isEmpty())
        return "view";

    return text;
}

QString DebugViewDescription(melonDS::WholeScene2DDebugView view)
{
    using DebugView = melonDS::WholeScene2DDebugView;

    switch (view)
    {
    case DebugView::NativeFinal:
        return "Native prepass output before whole-scene scaling. This is not always the exact postprocessing upscale source.";
    case DebugView::NativeExactFinal:
        return "Exact native final texture generated by postprocessing upscale, or the black-underlay overlay endpoint in presentation overlay mode.";
    case DebugView::Native3DResolve:
        return "Native-resolution Direct3D visual resolve generated from high-resolution Direct3D before final-native or overlay compositing. Alpha shows visual coverage.";
    case DebugView::Native3DSemantics:
        return "Native-stage Direct3D semantics. Red shows visual coverage, green shows native material alpha, and blue shows native presence.";
    case DebugView::FinalNative3DInput:
        return "The Direct3D input actually used by the native-stage final-native or overlay compositor. This may be native-rendered, high-res sampled directly, or visual RGB combined with native semantics.";
    case DebugView::NativeTopColor:
        return "Native front-layer candidate used by the whole-scene path.";
    case DebugView::NativeSecondColor:
        return "Native under-layer candidate used by the whole-scene path.";
    case DebugView::NativeMeta:
        return "Native metadata, tinted by resolved top/second source class.";
    case DebugView::NativeBG0Color:
        return "Native screen-space BG0 layer after scroll/affine, mosaic, windows, and layer enable checks.";
    case DebugView::NativeBG1Color:
        return "Native screen-space BG1 layer after scroll/affine, mosaic, windows, and layer enable checks.";
    case DebugView::NativeBG2Color:
        return "Native screen-space BG2 layer after scroll/affine, mosaic, windows, and layer enable checks.";
    case DebugView::NativeBG3Color:
        return "Native screen-space BG3 layer after scroll/affine, mosaic, windows, and layer enable checks.";
    case DebugView::NativeOBJColor:
        return "Native screen-space OBJ layer color after mosaic, windows, and OBJ enable checks.";
    case DebugView::NativeOBJFlags:
        return "Native screen-space OBJ flags after mosaic, windows, and OBJ enable checks. Red = alpha/blend mode, green = mosaic, blue = OBJ window/priority.";
    case DebugView::NativeOBJCoverage:
        return "Native screen-space OBJ coverage after mosaic, windows, and OBJ enable checks.";
    case DebugView::Native3DStackRole:
        return "Native 3D stack role classification. Magenta = Direct3D foreground, cyan = 2D above Direct3D, blue = Direct3D absent at this native pixel/hidden/unknown, yellow = native 2D effect, orange = active/unknown Direct3D-targeted effect, dark orange = neutral Direct3D-targeted effect, gray = BG0/Direct3D disabled or window-excluded.";
    case DebugView::Upscaled3DStackRole:
        return "Upscaled 3D stack role classification using high-resolution Direct3D coverage. Magenta = Direct3D foreground, cyan = 2D above Direct3D, blue = Direct3D absent/hidden/unknown, yellow = native 2D effect, orange = active/unknown Direct3D-targeted effect, dark orange = neutral Direct3D-targeted effect, gray = BG0/Direct3D disabled or window-excluded.";
    case DebugView::UpscaledTopColor:
        return "Upscaled front-layer candidate after the selected scaler.";
    case DebugView::UpscaledSecondColor:
        return "Upscaled under-layer candidate after the selected scaler.";
    case DebugView::UpscaledMeta:
        return "Upscaled metadata, tinted by resolved top/second source class.";
    case DebugView::UpscaledCoverage:
        return "Upscaled coverage mask. Red = top coverage, green = second coverage.";
    case DebugView::HighResBG0Color:
        return "High-resolution compositor BG0 layer color before final compositing.";
    case DebugView::HighResBG1Color:
        return "High-resolution compositor BG1 layer color before final compositing.";
    case DebugView::HighResBG2Color:
        return "High-resolution compositor BG2 layer color before final compositing.";
    case DebugView::HighResBG3Color:
        return "High-resolution compositor BG3 layer color before final compositing.";
    case DebugView::HighResBG0Meta:
        return "High-resolution compositor BG0 source metadata. Colors are hashed from source type and ID.";
    case DebugView::HighResBG1Meta:
        return "High-resolution compositor BG1 source metadata. Colors are hashed from source type and ID.";
    case DebugView::HighResBG2Meta:
        return "High-resolution compositor BG2 source metadata. Colors are hashed from source type and ID.";
    case DebugView::HighResBG3Meta:
        return "High-resolution compositor BG3 source metadata. Colors are hashed from source type and ID.";
    case DebugView::HighResOBJColor:
        return "OBJ color target used by high-resolution compositor mode before final compositing.";
    case DebugView::HighResOBJFlags:
        return "OBJ flags used by high-resolution compositor mode. Red = alpha/blend mode, green = mosaic, blue = OBJ window/priority.";
    case DebugView::HighResOBJCoverage:
        return "OBJ coverage used by high-resolution compositor mode. Native OBJ fallback shows source alpha.";
    case DebugView::StrictAffineCandidate:
        return "Complete output-resolution image produced by the strict affine path. Affine source RGB may be nearest, filtered inline, or sampled from a reconstructed cache; native source presence remains authoritative.";
    case DebugView::StrictAffineNativeReference:
        return "Independent native-resolution compositor result generated from the same accepted strict-affine frame.";
    case DebugView::StrictAffineDownsampleDifference:
        return "Unamplified absolute RGB difference after box-downsampling the strict affine candidate to the independent native reference. Black is exact.";
    case DebugView::StrictAffineEnhancedBG2Source:
        return "Spline36/ArtCNN/CuNNy/NNEDI3/xBRZ-enhanced logical BG2 source product at its generated resolution, before screen-space sampling and placement. RGB forced opaque is shown on the left and reconstructed alpha on the right.";
    case DebugView::StrictAffineEnhancedBG3Source:
        return "Spline36/ArtCNN/CuNNy/NNEDI3/xBRZ-enhanced logical BG3 source product at its generated resolution, before screen-space sampling and placement. RGB forced opaque is shown on the left and reconstructed alpha on the right.";
    case DebugView::NativeOBJSourceAtlas:
        return "Decoded native sprite atlas: 16 columns of 64x64 compact slots. RGB is forced opaque on the left; native alpha is shown on the right. Unused cells are not meaningful. Slot metadata is in the view status.";
    case DebugView::StrictAffineAssembledOBJSourceAtlas:
        return "Current assembled normal-source cache. RGB is on the left and reconstructed alpha on the right. Status lists each slot's root and member OAM indices. Shadows are not included; this view reads existing products.";
    case DebugView::StrictAffineEnhancedOBJSourceAtlas:
        return "Existing enhanced sprite cache atlas: same compact slots multiplied by the cache scale. RGB is on the left, alpha on the right. Only slots marked cache_valid in the status are current; this view does not rebuild products.";
    case DebugView::StrictAffineEnhancedOBJSource:
        return "Selected active strict-path affine OBJ cache tile after source reconstruction but before OAM sampling. RGB forced opaque is shown on the left and source alpha on the right, so hidden fringe color remains inspectable.";
    case DebugView::StrictAffineTransformedOBJColor:
        return "Output-resolution RGB for genuinely transformed affine OBJ after OAM sampling, before priority composition and masked edge AA. Alpha is forced opaque for diagnosis; compare the separate coverage view.";
    case DebugView::StrictAffineTransformedOBJCoverage:
        return "Output-resolution transformed affine-OBJ support before final composition. Red is reconstructed source coverage; green is affine semantic presence.";
    case DebugView::StrictAffineOrdinaryOBJBand0Color:
        return "Native-resolution color for the first admitted assembled ordinary-OBJ band. It is rendered directly from the band member set, so ordinary pixels hidden by affine OBJ or BG at the native final sample remain available.";
    case DebugView::StrictAffineOrdinaryOBJBand0Coverage:
        return "Native-resolution source coverage for the first admitted assembled ordinary-OBJ band. This is a diagnostic compositor input, not final scene ownership or reconstructed alpha.";
    case DebugView::StrictAffineOrdinaryOBJBand0Scaled:
        return "Selected-scaler reconstruction of the first admitted assembled ordinary-OBJ band. Every member is reconstructed together as one surface; this diagnostic product is not consumed by final composition.";
    case DebugView::StrictAffineResolvedOrdinaryOBJ:
        return "Selected-scaler reconstruction of the complete native OAM-resolved ordinary-OBJ surface consumed by the unified strict-affine OBJ merge.";
    case DebugView::StrictAffineOverlapSemantic:
        return "Completed semantic scene before localized 2x replacement in the immediate compositor.";
    case DebugView::StrictAffineOverlapUnderlay:
        return "Completed second/third-layer resolve. A tap uses this when replacing the same semantic affine winner; otherwise it uses the semantic scene.";
    case DebugView::StrictAffineOverlapDecisions:
        return "Raw decision bytes: R/G/B/A are GL offsets (0,0)/(1,0)/(0,1)/(1,1). 0 inactive; 1 absent/lower; 2 absent/semantic; 3 unsupported material (aborts all taps); 4 zero alpha; 5 blocked OBJ; 6 blocked BG; 7 accepted/lower; 8 accepted/semantic. Alpha is data, not opacity.";
    case DebugView::StrictAffineOverlapSubpixelColor:
        return "Exact RGBA8 snapshot consumed by immediate 2x composition. RGB may be premultiplied; alpha is retained. See renderer status for the premultiplication mode.";
    case DebugView::StrictAffineOverlapSubpixelFlags:
        return "Raw flags snapshot: R material, G mosaic, B window, A BG priority. Alpha is data, not opacity.";
    case DebugView::StrictAffineOverlapSubpixelCoverage:
        return "Raw coverage snapshot: R coverage, G affine presence, B rendered sprite index, A native presence. Alpha is data, not opacity.";
    case DebugView::Direct3D:
        return "Direct 3D texture sampled by the 2D resolve path on the main screen.";
    case DebugView::OverlayOperatorColor:
        return "Presentation overlay contribution after scaling. This is the black-endpoint result: A in final = A + K * underlay.";
    case DebugView::OverlayUnderlayWeight:
        return "Presentation overlay underlay weight after scaling. White means the enhanced underlay passes through; black means the overlay fully owns the pixel.";
    case DebugView::OverlayReconstructedNative:
        return "Native-resolution reconstruction from the overlay operator applied to native 3D.";
    case DebugView::OverlayReconstructionError:
        return "Difference between true native final and reconstructed native overlay result. Brighter pixels mean the operator model is failing there.";
    case DebugView::OverlayValidityConfidence:
        return "Native reconstruction confidence. White means the overlay operator reconstructs native truth closely; black means poor match.";
    case DebugView::OverlayOwnershipReason:
        return "Presentation overlay ownership/reason. Blue = enhanced underlay passes through, cyan = underlay plus added overlay color, magenta = overlay-owned/opaque, yellow = partial/translucent overlay, green = per-channel/tinted underlay response.";
    case DebugView::HybridSelector:
        return "Selector for the active presentation path. Conservative hybrid mode shows high-resolution/overlay/native selection. Strict high-resolution affine mode shows semantic layer ownership and fractional presentation coverage before ordered operands and masked output AA.";
    case DebugView::HybridCoverageMiss:
        return "Hybrid coverage miss debug. Red = high-resolution Direct3D coverage exists but the hybrid selector falls back to native presentation; magenta = selected high-resolution foreground; cyan = overlay assist.";
    case DebugView::HybridForegroundAlpha:
        return "Coverage for the active presentation path. Conservative hybrid mode shows Direct3D foreground alpha. Strict high-resolution affine mode shows effective post-MLAA enhanced text-BG selector coverage: white selected, black underlay, gray fractional.";
    case DebugView::HybridFinalSource:
        return "Hybrid final source debug. Cyan = overlay operator result, pink = hybrid foreground compositor texture, blue = high-resolution foreground-boundary 2D base, gray = native fallback.";
    case DebugView::SandwichLower2D:
        return "Candidate high-resolution 2D layer below Direct3D for a future sandwich compositor.";
    case DebugView::SandwichUpper2D:
        return "Candidate high-resolution 2D layer above Direct3D for a future sandwich compositor.";
    case DebugView::SandwichEligibility:
        return "Sandwich-compositor eligibility mask. Green/cyan mark clean sandwich candidates; other colors explain fallback classes.";
    case DebugView::OverlayEnhancedUnderlay:
        return "Enhanced underlay used by the presentation overlay final composite, normally high-resolution direct 3D.";
    case DebugView::OverlayTrueNativeFinal:
        return "Native final result generated with the overlay mode's selected native-stage 3D underlay. Use this to compare against overlay reconstruction.";
    case DebugView::OverlayFinalResult:
        return "Final per-engine presentation overlay result before the GL physical-screen final pass.";
    case DebugView::FinalTop:
        return "Completed top display at internal resolution, including widescreen, brightness, routing, sharpening, and LCD ghosting. Unused side padding is cropped. Window layout and OSD are excluded. The Screen selector is ignored.";
    case DebugView::FinalBottom:
        return "Completed bottom display at internal resolution, including widescreen, brightness, routing, sharpening, and LCD ghosting. Unused side padding is cropped. Window layout and OSD are excluded. The Screen selector is ignored.";
    case DebugView::MainVRAMDisplayRaw:
        return "Raw native-resolution CPU VRAM contents currently selected by main engine VRAM display, before high-resolution epoch replacement, native dirty-row overlay, final pass, screen swap, or brightness. The Screen selector is ignored.";
    case DebugView::MainVRAMDisplayRawBank0:
    case DebugView::MainVRAMDisplayRawBank1:
    case DebugView::MainVRAMDisplayRawBank2:
    case DebugView::MainVRAMDisplayRawBank3:
        return "Raw native-resolution CPU VRAM contents for the selected fixed bank, interpreted as a 256x192 VRAM display image. The Screen selector is ignored.";
    case DebugView::CaptureOutput256Bank0:
    case DebugView::CaptureOutput256Bank1:
    case DebugView::CaptureOutput256Bank2:
    case DebugView::CaptureOutput256Bank3:
        return "High-resolution 256x256 display-capture output layer for the selected fixed VRAM bank. This is the tracked GL capture buffer before any later consumer chooses a product. The Screen selector is ignored.";
    case DebugView::HighResDisplayCaptureFullBank0:
    case DebugView::HighResDisplayCaptureFullBank1:
    case DebugView::HighResDisplayCaptureFullBank2:
    case DebugView::HighResDisplayCaptureFullBank3:
        return "High-resolution full-equivalent display-capture product for the selected fixed VRAM bank. The Screen selector is ignored.";
    case DebugView::HighResDisplayCaptureBackgroundBank0:
    case DebugView::HighResDisplayCaptureBackgroundBank1:
    case DebugView::HighResDisplayCaptureBackgroundBank2:
    case DebugView::HighResDisplayCaptureBackgroundBank3:
        return "High-resolution background/3D-underlay display-capture product for the selected fixed VRAM bank. The Screen selector is ignored.";
    case DebugView::MainVRAMDisplayEpochBank0:
    case DebugView::MainVRAMDisplayEpochBank1:
    case DebugView::MainVRAMDisplayEpochBank2:
    case DebugView::MainVRAMDisplayEpochBank3:
        return "Persistent main-VRAM-display epoch texture for the selected fixed VRAM bank, before dirty-row native fallback overlay. The Screen selector is ignored.";
    }

    return QString();
}
}

WholeScene2DDebugDialog::WholeScene2DDebugDialog(QWidget* parent)
    : QDialog(parent)
    , mainWindow(static_cast<MainWindow*>(parent))
    , emuThread(mainWindow ? mainWindow->getEmuInstance()->getEmuThread() : nullptr)
    , refreshSnapshotPoisonSource3D(false)
    , refreshSnapshotPoisonNative3DResolve(false)
    , refreshSnapshotPoisonNative3DResolveAlpha(false)
    , pendingRefresh(true)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Whole-scene 2D Debug View");
    resize(820, 620);

    auto* mainLayout = new QVBoxLayout(this);
    auto* controlsLayout = new QVBoxLayout();
    auto* selectorLayout = new QHBoxLayout();
    auto* buttonLayout = new QHBoxLayout();
    auto* optionsLayout = new QHBoxLayout();

    cbScreen = new QComboBox(this);
    cbScreen->addItem("Main screen (A)", 0);
    cbScreen->addItem("Sub screen (B)", 1);
    cbScreen->setMinimumContentsLength(14);

    cbCategory = new QComboBox(this);
    cbView = new QComboBox(this);
    cbCategory->setMinimumContentsLength(20);
    cbView->setMinimumContentsLength(28);
    cbCategory->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cbView->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    for (const DebugViewSpec& spec : DebugViewSpecs())
    {
        const QString category = QString::fromUtf8(spec.Category);
        if (cbCategory->findText(category) < 0)
            cbCategory->addItem(category);
    }
    populateViewList();

    cbAutoRefresh = new QCheckBox("Auto-refresh", this);
    cbAutoRefresh->setChecked(false);
    cbAutoRefresh->setToolTip("Continuously reads the selected debug texture back from OpenGL. This can stall rendering.");

    cbPoisonSource3D = new QCheckBox("Poison source 3D", this);
    cbPoisonSource3D->setToolTip("Overwrite Parent.OutputTex3D before the high-res-to-native 3D resolve. Use this to verify what ResolveDirect3DToNative is reading.");

    cbPoisonNative3DResolve = new QCheckBox("Poison native 3D resolve", this);
    cbPoisonNative3DResolve->setToolTip("Overwrite NativeDirect3DTex RGB after ResolveDirect3DToNative while preserving its alpha. Use this to verify whether the native-stage compositor samples resolved 3D color.");

    cbPoisonNative3DResolveAlpha = new QCheckBox("Force resolve alpha", this);
    cbPoisonNative3DResolveAlpha->setToolTip("When poisoning NativeDirect3DTex, also force its alpha to opaque. Use this to isolate 3D coverage/alpha issues.");

    btnRefresh = new QPushButton("Refresh", this);
    btnCopy = new QPushButton("Copy", this);
    btnSave = new QPushButton("Save...", this);
    btnExportAll = new QPushButton("Export all...", this);
    btnCopy->setEnabled(false);
    btnSave->setEnabled(false);

    selectorLayout->addWidget(new QLabel("Screen:", this));
    selectorLayout->addWidget(cbScreen);
    selectorLayout->addSpacing(8);
    selectorLayout->addWidget(new QLabel("Category:", this));
    selectorLayout->addWidget(cbCategory);
    selectorLayout->addSpacing(8);
    selectorLayout->addWidget(new QLabel("View:", this));
    selectorLayout->addWidget(cbView);
    selectorLayout->addStretch(1);

    buttonLayout->addWidget(btnRefresh);
    buttonLayout->addWidget(btnCopy);
    buttonLayout->addWidget(btnSave);
    buttonLayout->addWidget(btnExportAll);
    buttonLayout->addStretch(1);

    optionsLayout->addWidget(cbAutoRefresh);
    optionsLayout->addWidget(cbPoisonSource3D);
    optionsLayout->addWidget(cbPoisonNative3DResolve);
    optionsLayout->addWidget(cbPoisonNative3DResolveAlpha);
    optionsLayout->addStretch(1);

    controlsLayout->addLayout(selectorLayout);
    controlsLayout->addLayout(optionsLayout);
    controlsLayout->addLayout(buttonLayout);

    lblDescription = new QLabel(this);
    lblDescription->setWordWrap(true);

    lblStatus = new QLabel(this);
    lblStatus->setWordWrap(true);
    lblStatus->setVisible(false);

    auto* lblDetails = new QLabel("Details:", this);
    txtDetails = new QPlainTextEdit(this);
    txtDetails->setReadOnly(true);
    txtDetails->setLineWrapMode(QPlainTextEdit::NoWrap);
    txtDetails->setMaximumHeight(170);
    txtDetails->setPlainText("No capture yet.");

    lblPreview = new QLabel(this);
    lblPreview->setAlignment(Qt::AlignCenter);
    lblPreview->setMinimumSize(320, 240);
    lblPreview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    lblPreview->setBackgroundRole(QPalette::Base);
    lblPreview->setAutoFillBackground(true);

    mainLayout->addLayout(controlsLayout);
    mainLayout->addWidget(lblDescription);
    mainLayout->addWidget(lblStatus);
    mainLayout->addWidget(lblDetails);
    mainLayout->addWidget(txtDetails);
    mainLayout->addWidget(lblPreview, 1);

    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(1000);

    connect(cbScreen, qOverload<int>(&QComboBox::currentIndexChanged), this, &WholeScene2DDebugDialog::selectSnapshotView);
    connect(cbCategory, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        populateViewList();
        selectSnapshotView();
    });
    connect(cbView, qOverload<int>(&QComboBox::currentIndexChanged), this, &WholeScene2DDebugDialog::selectSnapshotView);
    connect(cbAutoRefresh, &QCheckBox::toggled, this, &WholeScene2DDebugDialog::requestRefresh);
    connect(cbPoisonSource3D, &QCheckBox::toggled, this, &WholeScene2DDebugDialog::updateDebugPoison);
    connect(cbPoisonNative3DResolve, &QCheckBox::toggled, this, &WholeScene2DDebugDialog::updateDebugPoison);
    connect(cbPoisonNative3DResolveAlpha, &QCheckBox::toggled, this, &WholeScene2DDebugDialog::updateDebugPoison);
    connect(btnRefresh, &QPushButton::clicked, this, &WholeScene2DDebugDialog::updatePreview);
    connect(btnCopy, &QPushButton::clicked, this, &WholeScene2DDebugDialog::copyPreview);
    connect(btnSave, &QPushButton::clicked, this, &WholeScene2DDebugDialog::savePreview);
    connect(btnExportAll, &QPushButton::clicked, this, &WholeScene2DDebugDialog::exportAllViews);
    connect(refreshTimer, &QTimer::timeout, this, &WholeScene2DDebugDialog::onRefreshTimer);
    if (emuThread)
        connect(emuThread, &EmuThread::windowUpdate, this, &WholeScene2DDebugDialog::requestRefresh, Qt::QueuedConnection);

    refreshTimer->start();
    setRendererDebugViewsActive(true);
    requestRefresh();
}

WholeScene2DDebugDialog::~WholeScene2DDebugDialog()
{
    setRendererDebugViewsActive(false);
    closeDlg();
}

void WholeScene2DDebugDialog::closeEvent(QCloseEvent* event)
{
    setRendererDebugViewsActive(false);
    closeDlg();
    QDialog::closeEvent(event);
}

void WholeScene2DDebugDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updatePreviewPixmap();
}

void WholeScene2DDebugDialog::requestRefresh()
{
    pendingRefresh = true;
    lblDescription->setText(currentViewDescription());
}

void WholeScene2DDebugDialog::selectSnapshotView()
{
    pendingRefresh = true;
    lblDescription->setText(currentViewDescription());

    if (!displaySnapshotView())
        requestRefresh();
}

void WholeScene2DDebugDialog::onRefreshTimer()
{
    if (!cbAutoRefresh->isChecked() || !isVisible())
        return;

    if (emuThread && !emuThread->emuIsRunning())
        return;

    updatePreview();
}

void WholeScene2DDebugDialog::populateViewList()
{
    const QVariant previousView = cbView->currentData();
    const QString category = cbCategory->currentText();

    cbView->blockSignals(true);
    cbView->clear();
    for (const DebugViewSpec& spec : DebugViewSpecs())
    {
        if (category == QString::fromUtf8(spec.Category))
            cbView->addItem(QString::fromUtf8(spec.Label), static_cast<int>(spec.View));
    }

    const int previousIndex = previousView.isValid() ? cbView->findData(previousView) : -1;
    if (previousIndex >= 0)
        cbView->setCurrentIndex(previousIndex);
    else if (cbView->count() > 0)
        cbView->setCurrentIndex(0);
    cbView->blockSignals(false);
}

QString WholeScene2DDebugDialog::currentViewDescription() const
{
    auto view = static_cast<melonDS::WholeScene2DDebugView>(cbView->currentData().toInt());
    return DebugViewDescription(view);
}

QString WholeScene2DDebugDialog::currentViewLabel() const
{
    return cbView->currentText();
}

QString WholeScene2DDebugDialog::defaultExportFilename() const
{
    QString view = currentViewLabel().toLower();
    view.replace(' ', '-');
    view.replace('/', '-');
    view.replace('(', "");
    view.replace(')', "");
    const QString screen = (cbScreen->currentData().toInt() == 0) ? "main" : "sub";
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    return QString("whole-scene-2d-%1-%2-%3.png").arg(screen, view, stamp);
}

void WholeScene2DDebugDialog::setStatusText(const QString& text)
{
    lblStatus->setText(text);
    lblStatus->setVisible(!text.trimmed().isEmpty());
}

void WholeScene2DDebugDialog::setDetailsText(const QString& text)
{
    txtDetails->setPlainText(text);
}

void WholeScene2DDebugDialog::setRendererStatusText(const QString& text)
{
    const QString trimmed = text.trimmed();
    setStatusText(QString());
    setDetailsText(trimmed);
}

void WholeScene2DDebugDialog::setRendererDebugViewsActive(bool active)
{
    if (!mainWindow || !mainWindow->getEmuInstance())
        return;

    // Availability markers are renderer-owned along with the textures. Dialog
    // open/close must synchronize too, not just the later image readback.
    const bool borrow = emuThread && emuThread->isRunning();
    if (borrow) emuThread->borrowGL();
    auto* nds = mainWindow->getEmuInstance()->getNDS();
    if (nds)
        nds->GPU.GetRenderer().SetWholeScene2DDebugViewsActive(active);
    if (borrow) emuThread->returnGL();
}

bool WholeScene2DDebugDialog::captureRefreshSnapshot(QString* errorText)
{
    refreshSnapshot.clear();
    refreshSnapshotStamp.clear();
    refreshSnapshotScreenName.clear();

    if (!mainWindow || !mainWindow->getEmuInstance())
    {
        if (errorText)
            *errorText = "No active emulator window.";
        return false;
    }

    if (!mainWindow->hasOpenGL())
    {
        if (errorText)
            *errorText = "OpenGL is not active for this window.";
        return false;
    }

    auto* nds = mainWindow->getEmuInstance()->getNDS();
    if (!emuThread || !nds || !emuThread->emuIsActive())
    {
        if (errorText)
            *errorText = "The emulator is not actively rendering.";
        return false;
    }

    refreshSnapshotScreenName = "both";
    refreshSnapshotStamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    refreshSnapshotPoisonSource3D = cbPoisonSource3D->isChecked();
    refreshSnapshotPoisonNative3DResolve = cbPoisonNative3DResolve->isChecked();
    refreshSnapshotPoisonNative3DResolveAlpha = cbPoisonNative3DResolveAlpha->isChecked();

    emuThread->borrowGL();
    nds->GPU.GetRenderer().SetWholeScene2DDebugPoison(refreshSnapshotPoisonSource3D,
                                                      refreshSnapshotPoisonNative3DResolve,
                                                      refreshSnapshotPoisonNative3DResolveAlpha);
    if (emuThread->emuIsRunning())
        emuThread->borrowNextFrameGL();

    mainWindow->makeCurrentGL();

    refreshSnapshot = captureViews(nds->GPU.GetRenderer(), mainWindow);

    mainWindow->releaseGL();
    emuThread->returnGL();
    return true;
}

std::vector<WholeScene2DDebugDialog::CapturedView> WholeScene2DDebugDialog::captureViews(melonDS::Renderer& renderer, MainWindow* window)
{
    using DebugView = melonDS::WholeScene2DDebugView;
    std::vector<CapturedView> snapshot;
    // Capture once for both engine selectors. Final views are physical displays,
    // with presentation effects applied by the same shader as the live window.
    std::array<QImage, 2> finalDisplays;
    QString finalError;
    const bool finalAvailable = window->captureFinalDisplays(finalDisplays, &finalError);
    melonDS::WholeScene2DDebugReadContext context;
    const struct
    {
        int Screen;
        const char* Name;
    } screens[] = {
        {0, "main"},
        {1, "sub"},
    };

    for (const auto& screen : screens)
    {
        int viewIndex = 0;
        for (const DebugViewSpec& spec : DebugViewSpecs())
        {
            viewIndex++;

            CapturedView captured;
            captured.Index = viewIndex;
            captured.Screen = screen.Screen;
            captured.ScreenName = QString::fromUtf8(screen.Name);
            captured.ViewValue = static_cast<int>(spec.View);
            captured.Category = QString::fromUtf8(spec.Category);
            captured.Label = QString::fromUtf8(spec.Label);
            captured.FileStem = QString("%1-%2-%3-%4")
                                    .arg(captured.ScreenName)
                                    .arg(viewIndex, 2, 10, QChar('0'))
                                    .arg(SafeExportName(captured.Category))
                                    .arg(SafeExportName(captured.Label));

            QElapsedTimer readTimer;
            readTimer.start();
            if (spec.View == DebugView::FinalTop || spec.View == DebugView::FinalBottom)
            {
                const int display = spec.View == DebugView::FinalTop ? 0 : 1;
                captured.Available = finalAvailable;
                captured.Image = finalDisplays[display];
                captured.Width = captured.Image.width();
                captured.Height = captured.Image.height();
                captured.Status = finalAvailable
                    ? QString("Completed %1 display, including presentation effects. Internal resolution; side padding cropped; no window layout or OSD.")
                        .arg(display == 0 ? "top" : "bottom")
                    : finalError;
                snapshot.push_back(std::move(captured));
                continue;
            }
            std::vector<melonDS::u32> pixels;
            std::string status;
            const bool ok = renderer.ReadWholeScene2DDebugView(screen.Screen,
                                                                             spec.View,
                                                                             captured.Width,
                                                                             captured.Height,
                                                                             pixels,
                                                                             &status, &context);
            captured.Status = QString::fromStdString(status);
            if (ok && captured.Width > 0 && captured.Height > 0 && !pixels.empty())
            {
                QImage image(reinterpret_cast<const uchar*>(pixels.data()),
                             captured.Width, captured.Height,
                             QImage::Format_RGBA8888);
                captured.Image = image.copy();
                captured.Available = true;
            }

            captured.ReadMilliseconds = readTimer.elapsed();
            snapshot.push_back(std::move(captured));
        }
    }

    return snapshot;
}

bool WholeScene2DDebugDialog::dumpCurrentFrame(MainWindow* parent,
    const QString& timingCsvPath, qulonglong timingFrame,
    QString* exportPath, QString* errorText)
{
    return dumpFrame(parent, timingCsvPath, timingFrame, true, exportPath, errorText);
}

bool WholeScene2DDebugDialog::dumpRollingFrames(MainWindow* parent,
    const QString& timingCsvPath, qulonglong timingFrame,
    QString* exportPath, QString* errorText)
{
    return dumpFrame(parent, timingCsvPath, timingFrame, false, exportPath, errorText);
}

bool WholeScene2DDebugDialog::dumpFrame(MainWindow* parent,
    const QString& timingCsvPath, qulonglong timingFrame, bool includeViews,
    QString* exportPath, QString* errorText)
{
    if (exportPath)
        exportPath->clear();
    if (errorText)
        errorText->clear();

    if (!parent || !parent->getEmuInstance())
    {
        if (errorText)
            *errorText = "No active emulator window.";
        return false;
    }

    if (!parent->hasOpenGL())
    {
        if (errorText)
            *errorText = "OpenGL is not active for this window.";
        return false;
    }

    EmuThread* thread = parent->getEmuInstance()->getEmuThread();
    auto* nds = parent->getEmuInstance()->getNDS();
    if (!thread || !nds || !thread->emuIsActive())
    {
        if (errorText)
            *errorText = "The emulator is not actively rendering.";
        return false;
    }

    const QFileInfo csvInfo(timingCsvPath);
    if (timingCsvPath.isEmpty() || csvInfo.absolutePath().isEmpty() || csvInfo.completeBaseName().isEmpty())
    {
        if (errorText)
            *errorText = "No active whole-scene timing CSV path.";
        return false;
    }

    WholeSceneDebugExport::Job job;
    if (!job)
    {
        if (errorText) *errorText = "A debug export is already in progress.";
        return false;
    }
    WholeSceneDebugExport::Snapshot snapshot;
    snapshot.ViewsInSubdirectory = true;
    snapshot.IncludeFinalEvidence = true;
    QElapsedTimer elapsed;
    elapsed.start();
    const bool shouldDisableDebugViews = includeViews && currentDlg == nullptr;
    const bool poisonSource3D = currentDlg && currentDlg->cbPoisonSource3D->isChecked();
    const bool poisonNative3DResolve = currentDlg && currentDlg->cbPoisonNative3DResolve->isChecked();
    const bool poisonNative3DResolveAlpha = currentDlg && currentDlg->cbPoisonNative3DResolveAlpha->isChecked();
    thread->borrowGL();
    if (includeViews)
    {
        nds->GPU.GetRenderer().SetWholeScene2DDebugViewsActive(true);
        nds->GPU.GetRenderer().SetWholeScene2DDebugPoison(
            poisonSource3D, poisonNative3DResolve, poisonNative3DResolveAlpha);
        if (thread->emuIsRunning())
            thread->borrowNextFrameGL();
    }
    const qint64 prepareMs = elapsed.elapsed();
    std::string currentStatus, rollingStatus;
    parent->makeCurrentGL();
    snapshot.CurrentFinalAvailable = nds->GPU.GetRenderer()
        .ReadWholeScene2DCurrentFinalDebugFrame(snapshot.CurrentFinal, &currentStatus);
    const qint64 currentMs = elapsed.elapsed() - prepareMs;
    const bool rollingAvailable = nds->GPU.GetRenderer()
        .ReadWholeScene2DRollingDebugFrames(snapshot.RollingFrames, &rollingStatus);
    const qint64 rollingMs = elapsed.elapsed() - prepareMs - currentMs;
    if (includeViews)
        snapshot.Views = captureViews(nds->GPU.GetRenderer(), parent);
    if (shouldDisableDebugViews)
        nds->GPU.GetRenderer().SetWholeScene2DDebugViewsActive(false);
    parent->releaseGL();
    thread->returnGL();
    const qint64 viewsMs = elapsed.elapsed() - prepareMs - currentMs - rollingMs;
    if (!includeViews && !rollingAvailable)
    {
        if (errorText) *errorText = QString::fromStdString(rollingStatus);
        return false;
    }
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
    // Name the frame actually captured, preserving the hotkey's earlier frame
    // separately. Debug preparation can advance emulation after the request.
    const auto& current = snapshot.CurrentFinal;
    const qulonglong capturedFrame = snapshot.CurrentFinalAvailable && current.TimingFrameValid
        ? current.TimingFrame : timingFrame;
    const QString name = QString("%1frame%2-%3").arg(includeViews ? "" : "rolling-")
        .arg(capturedFrame, 6, 10, QChar('0')).arg(stamp);
    snapshot.Directory = QDir(csvInfo.absolutePath()).filePath(
        csvInfo.completeBaseName() + "-captures/" + name);
    QTextStream text(&snapshot.Header);
    text << (includeViews ? "Whole-scene 2D hotkey debug dump\n" : "Whole-scene 2D rolling debug dump\n")
         << "Timestamp: " << stamp << "\nTiming CSV: " << timingCsvPath
         << "\nTiming frame: " << capturedFrame << "\nRequested timing frame: " << timingFrame
         << "\nScreens: main, sub\n"
         << "Poison source 3D: " << (poisonSource3D ? "yes" : "no")
         << "\nPoison native 3D resolve: " << (poisonNative3DResolve ? "yes" : "no")
         << "\nForce native 3D resolve alpha: " << (poisonNative3DResolveAlpha ? "yes" : "no")
         << "\nSource: " << (includeViews ? "hotkey current-frame capture" : "hotkey rolling capture")
         << "\nCurrent final capture: " << QString::fromStdString(currentStatus)
         << "\nRolling final capture: " << QString::fromStdString(rollingStatus)
         << "\nCapture timings (CPU wall ms; readbacks may include GPU waits):\n"
         << "  Debug preparation/frame handoff: " << prepareMs
         << "\n  Current final: " << currentMs
         << "\n  Rolling final: " << rollingMs
         << "\n  Views/read/decode: " << viewsMs << "\n";
    text.flush();
    if (exportPath) *exportPath = snapshot.Directory;
    job.start(std::move(snapshot), parent, [parent](const WholeSceneDebugExport::Result& result) {
        if (auto* instance = parent->getEmuInstance())
            instance->osdAddMessage(0, "%s", result.Message.toUtf8().constData());
    });
    return true; // Accepted for background writing, not a claim of successful saving.
}

const WholeScene2DDebugDialog::CapturedView* WholeScene2DDebugDialog::findSnapshotView(int screen, int viewValue) const
{
    for (const CapturedView& captured : refreshSnapshot)
    {
        if (captured.Screen == screen && captured.ViewValue == viewValue)
            return &captured;
    }

    return nullptr;
}

bool WholeScene2DDebugDialog::displaySnapshotView(const QString& extraStatus)
{
    if (refreshSnapshot.empty())
        return false;

    const int screen = cbScreen->currentData().toInt();
    const int viewValue = cbView->currentData().toInt();
    const CapturedView* selected = findSnapshotView(screen, viewValue);
    QString status = selected ? selected->Status : QString("The selected debug view was not captured.");
    status += "\n\nDebug dialog refresh:";
    status += "\n  Snapshot views captured: ";
    status += QString::number(refreshSnapshot.size());
    status += "\n  Snapshot screens captured: main, sub";
    status += "\n  Selected screen: ";
    status += (screen == 0) ? "main" : "sub";
    if (!extraStatus.isEmpty())
        status += extraStatus;
    status += "\n  Export source: last refreshed snapshot";

    setRendererStatusText(status);

    if (!selected || !selected->Available || selected->Image.isNull())
    {
        currentImage = QImage();
        btnCopy->setEnabled(false);
        btnSave->setEnabled(false);
        lblPreview->clear();
        lblPreview->setText("No debug image available for the current selection.");
        return true;
    }

    currentImage = selected->Image;
    btnCopy->setEnabled(true);
    btnSave->setEnabled(true);
    updatePreviewPixmap();
    return true;
}

void WholeScene2DDebugDialog::updateDebugPoison()
{
    if (!mainWindow || !mainWindow->getEmuInstance() || !emuThread)
    {
        setStatusText("Debug poison controls are unavailable without an active emulator window.");
        return;
    }

    auto* nds = mainWindow->getEmuInstance()->getNDS();
    if (!nds)
    {
        setStatusText("Debug poison controls are unavailable without an active emulator.");
        return;
    }

    std::string status;
    emuThread->borrowGL();
    mainWindow->makeCurrentGL();
    nds->GPU.GetRenderer().SetWholeScene2DDebugPoison(cbPoisonSource3D->isChecked(),
                                                      cbPoisonNative3DResolve->isChecked(),
                                                      cbPoisonNative3DResolveAlpha->isChecked(),
                                                      &status);
    mainWindow->releaseGL();
    emuThread->returnGL();

    setStatusText(QString::fromStdString(status));
    requestRefresh();
}

void WholeScene2DDebugDialog::updatePreview()
{
    pendingRefresh = false;
    lblDescription->setText(currentViewDescription());

    if (!mainWindow || !mainWindow->getEmuInstance())
    {
        currentImage = QImage();
        btnCopy->setEnabled(false);
        btnSave->setEnabled(false);
        lblPreview->clear();
        lblPreview->setText("No active emulator window.");
        setRendererStatusText("Unavailable.");
        return;
    }

    if (!mainWindow->hasOpenGL())
    {
        currentImage = QImage();
        btnCopy->setEnabled(false);
        btnSave->setEnabled(false);
        lblPreview->clear();
        lblPreview->setText("OpenGL is not active for this window.");
        setRendererStatusText("Unavailable.");
        return;
    }

    auto* nds = mainWindow->getEmuInstance()->getNDS();
    if (!emuThread || !nds || !emuThread->emuIsActive())
    {
        currentImage = QImage();
        btnCopy->setEnabled(false);
        btnSave->setEnabled(false);
        lblPreview->clear();
        lblPreview->setText("The emulator is not actively rendering.");
        setRendererStatusText("Unavailable.");
        return;
    }

    QElapsedTimer readbackTimer;
    readbackTimer.start();

    QString errorText;
    if (!captureRefreshSnapshot(&errorText))
    {
        currentImage = QImage();
        btnCopy->setEnabled(false);
        btnSave->setEnabled(false);
        lblPreview->clear();
        lblPreview->setText(errorText);
        setRendererStatusText("Unavailable.");
        return;
    }

    QString extraStatus;
    extraStatus += "\n  GL readback wall time: ";
    extraStatus += QString::number(readbackTimer.nsecsElapsed() / 1000);
    extraStatus += " us";
    displaySnapshotView(extraStatus);
}

void WholeScene2DDebugDialog::copyPreview()
{
    if (currentImage.isNull())
        return;

    if (QClipboard* clipboard = QGuiApplication::clipboard())
    {
        clipboard->setImage(currentImage);
        setStatusText(QString("Copied %1 (%2x%3) to the clipboard.")
                          .arg(currentViewLabel())
                          .arg(currentImage.width())
                          .arg(currentImage.height()));
    }
}

void WholeScene2DDebugDialog::savePreview()
{
    if (currentImage.isNull())
        return;

    const QString path = QFileDialog::getSaveFileName(this,
                                                      "Save whole-scene 2D debug image",
                                                      defaultExportFilename(),
                                                      "PNG image (*.png)");
    if (path.isEmpty())
        return;

    if (currentImage.save(path, "PNG"))
        setStatusText(QString("Saved %1").arg(path));
    else
        setStatusText(QString("Failed to save %1").arg(path));
}

void WholeScene2DDebugDialog::exportAllViews()
{
    if (refreshSnapshot.empty())
    {
        setStatusText("Refresh a frame before exporting all views.");
        return;
    }
    WholeSceneDebugExport::Job job;
    if (!job)
    {
        setStatusText("A debug export is already in progress.");
        return;
    }
    const QString basePath = QFileDialog::getExistingDirectory(this, "Export Whole-Scene Debug Views");
    if (basePath.isEmpty()) return;
    WholeSceneDebugExport::Snapshot snapshot;
    snapshot.Directory = QDir(basePath).filePath(QString("whole-scene-2d-%1-%2")
        .arg(refreshSnapshotScreenName).arg(refreshSnapshotStamp));
    snapshot.Views = refreshSnapshot; // QImages share immutable storage with the preview.
    QTextStream text(&snapshot.Header);
    text << "Whole-scene 2D debug export\nTimestamp: " << refreshSnapshotStamp
         << "\nScreens: main, sub\nPoison source 3D: " << (refreshSnapshotPoisonSource3D ? "yes" : "no")
         << "\nPoison native 3D resolve: " << (refreshSnapshotPoisonNative3DResolve ? "yes" : "no")
         << "\nForce native 3D resolve alpha: " << (refreshSnapshotPoisonNative3DResolveAlpha ? "yes" : "no")
         << "\nSource: last refreshed snapshot\n";
    text.flush();
    job.start(std::move(snapshot), this, [this](const WholeSceneDebugExport::Result& result) {
        setStatusText(result.Message);
    });
    setStatusText("Saving debug export in the background...");
}

void WholeScene2DDebugDialog::updatePreviewPixmap()
{
    if (currentImage.isNull())
    {
        lblPreview->clear();
        return;
    }

    lblPreview->setText(QString());
    const QPixmap pixmap = QPixmap::fromImage(currentImage).scaled(lblPreview->size(), Qt::KeepAspectRatio, Qt::FastTransformation);
    lblPreview->setPixmap(pixmap);
}
