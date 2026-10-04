// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WholeScene2DDebugExport.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QTextStream>
#include <QThread>
#include <exception>
#include <memory>
#include <utility>
namespace
{
bool SaveFinalDebugFrame(const melonDS::WholeScene2DFinalDebugFrame& frame,
                         const QDir& dir,
                         const QString& stem,
                         QTextStream& manifestText,
                         int& savedCount,
                         int& failedCount)
{
    if (frame.Width <= 0 ||
        frame.Height <= 0 ||
        frame.TopRGBA.size() != static_cast<size_t>(frame.Width) * frame.Height ||
        frame.BottomRGBA.size() != static_cast<size_t>(frame.Width) * frame.Height)
    {
        failedCount += 2;
        manifestText << "  " << stem << ": unavailable or incomplete pixel data\n";
        return false;
    }

    const QString topName = stem + "-top.png";
    const QString bottomName = stem + "-bottom.png";

    QImage topImage(reinterpret_cast<const uchar*>(frame.TopRGBA.data()),
                    frame.Width,
                    frame.Height,
                    QImage::Format_RGBA8888);
    QImage bottomImage(reinterpret_cast<const uchar*>(frame.BottomRGBA.data()),
                       frame.Width,
                       frame.Height,
                       QImage::Format_RGBA8888);

    const bool topSaved = topImage.save(dir.filePath(topName), "PNG");
    const bool bottomSaved = bottomImage.save(dir.filePath(bottomName), "PNG");
    savedCount += topSaved ? 1 : 0;
    savedCount += bottomSaved ? 1 : 0;
    failedCount += topSaved ? 0 : 1;
    failedCount += bottomSaved ? 0 : 1;

    manifestText << "  " << stem << "\n";
    manifestText << "    Serial: " << frame.Serial << "\n";
    if (frame.TimingFrameValid)
        manifestText << "    Timing frame: " << frame.TimingFrame << "\n";
    else
        manifestText << "    Timing frame: unavailable\n";
    manifestText << "    Size: " << frame.Width << "x" << frame.Height << "\n";
    manifestText << "    Final sources: top=" << frame.FinalTopSource
                 << " bottom=" << frame.FinalBottomSource << "\n";
    const auto writeEngine = [&manifestText](const char* label,
                                             const melonDS::WholeScene2DEngineDebugIdentity& identity)
    {
        manifestText << "    " << label
                     << ": path=" << identity.Path
                     << " product_kind=" << identity.ChosenProductKind
                     << " render_action=" << identity.ChosenProductRenderAction
                     << " tex=" << identity.ChosenProductTex
                     << " bank=" << identity.ChosenProductCaptureBank
                     << " bg_epoch=" << identity.ChosenProductBackgroundEpochSerial
                     << " source_3d=" << identity.ChosenProductSource3DSerial
                     << " event=" << identity.ChosenProductCaptureEventSerial << "\n";
        manifestText << "    " << label
                     << " hashes: request_capture=" << identity.RequestCapturePresentationHash
                     << " request_current=" << identity.RequestCurrentPresentationHash
                     << " chosen_capture=" << identity.ChosenProductCapturePresentationHash
                     << " chosen_current=" << identity.ChosenProductCurrentPresentationHash << "\n";
    };
    writeEngine("Engine A", frame.EngineA);
    writeEngine("Engine B", frame.EngineB);
    manifestText << "    Affine OBJ: engine_a="
                 << frame.EngineAAffineOBJ.Summary.Count
                 << " engine_b=" << frame.EngineBAffineOBJ.Summary.Count
                 << "\n";
    manifestText << "    Top: " << (topSaved ? "saved " + topName : "failed") << "\n";
    manifestText << "    Bottom: " << (bottomSaved ? "saved " + bottomName : "failed") << "\n";
    return topSaved && bottomSaved;
}

bool SaveAffineOBJDebugEvidence(
    const QDir& dir,
    const melonDS::WholeScene2DFinalDebugFrame* currentFrame,
    const std::vector<melonDS::WholeScene2DFinalDebugFrame>& rollingFrames,
    QTextStream& manifestText)
{
    QFile summaryFile(dir.filePath("affine-obj-summary.csv"));
    QFile spriteFile(dir.filePath("affine-obj-sprites.csv"));
    QFile groupFile(dir.filePath("affine-obj-groups.csv"));
    QFile bandSummaryFile(dir.filePath("ordinary-obj-band-summary.csv"));
    QFile bandMemberFile(dir.filePath("ordinary-obj-band-members.csv"));
    QFile bandFile(dir.filePath("ordinary-obj-bands.csv"));
    if (!summaryFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !spriteFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !groupFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !bandSummaryFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !bandMemberFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !bandFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        manifestText << "Affine OBJ evidence: failed to create CSV sidecars\n\n";
        return false;
    }

    QTextStream summaryText(&summaryFile);
    QTextStream spriteText(&spriteFile);
    QTextStream groupText(&groupFile);
    QTextStream bandSummaryText(&bandSummaryFile);
    QTextStream bandMemberText(&bandMemberFile);
    QTextStream bandText(&bandFile);
    summaryText
        << "schema_version,scope,serial,timing_frame_valid,timing_frame,engine,"
           "count,transformed_count,double_size_count,semitransparent_count,"
           "bitmap_count,mosaic_count,distinct_rotscale_count,"
           "shared_rotscale_sprite_count,singleton_group_count,"
           "shared_group_count,singleton_cache_control_ready_count,"
           "structural_multisprite_candidate_group_count,"
           "rejected_shared_group_count,touching_pairs,overlapping_pairs,"
           "same_rotscale_touching_pairs,same_rotscale_overlapping_pairs,"
           "same_state_touching_pairs,same_state_overlapping_pairs,"
           "min_x,min_y,max_x,max_y,obj_mode_mask,source_type_mask,"
           "priority_mask,source_generation,source_hash,transform_hash,"
           "group_source_hash,canonical_group_layout_hash\n";
    spriteText
        << "schema_version,scope,serial,timing_frame_valid,timing_frame,engine,"
           "rendered_index,oam_index,attr0,attr1,attr2,position_x,position_y,"
           "width,height,bound_width,bound_height,rotscale_index,pa,pb,pc,pd,"
           "obj_mode,source_type,palette_offset,tile_offset,tile_stride,priority,"
           "mosaic,double_size,identity_transform,enhanced_cache_valid,"
           "source_generation,enhanced_algorithm,enhanced_source_scale,"
           "candidate_group_member_index,candidate_group_member_count,"
           "candidate_group_anchor_oam_index,"
           "candidate_group_source_offset_x_512,"
           "candidate_group_source_offset_y_512\n";
    groupText
        << "schema_version,scope,serial,timing_frame_valid,timing_frame,engine,"
           "rotscale_index,anchor_rendered_index,anchor_oam_index,member_count,"
           "pa,pb,pc,pd,member_oam_mask_low,member_oam_mask_high,"
           "touching_pairs,overlapping_pairs,min_x,min_y,max_x,max_y,"
           "all_normal_mode,all_supported_source,no_mosaic,same_mode,"
           "same_source_type,same_priority,all_enhanced_caches_valid,"
           "singleton_cache_control_ready,structural_multisprite_candidate,"
           "requires_temporal_layout_proof,rejection_mask,source_hash,"
           "canonical_layout_hash\n";
    bandSummaryText
        << "schema_version,scope,serial,timing_frame_valid,timing_frame,engine,"
           "input_count,affine_count,ordinary_count,supported_ordinary_count,"
           "rejected_ordinary_count,band_count,max_band_member_count,"
           "split_affine_group_member_count,band_limit,recipe_ready,"
           "rejection_mask,blocking_rejection_mask,affine_oam_mask_low,affine_oam_mask_high,"
           "affine_bg_mask,"
           "classification_hash\n";
    bandMemberText
        << "schema_version,scope,serial,timing_frame_valid,timing_frame,engine,"
           "rendered_index,oam_index,total_priority,band_index,priority,"
           "obj_mode,source_type,mosaic,position_x,position_y,bound_width,"
           "bound_height,affine_in_front_oam_mask_low,"
           "affine_in_front_oam_mask_high,affine_behind_oam_mask_low,"
           "affine_behind_oam_mask_high,preserved_in_front_oam_mask_low,"
           "preserved_in_front_oam_mask_high,preserved_behind_oam_mask_low,"
           "preserved_behind_oam_mask_high,affine_bg_in_front_mask,"
           "affine_bg_behind_mask,rejection_mask\n";
    bandText
        << "schema_version,scope,serial,timing_frame_valid,timing_frame,engine,"
           "band_index,member_count,min_total_priority,max_total_priority,"
           "member_oam_mask_low,member_oam_mask_high,"
           "affine_in_front_oam_mask_low,affine_in_front_oam_mask_high,"
           "affine_behind_oam_mask_low,affine_behind_oam_mask_high,"
           "preserved_in_front_oam_mask_low,preserved_in_front_oam_mask_high,"
           "preserved_behind_oam_mask_low,preserved_behind_oam_mask_high,"
           "affine_bg_in_front_mask,affine_bg_behind_mask,"
           "relation_hash\n";

    int summaryRows = 0;
    int spriteRows = 0;
    int groupRows = 0;
    int bandSummaryRows = 0;
    int bandMemberRows = 0;
    int bandRows = 0;
    const auto writeEvidence = [&](const QString& scope,
                                   const melonDS::WholeScene2DFinalDebugFrame& frame,
                                   const char* engine,
                                   const melonDS::WholeScene2DAffineOBJDebugEvidence& evidence)
    {
        const auto& summary = evidence.Summary;
        summaryText << 3 << ',' << scope << ',' << frame.Serial << ','
                    << (frame.TimingFrameValid ? 1 : 0) << ',' << frame.TimingFrame
                    << ',' << engine << ',' << summary.Count << ','
                    << summary.TransformedCount << ',' << summary.DoubleSizeCount
                    << ',' << summary.SemiTransparentCount << ','
                    << summary.BitmapCount << ',' << summary.MosaicCount << ','
                    << summary.DistinctRotscaleCount << ','
                    << summary.SharedRotscaleSpriteCount << ','
                    << summary.SingletonGroupCount << ','
                    << summary.SharedGroupCount << ','
                    << summary.SingletonCacheControlReadyCount << ','
                    << summary.StructuralMultiSpriteCandidateGroupCount << ','
                    << summary.RejectedSharedGroupCount << ','
                    << summary.TouchingPairs << ',' << summary.OverlappingPairs
                    << ',' << summary.SameRotscaleTouchingPairs << ','
                    << summary.SameRotscaleOverlappingPairs << ','
                    << summary.SameStateTouchingPairs << ','
                    << summary.SameStateOverlappingPairs << ',' << summary.MinX
                    << ',' << summary.MinY << ',' << summary.MaxX << ','
                    << summary.MaxY << ',' << summary.OBJModeMask << ','
                    << summary.SourceTypeMask << ',' << summary.PriorityMask
                    << ',' << summary.SourceGeneration << ',' << summary.SourceHash
                    << ',' << summary.TransformHash << ','
                    << summary.GroupSourceHash << ','
                    << summary.CanonicalGroupLayoutHash << '\n';
        summaryRows++;

        for (const auto& sprite : evidence.Records)
        {
            spriteText << 3 << ',' << scope << ',' << frame.Serial << ','
                       << (frame.TimingFrameValid ? 1 : 0) << ','
                       << frame.TimingFrame << ',' << engine << ','
                       << sprite.RenderedIndex << ',' << sprite.OAMIndex << ','
                       << sprite.Attr0 << ',' << sprite.Attr1 << ',' << sprite.Attr2
                       << ',' << sprite.PositionX << ',' << sprite.PositionY << ','
                       << sprite.Width << ',' << sprite.Height << ','
                       << sprite.BoundWidth << ',' << sprite.BoundHeight << ','
                       << sprite.RotscaleIndex << ',' << sprite.Rotscale[0] << ','
                       << sprite.Rotscale[1] << ',' << sprite.Rotscale[2] << ','
                       << sprite.Rotscale[3] << ',' << sprite.OBJMode << ','
                       << sprite.SourceType << ',' << sprite.PaletteOffset << ','
                       << sprite.TileOffset << ',' << sprite.TileStride << ','
                       << sprite.Priority << ',' << (sprite.Mosaic ? 1 : 0) << ','
                       << (sprite.DoubleSize ? 1 : 0) << ','
                       << (sprite.IdentityTransform ? 1 : 0) << ','
                       << (sprite.EnhancedSourceCacheValid ? 1 : 0) << ','
                       << sprite.SourceGeneration << ',' << sprite.EnhancedAlgorithm
                       << ',' << sprite.EnhancedSourceScale << ','
                       << sprite.CandidateGroupMemberIndex << ','
                       << sprite.CandidateGroupMemberCount << ','
                       << sprite.CandidateGroupAnchorOAMIndex << ','
                       << sprite.CandidateGroupSourceOffsetX512 << ','
                       << sprite.CandidateGroupSourceOffsetY512 << '\n';
            spriteRows++;
        }

        for (const auto& group : evidence.Groups)
        {
            groupText << 3 << ',' << scope << ',' << frame.Serial << ','
                      << (frame.TimingFrameValid ? 1 : 0) << ','
                      << frame.TimingFrame << ',' << engine << ','
                      << group.RotscaleIndex << ','
                      << group.AnchorRenderedIndex << ',' << group.AnchorOAMIndex
                      << ',' << group.MemberCount << ',' << group.Rotscale[0]
                      << ',' << group.Rotscale[1] << ',' << group.Rotscale[2]
                      << ',' << group.Rotscale[3] << ','
                      << group.MemberOAMMask[0] << ',' << group.MemberOAMMask[1]
                      << ',' << group.TouchingPairs << ','
                      << group.OverlappingPairs << ',' << group.MinX << ','
                      << group.MinY << ',' << group.MaxX << ',' << group.MaxY
                      << ',' << (group.AllNormalMode ? 1 : 0) << ','
                      << (group.AllSupportedSource ? 1 : 0) << ','
                      << (group.NoMosaic ? 1 : 0) << ','
                      << (group.SameMode ? 1 : 0) << ','
                      << (group.SameSourceType ? 1 : 0) << ','
                      << (group.SamePriority ? 1 : 0) << ','
                      << (group.AllEnhancedCachesValid ? 1 : 0) << ','
                      << (group.SingletonCacheControlReady ? 1 : 0) << ','
                      << (group.StructuralMultiSpriteCandidate ? 1 : 0) << ','
                      << (group.RequiresTemporalLayoutProof ? 1 : 0) << ','
                      << group.RejectionMask << ',' << group.SourceHash << ','
                      << group.CanonicalLayoutHash << '\n';
            groupRows++;
        }

        const auto& bandSummary = evidence.OrdinaryBands.Summary;
        bandSummaryText << 3 << ',' << scope << ',' << frame.Serial << ','
                        << (frame.TimingFrameValid ? 1 : 0) << ','
                        << frame.TimingFrame << ',' << engine << ','
                        << bandSummary.InputCount << ','
                        << bandSummary.AffineCount << ','
                        << bandSummary.OrdinaryCount << ','
                        << bandSummary.SupportedOrdinaryCount << ','
                        << bandSummary.RejectedOrdinaryCount << ','
                        << bandSummary.BandCount << ','
                        << bandSummary.MaxBandMemberCount << ','
                        << bandSummary.SplitAffineGroupMemberCount << ','
                        << bandSummary.BandLimit << ','
                        << (bandSummary.RecipeReady ? 1 : 0) << ','
                        << bandSummary.RejectionMask << ','
                        << bandSummary.BlockingRejectionMask << ','
                        << bandSummary.AffineOAMMask[0] << ','
                        << bandSummary.AffineOAMMask[1] << ','
                        << bandSummary.AffineBGMask << ','
                        << bandSummary.ClassificationHash << '\n';
        bandSummaryRows++;

        for (const auto& member : evidence.OrdinaryBands.Members)
        {
            bandMemberText << 4 << ',' << scope << ',' << frame.Serial << ','
                           << (frame.TimingFrameValid ? 1 : 0) << ','
                           << frame.TimingFrame << ',' << engine << ','
                           << member.RenderedIndex << ',' << member.OAMIndex
                           << ',' << member.TotalPriority << ','
                           << member.BandIndex << ',' << member.Priority << ','
                           << member.OBJMode << ',' << member.SourceType << ','
                           << (member.Mosaic ? 1 : 0) << ','
                           << member.PositionX << ',' << member.PositionY << ','
                           << member.BoundWidth << ',' << member.BoundHeight << ','
                           << member.AffineInFrontOAMMask[0] << ','
                           << member.AffineInFrontOAMMask[1] << ','
                           << member.AffineBehindOAMMask[0] << ','
                           << member.AffineBehindOAMMask[1] << ','
                           << member.PreservedInFrontOAMMask[0] << ','
                           << member.PreservedInFrontOAMMask[1] << ','
                           << member.PreservedBehindOAMMask[0] << ','
                           << member.PreservedBehindOAMMask[1] << ','
                           << member.AffineBGInFrontMask << ','
                           << member.AffineBGBehindMask << ','
                           << member.RejectionMask << '\n';
            bandMemberRows++;
        }

        for (const auto& band : evidence.OrdinaryBands.Bands)
        {
            bandText << 3 << ',' << scope << ',' << frame.Serial << ','
                     << (frame.TimingFrameValid ? 1 : 0) << ','
                     << frame.TimingFrame << ',' << engine << ','
                     << band.BandIndex << ',' << band.MemberCount << ','
                     << band.MinTotalPriority << ',' << band.MaxTotalPriority
                     << ',' << band.MemberOAMMask[0] << ','
                     << band.MemberOAMMask[1] << ','
                     << band.AffineInFrontOAMMask[0] << ','
                     << band.AffineInFrontOAMMask[1] << ','
                     << band.AffineBehindOAMMask[0] << ','
                     << band.AffineBehindOAMMask[1] << ','
                     << band.PreservedInFrontOAMMask[0] << ','
                     << band.PreservedInFrontOAMMask[1] << ','
                     << band.PreservedBehindOAMMask[0] << ','
                     << band.PreservedBehindOAMMask[1] << ','
                     << band.AffineBGInFrontMask << ','
                     << band.AffineBGBehindMask << ','
                     << band.RelationHash << '\n';
            bandRows++;
        }
    };
    const auto writeFrame = [&](const QString& scope,
                                const melonDS::WholeScene2DFinalDebugFrame& frame)
    {
        writeEvidence(scope, frame, "a", frame.EngineAAffineOBJ);
        writeEvidence(scope, frame, "b", frame.EngineBAffineOBJ);
    };

    if (currentFrame)
        writeFrame("current", *currentFrame);
    for (const auto& frame : rollingFrames)
        writeFrame("rolling", frame);

    summaryText.flush();
    spriteText.flush();
    groupText.flush();
    bandSummaryText.flush();
    bandMemberText.flush();
    bandText.flush();
    const bool ok = summaryFile.error() == QFile::NoError &&
                    spriteFile.error() == QFile::NoError &&
                    groupFile.error() == QFile::NoError &&
                    bandSummaryFile.error() == QFile::NoError &&
                    bandMemberFile.error() == QFile::NoError &&
                    bandFile.error() == QFile::NoError;
    manifestText << "Affine OBJ evidence: " << (ok ? "saved" : "write failed")
                 << " affine-obj-summary.csv (" << summaryRows << " rows), "
                 << "affine-obj-sprites.csv (" << spriteRows << " rows), "
                 << "affine-obj-groups.csv (" << groupRows << " rows)\n";
    manifestText << "  Groups are factual rotscale-index partitions. Structural candidates still require a stable canonical-layout hash across matrix motion; OAM-bound contact and same-state are diagnostics, not proof of authored grouping. Singleton controls reuse the existing isolated enhanced cache by exact cache-key agreement.\n\n";
    manifestText << "Ordered ordinary OBJ band evidence: "
                 << (ok ? "saved" : "write failed") << ' '
                 << "ordinary-obj-band-summary.csv (" << bandSummaryRows
                 << " rows), ordinary-obj-band-members.csv ("
                 << bandMemberRows << " rows), ordinary-obj-bands.csv ("
                 << bandRows << " rows)\n";
    manifestText << "  Bands are maximal equivalence classes by exact DS priority/OAM relation to every genuinely transformed affine OBJ. Special OBJ roles, unproven multipart affine groups, a band which splits an affine group, and recipes above the bounded band limit remain explicit rejections. This evidence does not change rendering authority.\n\n";
    return ok;
}


// Accessed only on the UI thread, including queued completion.
bool ExportBusy = false;
}
namespace WholeSceneDebugExport
{
Result Write(const Snapshot& snapshot)
{
    Result result;
    result.Directory = snapshot.Directory;
    QElapsedTimer elapsed;
    elapsed.start();
    QDir dir(snapshot.Directory);
    auto fail = [&](const QString& message) {
        result.Message = message;
        return result;
    };
    if (!dir.mkpath("."))
        return fail("Failed to create export directory: " + dir.path());
    const QString prefix = snapshot.ViewsInSubdirectory ? "views/" : "";
    if ((!snapshot.Views.empty() && !dir.mkpath(prefix.isEmpty() ? "." : "views")) ||
        (snapshot.CurrentFinalAvailable && !dir.mkpath("current-final")) ||
        (!snapshot.RollingFrames.empty() && !dir.mkpath("rolling-final")))
        return fail("Failed to create export subdirectories: " + dir.path());
    QFile manifest(dir.filePath("manifest.txt"));
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::Text))
        return fail("Failed to write manifest: " + dir.path());
    QTextStream text(&manifest);
    text << snapshot.Header << "\n";
    int saved = 0, skipped = 0, failed = 0;
    int currentSaved = 0, currentFailed = 0, rollingSaved = 0, rollingFailed = 0;
    bool evidenceSaved = true;
    if (snapshot.IncludeFinalEvidence)
        evidenceSaved = SaveAffineOBJDebugEvidence(dir,
            snapshot.CurrentFinalAvailable ? &snapshot.CurrentFinal : nullptr,
            snapshot.RollingFrames, text);
    const qint64 evidenceMs = elapsed.elapsed();
    if (snapshot.CurrentFinalAvailable)
    {
        text << "Current final frame:\n";
        SaveFinalDebugFrame(snapshot.CurrentFinal, QDir(dir.filePath("current-final")),
                           "current-final", text, currentSaved, currentFailed);
    }
    if (!snapshot.RollingFrames.empty())
    {
        text << "Rolling final frames:\n";
        int index = 0;
        for (const auto& frame : snapshot.RollingFrames)
        {
            const QString stem = QString("rolling-%1-serial%2")
                .arg(index++, 3, 10, QChar('0')).arg(frame.Serial, 6, 10, QChar('0'));
            SaveFinalDebugFrame(frame, QDir(dir.filePath("rolling-final")),
                               stem, text, rollingSaved, rollingFailed);
        }
    }
    const qint64 finalMs = elapsed.elapsed() - evidenceMs;
    for (const auto& view : snapshot.Views)
    {
        text << view.FileStem << "\n  Screen: " << view.ScreenName
             << "\n  Category: " << view.Category << "\n  View: " << view.Label
             << "\n  Capture/read/decode ms: " << view.ReadMilliseconds << "\n";
        if (!view.Available)
        {
            ++skipped;
            text << "  Result: skipped\n";
        }
        else
        {
            const QString name = prefix + view.FileStem + ".png";
            if (view.Image.save(dir.filePath(name), "PNG"))
            {
                ++saved;
                text << "  Result: saved " << name << " (" << view.Width << "x" << view.Height << ")\n";
            }
            else
            {
                ++failed;
                text << "  Result: failed to save " << name << "\n";
            }
        }
        if (!view.Status.isEmpty())
        {
            QString status = view.Status;
            text << "  " << status.replace("\n", "\n  ") << "\n";
        }
        text << "\n";
    }
    text << "Summary:\n  Saved views: " << saved << "\n  Skipped views: " << skipped
         << "\n  Failed views: " << failed
         << "\n  Saved current final images: " << currentSaved
         << "\n  Failed current final images: " << currentFailed
         << "\n  Saved rolling images: " << rollingSaved
         << "\n  Failed rolling images: " << rollingFailed << "\n";
    if (snapshot.IncludeFinalEvidence)
        text << "  Affine OBJ evidence CSVs: " << (evidenceSaved ? "saved" : "failed") << "\n";
    text << "Export timings (CPU wall ms, outside renderer ownership):\n"
         << "  Evidence CSVs: " << evidenceMs << "\n"
         << "  Final/rolling PNGs: " << finalMs << "\n"
         << "  View PNGs and manifest: " << elapsed.elapsed() - evidenceMs - finalMs << "\n";
    text.flush();
    const bool manifestSaved = text.status() == QTextStream::Ok && manifest.flush();
    result.Success = manifestSaved && evidenceSaved && failed == 0 && currentFailed == 0 && rollingFailed == 0;
    result.Message = QString("%1 %2 images to %3 (%4 skipped, %5 failed; %6 ms writing).")
        .arg(result.Success ? "Exported" : "Export incomplete:")
        .arg(saved + currentSaved + rollingSaved).arg(dir.path()).arg(skipped)
        .arg(failed + currentFailed + rollingFailed).arg(elapsed.elapsed());
    return result;
}
Job::Job() : Reserved(!ExportBusy)
{
    if (Reserved)
        ExportBusy = true;
}

Job::~Job()
{
    if (Reserved)
        ExportBusy = false;
}

void Job::start(Snapshot snapshot, QObject* recipient, std::function<void(const Result&)> completed)
{
    Q_ASSERT(Reserved);
    auto result = std::make_shared<Result>();
    result->Directory = snapshot.Directory;
    auto* worker = QThread::create([snapshot = std::move(snapshot), result]() {
        try
        {
            *result = Write(snapshot);
        }
        catch (const std::exception& error)
        {
            result->Message = QString("Debug export failed: %1").arg(error.what());
        }
        catch (...)
        {
            result->Message = "Debug export failed unexpectedly.";
        }
    });
    worker->setParent(QCoreApplication::instance());
    QObject::connect(worker, &QThread::finished, QCoreApplication::instance(),
        [worker, result, recipient = QPointer<QObject>(recipient), completed = std::move(completed)]() {
            ExportBusy = false;
            if (recipient)
                completed(*result);
            worker->deleteLater();
        });
    // Do not destroy a running worker or abandon a half-written capture at exit.
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                     worker, [worker]() { worker->wait(); });
    Reserved = false;
    worker->start();
}
}
