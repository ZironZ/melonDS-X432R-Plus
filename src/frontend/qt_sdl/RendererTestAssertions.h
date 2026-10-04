// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RENDERERTESTASSERTIONS_H
#define RENDERERTESTASSERTIONS_H

#include <QJsonArray>
#include <QString>

struct RendererTestAssertionRun
{
    QJsonArray results;
    int passed = 0;
    int failed = 0;
    QString firstFailure;
};

class RendererTestAssertions
{
public:
    // Loads and validates a portable expectation document. The replay
    // manifest owns ROM/state paths; this document owns assertions only.
    // imageCompare assertions select an exported image by exact frame plus
    // final surface, or by exact frame plus engine/debug view. They require an
    // explicit ROI, max changed fraction, max channel error, max connected
    // component size, temporal scope, and reference-authority metadata. An
    // optional same-size exclusion mask treats non-black RGB pixels as
    // excluded and preserves them as magenta in the difference artifact.
    // imageHistogram assertions use the same semantic image/ROI selection and
    // constrain declared RGB/RGBA color counts or fractions, unique colors,
    // and the number of pixels not represented by the declared colors.
    // perceptualSignature assertions reduce an ROI to a deterministic named
    // average-luma grid and require the expected pinned prototype to be the
    // nearest match within an explicit mean-absolute-luma distance.
    // perceptualSequence applies the same classifier to a consecutive exact
    // frame range and requires reviewed pose identities and run lengths.
    // csvSequence summarizes a selected column into transitions, reversions
    // and runs; csvRunLimit measures the frequency, duration and allowed
    // reasons of rows matching a fallback predicate.
    // pairedImageCompare reduces an integer-scale candidate ROI with an
    // explicit nearest-center or box-average rule and compares RGB or RGBA
    // against a semantic native capture or reviewed portable scale-1 reference.
    static bool load(const QString& path, QJsonArray& assertions, QString& error);

    // Evaluates assertions after the timing log and exact-frame exports have
    // closed. Paths in the assertion document are resolved by the assertion
    // type, not relative to the replay manifest.
    static RendererTestAssertionRun evaluate(const QJsonArray& assertions,
                                             const QString& expectationsPath,
                                             const QString& outputDir,
                                             const QString& summaryCsvPath,
                                             const QString& rendererDetailsCsvPath);
};

#endif // RENDERERTESTASSERTIONS_H
