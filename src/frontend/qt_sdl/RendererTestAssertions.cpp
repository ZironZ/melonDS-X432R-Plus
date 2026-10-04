// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RendererTestAssertions.h"

#include <cmath>
#include <limits>
#include <QFile>
#include <QDir>
#include <QHash>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QFileInfo>
#include <QRect>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QVector>

namespace
{
struct CsvTable
{
    QStringList columns;
    QVector<QStringList> rows;
};

struct ImageMetrics
{
    qint64 comparedPixels = 0;
    qint64 changedPixels = 0;
    double changedPixelFraction = 0.0;
    int maxChannelError = 0;
    int maxConnectedComponentSize = 0;
    QRect changedBounds;
};

QString SafeName(const QString& name)
{
    QString result;
    for (const QChar ch : name)
    {
        if (ch.isLetterOrNumber())
            result += ch.toLower();
        else if (!result.isEmpty() && !result.endsWith('-'))
            result += '-';
    }
    while (result.endsWith('-'))
        result.chop(1);
    return result.isEmpty() ? QStringLiteral("assertion") : result;
}

QString DebugViewExportName(const QString& name)
{
    QString result;
    for (const QChar ch : name)
    {
        if (ch.isUpper() && !result.isEmpty())
            result += '-';
        if (!ch.isLetterOrNumber())
            return {};
        result += ch.toLower();
    }
    return result;
}

bool JsonNonNegativeInteger(const QJsonValue& value, int& result)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < 0 || number > static_cast<double>(std::numeric_limits<int>::max()))
    {
        return false;
    }
    result = static_cast<int>(number);
    return true;
}

QStringList ParseCsvLine(const QString& line)
{
    QStringList fields;
    QString field;
    bool quoted = false;
    for (int i = 0; i < line.size(); i++)
    {
        const QChar ch = line[i];
        if (quoted)
        {
            if (ch == '"')
            {
                if (i + 1 < line.size() && line[i + 1] == '"')
                {
                    field += '"';
                    i++;
                }
                else
                {
                    quoted = false;
                }
            }
            else
            {
                field += ch;
            }
        }
        else if (ch == ',')
        {
            fields.push_back(field);
            field.clear();
        }
        else if (ch == '"' && field.isEmpty())
        {
            quoted = true;
        }
        else
        {
            field += ch;
        }
    }
    fields.push_back(field);
    return fields;
}

bool LoadCsv(const QString& path, CsvTable& table, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        error = QString("Could not open assertion CSV %1: %2").arg(path, file.errorString());
        return false;
    }

    QTextStream stream(&file);
    if (stream.atEnd())
    {
        error = QString("Assertion CSV %1 is empty.").arg(path);
        return false;
    }
    table.columns = ParseCsvLine(stream.readLine());
    if (!table.columns.isEmpty() && table.columns.back().endsWith('\r'))
        table.columns.back().chop(1);

    while (!stream.atEnd())
    {
        QString line = stream.readLine();
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        QStringList row = ParseCsvLine(line);
        if (row.size() != table.columns.size())
        {
            error = QString("Assertion CSV %1 row %2 has %3 fields; expected %4.")
                .arg(path)
                .arg(table.rows.size() + 2)
                .arg(row.size())
                .arg(table.columns.size());
            return false;
        }
        table.rows.push_back(std::move(row));
    }
    return true;
}

bool JsonInteger(const QJsonValue& value, qint64& result)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < 0 || number > 9007199254740991.0)
    {
        return false;
    }
    result = static_cast<qint64>(number);
    return true;
}

bool CellNumber(const QString& cell, double& result)
{
    bool ok = false;
    result = cell.toDouble(&ok);
    return ok && std::isfinite(result);
}

QString JsonDisplay(const QJsonValue& value)
{
    if (value.isString())
        return value.toString();
    if (value.isBool())
        return value.toBool() ? "true" : "false";
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 17);
    return QString::fromUtf8(QJsonDocument(QJsonArray { value }).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

bool EqualCell(const QString& cell, const QJsonValue& expected)
{
    if (expected.isDouble())
    {
        double actual = 0.0;
        return CellNumber(cell, actual) && actual == expected.toDouble();
    }
    if (expected.isBool())
        return cell == (expected.toBool() ? "1" : "0") ||
               cell.compare(expected.toBool() ? "true" : "false", Qt::CaseInsensitive) == 0;
    return cell == expected.toString();
}

bool ValidateReference(const QJsonObject& assertion, QString& error)
{
    const QJsonObject reference = assertion.value("reference").toObject();
    static const QSet<QString> classes = {
        "emulatorParity", "scoped", "negative", "correctness"
    };
    const QString referenceClass = reference.value("class").toString();
    if (!classes.contains(referenceClass))
    {
        error = "reference.class must be emulatorParity, scoped, negative or correctness";
        return false;
    }
    if (reference.value("scope").toString().trimmed().isEmpty())
    {
        error = "reference.scope must be a non-empty string";
        return false;
    }
    if (!reference.value("knownExclusions").isArray())
    {
        error = "reference.knownExclusions must be an array (it may be empty)";
        return false;
    }
    if (reference.value("toleranceRationale").toString().trimmed().isEmpty())
    {
        error = "reference.toleranceRationale must be a non-empty string";
        return false;
    }
    return true;
}

bool ValidateCondition(const QJsonValue& value, QString& error)
{
    if (!value.isObject())
    {
        error = "condition must be an object";
        return false;
    }
    const QJsonObject condition = value.toObject();
    static const QSet<QString> operators = {
        "equals", "oneOf", "min", "max", "bitmaskAll", "bitmaskNone"
    };
    int count = 0;
    for (auto it = condition.begin(); it != condition.end(); ++it)
    {
        if (!operators.contains(it.key()))
        {
            error = QString("unknown condition operator %1").arg(it.key());
            return false;
        }
        count++;
    }
    if (count == 0)
    {
        error = "condition must contain at least one operator";
        return false;
    }
    if (condition.contains("oneOf") && !condition.value("oneOf").isArray())
    {
        error = "oneOf must be an array";
        return false;
    }
    for (const QString& key : {QString("min"), QString("max")})
    {
        if (condition.contains(key) && !condition.value(key).isDouble())
        {
            error = QString("%1 must be numeric").arg(key);
            return false;
        }
    }
    for (const QString& key : {QString("bitmaskAll"), QString("bitmaskNone")})
    {
        qint64 ignored = 0;
        if (condition.contains(key) && !JsonInteger(condition.value(key), ignored))
        {
            error = QString("%1 must be an integer").arg(key);
            return false;
        }
    }
    return true;
}

bool ValidateCsvAssertion(const QJsonObject& assertion, QString& error)
{
    const QString csv = assertion.value("csv").toString();
    if (csv != "summary" && csv != "rendererDetails")
    {
        error = "csv must be summary or rendererDetails";
        return false;
    }
    if (!assertion.value("where").isObject() || !assertion.value("expect").isObject())
    {
        error = "where and expect must be objects";
        return false;
    }
    if (!assertion.value("rows").isObject())
    {
        error = "rows must be an explicit count condition";
        return false;
    }

    for (const QString& groupName : {QString("where"), QString("expect")})
    {
        const QJsonObject group = assertion.value(groupName).toObject();
        for (auto it = group.begin(); it != group.end(); ++it)
        {
            QString conditionError;
            if (!ValidateCondition(it.value(), conditionError))
            {
                error = QString("%1.%2 %3").arg(groupName, it.key(), conditionError);
                return false;
            }
        }
    }

    QString countError;
    if (!ValidateCondition(assertion.value("rows"), countError))
    {
        error = QString("rows %1").arg(countError);
        return false;
    }
    return true;
}

bool ValidateImageCompareAssertion(const QJsonObject& assertion, QString& error)
{
    const QJsonObject actual = assertion.value("actual").toObject();
    qint64 frame = 0;
    if (actual.isEmpty() || !JsonInteger(actual.value("frame"), frame))
    {
        error = "actual.frame must be a non-negative integer";
        return false;
    }

    const QString surface = actual.value("surface").toString();
    const QString engine = actual.value("engine").toString();
    const QString debugView = actual.value("debugView").toString();
    const bool hasSurface = surface == "finalTop" || surface == "finalBottom";
    const bool hasDebugView = (engine == "A" || engine == "B") &&
                              !DebugViewExportName(debugView).isEmpty();
    if (hasSurface == hasDebugView)
    {
        error = "actual must select exactly one finalTop/finalBottom surface or one engine A/B debugView";
        return false;
    }
    if (!hasSurface && (!surface.isEmpty() || engine.isEmpty() || debugView.isEmpty()))
    {
        error = "actual contains an invalid surface or incomplete debug-view selection";
        return false;
    }

    if (assertion.value("expected").toString().trimmed().isEmpty())
    {
        error = "expected must be a non-empty path relative to the expectations document";
        return false;
    }

    const QJsonArray roi = assertion.value("roi").toArray();
    if (roi.size() != 4)
    {
        error = "roi must be [x, y, width, height]";
        return false;
    }
    int roiValues[4] {};
    for (int i = 0; i < 4; i++)
    {
        if (!JsonNonNegativeInteger(roi[i], roiValues[i]))
        {
            error = "roi values must be non-negative integers";
            return false;
        }
    }
    if (roiValues[2] == 0 || roiValues[3] == 0)
    {
        error = "roi width and height must be greater than zero";
        return false;
    }

    const QJsonObject tolerance = assertion.value("tolerance").toObject();
    const QJsonValue fractionValue = tolerance.value("maxChangedPixelFraction");
    if (!fractionValue.isDouble() || !std::isfinite(fractionValue.toDouble()) ||
        fractionValue.toDouble() < 0.0 || fractionValue.toDouble() > 1.0)
    {
        error = "tolerance.maxChangedPixelFraction must be between 0 and 1";
        return false;
    }
    int maxChannelError = 0;
    if (!JsonNonNegativeInteger(tolerance.value("maxChannelError"), maxChannelError) ||
        maxChannelError > 255)
    {
        error = "tolerance.maxChannelError must be an integer from 0 to 255";
        return false;
    }
    int maxComponent = 0;
    if (!JsonNonNegativeInteger(tolerance.value("maxConnectedComponentSize"), maxComponent))
    {
        error = "tolerance.maxConnectedComponentSize must be a non-negative integer";
        return false;
    }

    if (assertion.value("temporalScope").toString().trimmed().isEmpty())
    {
        error = "temporalScope must state the adjacent-frame persistence claim or explicitly say single-frame";
        return false;
    }
    if (assertion.contains("exclusionMask") &&
        assertion.value("exclusionMask").toString().trimmed().isEmpty())
    {
        error = "exclusionMask must be a non-empty relative path when present";
        return false;
    }
    return true;
}

bool ValidateImageHistogramAssertion(const QJsonObject& assertion, QString& error)
{
    const QJsonObject actual = assertion.value("actual").toObject();
    qint64 frame = 0;
    if (actual.isEmpty() || !JsonInteger(actual.value("frame"), frame))
    {
        error = "actual.frame must be a non-negative integer";
        return false;
    }
    const QString surface = actual.value("surface").toString();
    const QString engine = actual.value("engine").toString();
    const QString debugView = actual.value("debugView").toString();
    const bool hasSurface = surface == "finalTop" || surface == "finalBottom";
    const bool hasDebugView = (engine == "A" || engine == "B") &&
                              !DebugViewExportName(debugView).isEmpty();
    if (hasSurface == hasDebugView ||
        (!hasSurface && (!surface.isEmpty() || engine.isEmpty() || debugView.isEmpty())))
    {
        error = "actual must select exactly one final surface or one engine/debug view";
        return false;
    }

    const QJsonArray roi = assertion.value("roi").toArray();
    if (roi.size() != 4)
    {
        error = "roi must be [x, y, width, height]";
        return false;
    }
    int roiValues[4] {};
    for (int i = 0; i < 4; i++)
    {
        if (!JsonNonNegativeInteger(roi[i], roiValues[i]))
        {
            error = "roi values must be non-negative integers";
            return false;
        }
    }
    if (roiValues[2] == 0 || roiValues[3] == 0)
    {
        error = "roi width and height must be greater than zero";
        return false;
    }

    const QString colorMode = assertion.value("colorMode").toString();
    if (colorMode != "rgb" && colorMode != "rgba")
    {
        error = "colorMode must be rgb or rgba";
        return false;
    }
    const int componentCount = colorMode == "rgb" ? 3 : 4;
    const QJsonArray colors = assertion.value("colors").toArray();
    if (colors.isEmpty())
    {
        error = "colors must contain at least one expected color";
        return false;
    }
    QSet<QString> colorKeys;
    for (int i = 0; i < colors.size(); i++)
    {
        const QJsonObject color = colors[i].toObject();
        const QJsonArray components = color.value("value").toArray();
        if (color.isEmpty() || components.size() != componentCount)
        {
            error = QString("colors[%1].value must contain %2 components")
                .arg(i).arg(componentCount);
            return false;
        }
        QStringList keyParts;
        for (const QJsonValue& component : components)
        {
            int number = 0;
            if (!JsonNonNegativeInteger(component, number) || number > 255)
            {
                error = QString("colors[%1] components must be integers from 0 to 255").arg(i);
                return false;
            }
            keyParts.push_back(QString::number(number));
        }
        const QString key = keyParts.join(',');
        if (colorKeys.contains(key))
        {
            error = QString("colors[%1] duplicates color %2").arg(i).arg(key);
            return false;
        }
        colorKeys.insert(key);
        if (!color.contains("pixels") && !color.contains("fraction"))
        {
            error = QString("colors[%1] requires a pixels or fraction condition").arg(i);
            return false;
        }
        for (const QString& conditionName : {QString("pixels"), QString("fraction")})
        {
            if (!color.contains(conditionName))
                continue;
            QString conditionError;
            if (!ValidateCondition(color.value(conditionName), conditionError))
            {
                error = QString("colors[%1].%2 %3").arg(i).arg(conditionName, conditionError);
                return false;
            }
        }
    }

    for (const QString& conditionName : {QString("uniqueColors"), QString("unlistedPixels")})
    {
        QString conditionError;
        if (!ValidateCondition(assertion.value(conditionName), conditionError))
        {
            error = QString("%1 %2").arg(conditionName, conditionError);
            return false;
        }
    }
    if (assertion.value("temporalScope").toString().trimmed().isEmpty())
    {
        error = "temporalScope must state the adjacent-frame persistence claim or explicitly say single-frame";
        return false;
    }
    if (assertion.contains("exclusionMask") &&
        assertion.value("exclusionMask").toString().trimmed().isEmpty())
    {
        error = "exclusionMask must be a non-empty relative path when present";
        return false;
    }
    return true;
}

bool ValidatePerceptualSignatureAssertion(const QJsonObject& assertion, QString& error)
{
    const QJsonObject actual = assertion.value("actual").toObject();
    qint64 frame = 0;
    if (actual.isEmpty() || !JsonInteger(actual.value("frame"), frame))
    {
        error = "actual.frame must be a non-negative integer";
        return false;
    }
    const QString surface = actual.value("surface").toString();
    const QString engine = actual.value("engine").toString();
    const QString debugView = actual.value("debugView").toString();
    const bool hasSurface = surface == "finalTop" || surface == "finalBottom";
    const bool hasDebugView = (engine == "A" || engine == "B") &&
                              !DebugViewExportName(debugView).isEmpty();
    if (hasSurface == hasDebugView ||
        (!hasSurface && (!surface.isEmpty() || engine.isEmpty() || debugView.isEmpty())))
    {
        error = "actual must select exactly one final surface or one engine/debug view";
        return false;
    }

    const QJsonArray roi = assertion.value("roi").toArray();
    if (roi.size() != 4)
    {
        error = "roi must be [x, y, width, height]";
        return false;
    }
    int roiValues[4] {};
    for (int i = 0; i < 4; i++)
    {
        if (!JsonNonNegativeInteger(roi[i], roiValues[i]))
        {
            error = "roi values must be non-negative integers";
            return false;
        }
    }
    if (roiValues[2] == 0 || roiValues[3] == 0)
    {
        error = "roi width and height must be greater than zero";
        return false;
    }

    const QJsonArray grid = assertion.value("grid").toArray();
    if (grid.size() != 2)
    {
        error = "grid must be [columns, rows]";
        return false;
    }
    int gridWidth = 0;
    int gridHeight = 0;
    if (!JsonNonNegativeInteger(grid[0], gridWidth) ||
        !JsonNonNegativeInteger(grid[1], gridHeight) ||
        gridWidth == 0 || gridHeight == 0 || gridWidth > 256 || gridHeight > 256)
    {
        error = "grid dimensions must be integers from 1 to 256";
        return false;
    }
    if (gridWidth > roiValues[2] || gridHeight > roiValues[3])
    {
        error = "grid dimensions cannot exceed the ROI dimensions";
        return false;
    }
    if (assertion.value("distanceMetric").toString() != "meanAbsoluteLuma")
    {
        error = "distanceMetric must be meanAbsoluteLuma";
        return false;
    }
    const QJsonValue maxDistance = assertion.value("maxDistance");
    if (!maxDistance.isDouble() || !std::isfinite(maxDistance.toDouble()) ||
        maxDistance.toDouble() < 0.0 || maxDistance.toDouble() > 255.0)
    {
        error = "maxDistance must be between 0 and 255 luma levels";
        return false;
    }

    const QJsonArray prototypes = assertion.value("prototypes").toArray();
    const QString expectedPrototype = assertion.value("expectedPrototype").toString().trimmed();
    if (prototypes.isEmpty() || expectedPrototype.isEmpty())
    {
        error = "prototypes and expectedPrototype are required";
        return false;
    }
    QSet<QString> prototypeIds;
    for (int i = 0; i < prototypes.size(); i++)
    {
        const QJsonObject prototype = prototypes[i].toObject();
        const QString id = prototype.value("id").toString().trimmed();
        const QString file = prototype.value("file").toString().trimmed();
        if (id.isEmpty() || file.isEmpty() || prototypeIds.contains(id))
        {
            error = QString("prototypes[%1] requires a unique id and non-empty relative file").arg(i);
            return false;
        }
        prototypeIds.insert(id);
    }
    if (!prototypeIds.contains(expectedPrototype))
    {
        error = "expectedPrototype must name one of the declared prototypes";
        return false;
    }
    if (assertion.value("temporalScope").toString().trimmed().isEmpty())
    {
        error = "temporalScope must state the adjacent-frame persistence claim or explicitly say single-frame";
        return false;
    }
    if (assertion.contains("exclusionMask") &&
        assertion.value("exclusionMask").toString().trimmed().isEmpty())
    {
        error = "exclusionMask must be a non-empty relative path when present";
        return false;
    }
    return true;
}

bool ValidatePerceptualSequenceAssertion(const QJsonObject& assertion, QString& error)
{
    const QJsonObject actual = assertion.value("actual").toObject();
    if (actual.isEmpty() || actual.contains("frame"))
    {
        error = "actual must select a surface/debug view without a frame";
        return false;
    }
    const QString surface = actual.value("surface").toString();
    const QString engine = actual.value("engine").toString();
    const QString debugView = actual.value("debugView").toString();
    const bool hasSurface = surface == "finalTop" || surface == "finalBottom";
    const bool hasDebugView = (engine == "A" || engine == "B") &&
                              !DebugViewExportName(debugView).isEmpty();
    if (hasSurface == hasDebugView ||
        (!hasSurface && (!surface.isEmpty() || engine.isEmpty() || debugView.isEmpty())))
    {
        error = "actual must select exactly one final surface or one engine/debug view";
        return false;
    }

    const QJsonObject frames = assertion.value("frames").toObject();
    qint64 minFrame = 0;
    qint64 maxFrame = 0;
    if (!JsonInteger(frames.value("min"), minFrame) ||
        !JsonInteger(frames.value("max"), maxFrame) || maxFrame < minFrame ||
        maxFrame - minFrame >= std::numeric_limits<int>::max())
    {
        error = "frames.min/max must define a non-negative consecutive range";
        return false;
    }

    const QJsonArray roi = assertion.value("roi").toArray();
    if (roi.size() != 4)
    {
        error = "roi must be [x, y, width, height]";
        return false;
    }
    int roiValues[4] {};
    for (int i = 0; i < 4; i++)
    {
        if (!JsonNonNegativeInteger(roi[i], roiValues[i]))
        {
            error = "roi values must be non-negative integers";
            return false;
        }
    }
    if (roiValues[2] == 0 || roiValues[3] == 0)
    {
        error = "roi width and height must be greater than zero";
        return false;
    }

    const QJsonArray grid = assertion.value("grid").toArray();
    int gridWidth = 0;
    int gridHeight = 0;
    if (grid.size() != 2 ||
        !JsonNonNegativeInteger(grid[0], gridWidth) ||
        !JsonNonNegativeInteger(grid[1], gridHeight) ||
        gridWidth == 0 || gridHeight == 0 || gridWidth > 256 || gridHeight > 256)
    {
        error = "grid dimensions must be integers from 1 to 256";
        return false;
    }
    if (gridWidth > roiValues[2] || gridHeight > roiValues[3])
    {
        error = "grid dimensions cannot exceed the ROI dimensions";
        return false;
    }
    if (assertion.value("distanceMetric").toString() != "meanAbsoluteLuma")
    {
        error = "distanceMetric must be meanAbsoluteLuma";
        return false;
    }
    const QJsonValue maxDistance = assertion.value("maxDistance");
    if (!maxDistance.isDouble() || !std::isfinite(maxDistance.toDouble()) ||
        maxDistance.toDouble() < 0.0 || maxDistance.toDouble() > 255.0)
    {
        error = "maxDistance must be between 0 and 255 luma levels";
        return false;
    }

    const QJsonArray prototypes = assertion.value("prototypes").toArray();
    if (prototypes.isEmpty())
    {
        error = "prototypes must contain at least one reviewed pose";
        return false;
    }
    QSet<QString> prototypeIds;
    QStringList prototypeOrder;
    for (int i = 0; i < prototypes.size(); i++)
    {
        const QJsonObject prototype = prototypes[i].toObject();
        const QString id = prototype.value("id").toString().trimmed();
        const QString file = prototype.value("file").toString().trimmed();
        if (id.isEmpty() || file.isEmpty() || prototypeIds.contains(id))
        {
            error = QString("prototypes[%1] requires a unique id and non-empty relative file").arg(i);
            return false;
        }
        prototypeIds.insert(id);
        prototypeOrder.push_back(id);
    }

    const QJsonArray expectedRuns = assertion.value("expectedRuns").toArray();
    if (expectedRuns.isEmpty())
    {
        error = "expectedRuns must contain the complete pose sequence";
        return false;
    }
    qint64 expectedFrames = 0;
    QString previousId;
    for (int i = 0; i < expectedRuns.size(); i++)
    {
        const QJsonObject run = expectedRuns[i].toObject();
        const QString id = run.value("id").toString().trimmed();
        int length = 0;
        if (!prototypeIds.contains(id) ||
            !JsonNonNegativeInteger(run.value("length"), length) || length == 0)
        {
            error = QString("expectedRuns[%1] requires a prototype id and positive length").arg(i);
            return false;
        }
        if (id == previousId)
        {
            error = QString("expectedRuns[%1] repeats the previous prototype; merge adjacent runs").arg(i);
            return false;
        }
        if (!previousId.isEmpty() &&
            prototypeOrder.indexOf(id) !=
                (prototypeOrder.indexOf(previousId) + 1) % prototypeOrder.size())
        {
            error = QString("expectedRuns[%1] is not the next prototype in declared cycle order").arg(i);
            return false;
        }
        previousId = id;
        expectedFrames += length;
    }
    if (expectedFrames != maxFrame - minFrame + 1)
    {
        error = "expectedRuns lengths must cover every frame in the declared range";
        return false;
    }
    if (assertion.value("temporalScope").toString().trimmed().isEmpty())
    {
        error = "temporalScope must define the consecutive-frame pose contract";
        return false;
    }
    if (assertion.contains("exclusionMask") &&
        assertion.value("exclusionMask").toString().trimmed().isEmpty())
    {
        error = "exclusionMask must be a non-empty relative path when present";
        return false;
    }
    return true;
}

bool ValidateConditionGroup(const QJsonValue& value,
                            const QString& name,
                            bool allowEmpty,
                            QString& error)
{
    if (!value.isObject())
    {
        error = QString("%1 must be an object").arg(name);
        return false;
    }
    const QJsonObject group = value.toObject();
    if (!allowEmpty && group.isEmpty())
    {
        error = QString("%1 must not be empty").arg(name);
        return false;
    }
    for (auto it = group.begin(); it != group.end(); ++it)
    {
        QString conditionError;
        if (!ValidateCondition(it.value(), conditionError))
        {
            error = QString("%1.%2 %3").arg(name, it.key(), conditionError);
            return false;
        }
    }
    return true;
}

bool ValidateCsvSequenceAssertion(const QJsonObject& assertion, QString& error)
{
    const QString csv = assertion.value("csv").toString();
    if (csv != "summary" && csv != "rendererDetails")
    {
        error = "csv must be summary or rendererDetails";
        return false;
    }
    if (assertion.value("column").toString().trimmed().isEmpty())
    {
        error = "column must be a non-empty CSV column";
        return false;
    }
    if (!ValidateConditionGroup(assertion.value("where"), "where", true, error))
        return false;
    for (const QString& name : {QString("rows"), QString("transitions"),
                                QString("reversions"), QString("singleFrameReversions"),
                                QString("longestRun")})
    {
        QString conditionError;
        if (!ValidateCondition(assertion.value(name), conditionError))
        {
            error = QString("%1 %2").arg(name, conditionError);
            return false;
        }
    }
    if (!assertion.value("requireConsecutiveFrames").isBool())
    {
        error = "requireConsecutiveFrames must be explicit";
        return false;
    }
    if (assertion.contains("allowedValues") &&
        (!assertion.value("allowedValues").isArray() ||
         assertion.value("allowedValues").toArray().isEmpty()))
    {
        error = "allowedValues must be a non-empty array when present";
        return false;
    }
    return true;
}

bool ValidateCsvRunLimitAssertion(const QJsonObject& assertion, QString& error)
{
    const QString csv = assertion.value("csv").toString();
    if (csv != "summary" && csv != "rendererDetails")
    {
        error = "csv must be summary or rendererDetails";
        return false;
    }
    if (!ValidateConditionGroup(assertion.value("where"), "where", true, error) ||
        !ValidateConditionGroup(assertion.value("match"), "match", false, error) ||
        !ValidateConditionGroup(assertion.value("matchedExpect"), "matchedExpect", true, error))
    {
        return false;
    }
    for (const QString& name : {QString("rows"), QString("matchingRows"),
                                QString("matchingFraction"), QString("runs"),
                                QString("longestRun")})
    {
        QString conditionError;
        if (!ValidateCondition(assertion.value(name), conditionError))
        {
            error = QString("%1 %2").arg(name, conditionError);
            return false;
        }
    }
    const bool hasArea = assertion.contains("area");
    const bool hasMatchingArea = assertion.contains("matchingArea");
    if (hasArea != hasMatchingArea)
    {
        error = "area and matchingArea must either both be present or both be absent";
        return false;
    }
    if (hasArea)
    {
        const QJsonObject area = assertion.value("area").toObject();
        qint64 width = 0;
        if (area.isEmpty() ||
            area.value("yStartColumn").toString().trimmed().isEmpty() ||
            area.value("yEndColumn").toString().trimmed().isEmpty() ||
            !JsonInteger(area.value("width"), width) || width <= 0)
        {
            error = "area must provide yStartColumn, yEndColumn and a positive integer width";
            return false;
        }
        QString conditionError;
        if (!ValidateCondition(assertion.value("matchingArea"), conditionError))
        {
            error = "matchingArea " + conditionError;
            return false;
        }
    }
    if (!assertion.value("requireConsecutiveFrames").isBool())
    {
        error = "requireConsecutiveFrames must be explicit";
        return false;
    }
    return true;
}

bool ValidateSemanticImageSelection(const QJsonObject& selection,
                                    const QString& name,
                                    QString& error)
{
    qint64 frame = 0;
    if (selection.isEmpty() || !JsonInteger(selection.value("frame"), frame))
    {
        error = QString("%1.frame must be a non-negative integer").arg(name);
        return false;
    }
    const QString surface = selection.value("surface").toString();
    const QString engine = selection.value("engine").toString();
    const QString debugView = selection.value("debugView").toString();
    const bool hasSurface = surface == "finalTop" || surface == "finalBottom";
    const bool hasDebugView = (engine == "A" || engine == "B") &&
                              !DebugViewExportName(debugView).isEmpty();
    if (hasSurface == hasDebugView ||
        (!hasSurface && (!surface.isEmpty() || engine.isEmpty() || debugView.isEmpty())))
    {
        error = QString("%1 must select exactly one final surface or one engine/debug view")
            .arg(name);
        return false;
    }
    return true;
}

bool ValidateImageRoi(const QJsonValue& value, const QString& name, QString& error)
{
    const QJsonArray roi = value.toArray();
    if (roi.size() != 4)
    {
        error = QString("%1 must be [x, y, width, height]").arg(name);
        return false;
    }
    int values[4] {};
    for (int i = 0; i < 4; i++)
    {
        if (!JsonNonNegativeInteger(roi[i], values[i]))
        {
            error = QString("%1 values must be non-negative integers").arg(name);
            return false;
        }
    }
    if (values[2] == 0 || values[3] == 0)
    {
        error = QString("%1 width and height must be greater than zero").arg(name);
        return false;
    }
    return true;
}

bool ValidatePairedImageAssertion(const QJsonObject& assertion, QString& error)
{
    if (!ValidateSemanticImageSelection(assertion.value("candidate").toObject(),
                                        "candidate", error))
    {
        return false;
    }
    const QJsonObject native = assertion.value("native").toObject();
    const QString nativeFile = native.value("file").toString().trimmed();
    if (nativeFile.isEmpty())
    {
        if (!ValidateSemanticImageSelection(native, "native", error))
            return false;
    }
    else if (native.size() != 1)
    {
        error = "native must contain either file or a semantic capture selection, not both";
        return false;
    }
    if (!ValidateImageRoi(assertion.value("candidateRoi"), "candidateRoi", error) ||
        !ValidateImageRoi(assertion.value("nativeRoi"), "nativeRoi", error))
    {
        return false;
    }
    const QString reduction = assertion.value("reduction").toString();
    if (reduction != "nearestCenter" && reduction != "boxAverage")
    {
        error = "reduction must be nearestCenter or boxAverage";
        return false;
    }
    const QString colorMode = assertion.value("colorMode").toString();
    if (colorMode != "rgb" && colorMode != "rgba")
    {
        error = "colorMode must be rgb or rgba";
        return false;
    }
    const QJsonObject tolerance = assertion.value("tolerance").toObject();
    const QJsonValue fractionValue = tolerance.value("maxChangedPixelFraction");
    if (!fractionValue.isDouble() || !std::isfinite(fractionValue.toDouble()) ||
        fractionValue.toDouble() < 0.0 || fractionValue.toDouble() > 1.0)
    {
        error = "tolerance.maxChangedPixelFraction must be between 0 and 1";
        return false;
    }
    int maxChannelError = 0;
    if (!JsonNonNegativeInteger(tolerance.value("maxChannelError"), maxChannelError) ||
        maxChannelError > 255)
    {
        error = "tolerance.maxChannelError must be an integer from 0 to 255";
        return false;
    }
    int maxComponent = 0;
    if (!JsonNonNegativeInteger(tolerance.value("maxConnectedComponentSize"), maxComponent))
    {
        error = "tolerance.maxConnectedComponentSize must be a non-negative integer";
        return false;
    }
    if (assertion.value("temporalScope").toString().trimmed().isEmpty())
    {
        error = "temporalScope must state the paired-frame timing relationship";
        return false;
    }
    return true;
}

bool EvaluateCondition(const QString& cell,
                       const QJsonObject& condition,
                       QString& expectedDescription)
{
    QStringList descriptions;
    bool passed = true;
    if (condition.contains("equals"))
    {
        const QJsonValue expected = condition.value("equals");
        descriptions.push_back(QString("equals %1").arg(JsonDisplay(expected)));
        passed &= EqualCell(cell, expected);
    }
    if (condition.contains("oneOf"))
    {
        bool found = false;
        QStringList values;
        for (const QJsonValue& expected : condition.value("oneOf").toArray())
        {
            values.push_back(JsonDisplay(expected));
            found |= EqualCell(cell, expected);
        }
        descriptions.push_back(QString("one of [%1]").arg(values.join(", ")));
        passed &= found;
    }
    if (condition.contains("min"))
    {
        double actual = 0.0;
        const double minimum = condition.value("min").toDouble();
        descriptions.push_back(QString("at least %1").arg(minimum, 0, 'g', 17));
        passed &= CellNumber(cell, actual) && actual >= minimum;
    }
    if (condition.contains("max"))
    {
        double actual = 0.0;
        const double maximum = condition.value("max").toDouble();
        descriptions.push_back(QString("at most %1").arg(maximum, 0, 'g', 17));
        passed &= CellNumber(cell, actual) && actual <= maximum;
    }
    if (condition.contains("bitmaskAll") || condition.contains("bitmaskNone"))
    {
        bool actualOk = false;
        const qint64 actual = cell.toLongLong(&actualOk, 0);
        if (condition.contains("bitmaskAll"))
        {
            qint64 mask = 0;
            JsonInteger(condition.value("bitmaskAll"), mask);
            descriptions.push_back(QString("contains all bits 0x%1").arg(mask, 0, 16));
            passed &= actualOk && (actual & mask) == mask;
        }
        if (condition.contains("bitmaskNone"))
        {
            qint64 mask = 0;
            JsonInteger(condition.value("bitmaskNone"), mask);
            descriptions.push_back(QString("contains no bits 0x%1").arg(mask, 0, 16));
            passed &= actualOk && (actual & mask) == 0;
        }
    }
    expectedDescription = descriptions.join(" and ");
    return passed;
}

QJsonObject FailureResult(const QJsonObject& assertion, const QString& message)
{
    QJsonObject result;
    result["id"] = assertion.value("id").toString();
    result["type"] = assertion.value("type").toString();
    result["passed"] = false;
    result["message"] = message;
    result["reference"] = assertion.value("reference").toObject();
    return result;
}

bool ResolveExpectationFile(const QString& expectationsPath,
                            const QString& relativePath,
                            QString& absolutePath)
{
    if (QFileInfo(relativePath).isAbsolute())
        return false;

    const QDir baseDir = QFileInfo(expectationsPath).absoluteDir();
    absolutePath = QFileInfo(baseDir.filePath(relativePath)).absoluteFilePath();
    const QString relative = baseDir.relativeFilePath(absolutePath);
    return relative != ".." && !relative.startsWith("../") &&
           !relative.startsWith("..\\") && !QFileInfo(relative).isAbsolute();
}

QString SemanticImagePath(const QJsonObject& actual, const QString& outputDir)
{
    const qint64 frame = static_cast<qint64>(actual.value("frame").toDouble());
    const QString frameDir = QString("exact-frame-captures/frame%1")
        .arg(frame, 6, 10, QChar('0'));
    const QString surface = actual.value("surface").toString();
    QString filename;
    if (surface == "finalTop")
        filename = "final-top.png";
    else if (surface == "finalBottom")
        filename = "final-bottom.png";
    else
    {
        filename = QString("engine-%1-%2.png")
            .arg(actual.value("engine").toString().toLower(),
                 DebugViewExportName(actual.value("debugView").toString()));
    }
    return QDir(outputDir).filePath(frameDir + "/" + filename);
}

QString ActualImagePath(const QJsonObject& assertion, const QString& outputDir)
{
    return SemanticImagePath(assertion.value("actual").toObject(), outputDir);
}

bool ComputeImageMetrics(const QImage& actual,
                         const QImage& expected,
                         const QImage& exclusionMask,
                         const QRect& roi,
                         bool includeAlpha,
                         ImageMetrics& metrics,
                         QImage& actualCrop,
                         QImage& expectedCrop,
                         QImage& difference,
                         QImage& maskCrop)
{
    actualCrop = actual.copy(roi).convertToFormat(QImage::Format_RGBA8888);
    expectedCrop = expected.copy(roi).convertToFormat(QImage::Format_RGBA8888);
    if (!exclusionMask.isNull())
        maskCrop = exclusionMask.copy(roi).convertToFormat(QImage::Format_RGBA8888);

    const int width = roi.width();
    const int height = roi.height();
    QVector<quint8> changed(width * height, 0);
    difference = QImage(width, height, QImage::Format_RGBA8888);
    difference.fill(qRgba(0, 0, 0, 255));

    bool hasChangedBounds = false;
    for (int y = 0; y < height; y++)
    {
        const uchar* actualRow = actualCrop.constScanLine(y);
        const uchar* expectedRow = expectedCrop.constScanLine(y);
        const uchar* maskRow = maskCrop.isNull() ? nullptr : maskCrop.constScanLine(y);
        uchar* differenceRow = difference.scanLine(y);
        for (int x = 0; x < width; x++)
        {
            const int offset = x * 4;
            const bool excluded = maskRow &&
                (maskRow[offset] != 0 || maskRow[offset + 1] != 0 || maskRow[offset + 2] != 0);
            if (excluded)
            {
                differenceRow[offset] = 255;
                differenceRow[offset + 1] = 0;
                differenceRow[offset + 2] = 255;
                differenceRow[offset + 3] = 255;
                continue;
            }

            metrics.comparedPixels++;
            int pixelMaxError = 0;
            int channelErrors[4] {};
            const int channelCount = includeAlpha ? 4 : 3;
            for (int channel = 0; channel < channelCount; channel++)
            {
                const int error = std::abs(static_cast<int>(actualRow[offset + channel]) -
                                           static_cast<int>(expectedRow[offset + channel]));
                channelErrors[channel] = error;
                pixelMaxError = std::max(pixelMaxError, error);
                metrics.maxChannelError = std::max(metrics.maxChannelError, error);
            }
            differenceRow[offset] = static_cast<uchar>(channelErrors[0]);
            differenceRow[offset + 1] = static_cast<uchar>(channelErrors[1]);
            differenceRow[offset + 2] = static_cast<uchar>(
                includeAlpha ? std::max(channelErrors[2], channelErrors[3])
                             : channelErrors[2]);
            differenceRow[offset + 3] = 255;

            if (pixelMaxError > 0)
            {
                changed[y * width + x] = 1;
                metrics.changedPixels++;
                const QRect pixelBounds(roi.x() + x, roi.y() + y, 1, 1);
                if (!hasChangedBounds)
                {
                    metrics.changedBounds = pixelBounds;
                    hasChangedBounds = true;
                }
                else
                {
                    metrics.changedBounds = metrics.changedBounds.united(pixelBounds);
                }
            }
        }
    }

    if (metrics.comparedPixels <= 0)
        return false;
    metrics.changedPixelFraction = static_cast<double>(metrics.changedPixels) /
                                   static_cast<double>(metrics.comparedPixels);

    QVector<quint8> visited(width * height, 0);
    QVector<int> pending;
    for (int start = 0; start < changed.size(); start++)
    {
        if (!changed[start] || visited[start])
            continue;
        int componentSize = 0;
        pending.clear();
        pending.push_back(start);
        visited[start] = 1;
        while (!pending.isEmpty())
        {
            const int index = pending.back();
            pending.pop_back();
            componentSize++;
            const int x = index % width;
            const int y = index / width;
            const int neighbors[] = {
                x > 0 ? index - 1 : -1,
                x + 1 < width ? index + 1 : -1,
                y > 0 ? index - width : -1,
                y + 1 < height ? index + width : -1,
            };
            for (const int neighbor : neighbors)
            {
                if (neighbor >= 0 && changed[neighbor] && !visited[neighbor])
                {
                    visited[neighbor] = 1;
                    pending.push_back(neighbor);
                }
            }
        }
        metrics.maxConnectedComponentSize =
            std::max(metrics.maxConnectedComponentSize, componentSize);
    }
    return true;
}

QJsonObject EvaluateImageCompareAssertion(const QJsonObject& assertion,
                                          const QString& expectationsPath,
                                          const QString& outputDir)
{
    QJsonObject result;
    result["id"] = assertion.value("id").toString();
    result["type"] = "imageCompare";
    result["reference"] = assertion.value("reference").toObject();
    result["actual"] = assertion.value("actual").toObject();
    result["roi"] = assertion.value("roi").toArray();
    result["temporalScope"] = assertion.value("temporalScope").toString();

    QString expectedPath;
    if (!ResolveExpectationFile(expectationsPath,
                                assertion.value("expected").toString(),
                                expectedPath))
    {
        result["passed"] = false;
        result["message"] = "Expected image path escapes the expectations directory.";
        return result;
    }
    const QString actualPath = ActualImagePath(assertion, outputDir);
    result["actualFile"] = QDir(outputDir).relativeFilePath(actualPath);
    result["expectedFile"] = QDir(QFileInfo(expectationsPath).absolutePath())
        .relativeFilePath(expectedPath);

    QImage actual(actualPath);
    QImage expected(expectedPath);
    if (actual.isNull() || expected.isNull())
    {
        result["passed"] = false;
        result["message"] = QString("Could not load %1 image: %2")
            .arg(actual.isNull() ? "actual" : "expected",
                 actual.isNull() ? actualPath : expectedPath);
        return result;
    }
    actual = actual.convertToFormat(QImage::Format_RGBA8888);
    expected = expected.convertToFormat(QImage::Format_RGBA8888);
    if (actual.size() != expected.size())
    {
        result["passed"] = false;
        result["message"] = QString("Image sizes differ: actual %1x%2, expected %3x%4.")
            .arg(actual.width()).arg(actual.height()).arg(expected.width()).arg(expected.height());
        return result;
    }

    const QJsonArray roiValues = assertion.value("roi").toArray();
    const QRect roi(roiValues[0].toInt(), roiValues[1].toInt(),
                    roiValues[2].toInt(), roiValues[3].toInt());
    const QRect imageBounds(0, 0, actual.width(), actual.height());
    if (!imageBounds.contains(roi))
    {
        result["passed"] = false;
        result["message"] = QString("ROI %1,%2 %3x%4 is outside the %5x%6 image.")
            .arg(roi.x()).arg(roi.y()).arg(roi.width()).arg(roi.height())
            .arg(actual.width()).arg(actual.height());
        return result;
    }

    QImage exclusionMask;
    if (assertion.contains("exclusionMask"))
    {
        QString maskPath;
        if (!ResolveExpectationFile(expectationsPath,
                                    assertion.value("exclusionMask").toString(),
                                    maskPath))
        {
            result["passed"] = false;
            result["message"] = "Exclusion mask path escapes the expectations directory.";
            return result;
        }
        exclusionMask.load(maskPath);
        if (exclusionMask.isNull() || exclusionMask.size() != actual.size())
        {
            result["passed"] = false;
            result["message"] = "Exclusion mask is missing or does not match the compared image size.";
            return result;
        }
    }

    ImageMetrics metrics;
    QImage actualCrop;
    QImage expectedCrop;
    QImage difference;
    QImage maskCrop;
    if (!ComputeImageMetrics(actual, expected, exclusionMask, roi, true, metrics,
                             actualCrop, expectedCrop, difference, maskCrop))
    {
        result["passed"] = false;
        result["message"] = "The exclusion mask removed every pixel in the ROI.";
        return result;
    }

    const QJsonObject tolerance = assertion.value("tolerance").toObject();
    const double maxChangedFraction = tolerance.value("maxChangedPixelFraction").toDouble();
    const int maxChannelError = tolerance.value("maxChannelError").toInt();
    const int maxComponentSize = tolerance.value("maxConnectedComponentSize").toInt();
    const bool passed = metrics.changedPixelFraction <= maxChangedFraction &&
                        metrics.maxChannelError <= maxChannelError &&
                        metrics.maxConnectedComponentSize <= maxComponentSize;

    result["passed"] = passed;
    result["comparedPixels"] = metrics.comparedPixels;
    result["changedPixels"] = metrics.changedPixels;
    result["changedPixelFraction"] = metrics.changedPixelFraction;
    result["maxChannelError"] = metrics.maxChannelError;
    result["maxConnectedComponentSize"] = metrics.maxConnectedComponentSize;
    result["tolerance"] = tolerance;
    if (!metrics.changedBounds.isNull())
    {
        QJsonObject bounds;
        bounds["x"] = metrics.changedBounds.x();
        bounds["y"] = metrics.changedBounds.y();
        bounds["width"] = metrics.changedBounds.width();
        bounds["height"] = metrics.changedBounds.height();
        result["changedBounds"] = bounds;
    }

    if (passed)
    {
        result["message"] = QString("Compared %1 ROI pixel(s); %2 changed.")
            .arg(metrics.comparedPixels).arg(metrics.changedPixels);
        return result;
    }

    result["message"] = QString(
        "Image comparison failed: changed fraction %1 (max %2), channel error %3 (max %4), component %5 (max %6).")
        .arg(metrics.changedPixelFraction, 0, 'g', 10)
        .arg(maxChangedFraction, 0, 'g', 10)
        .arg(metrics.maxChannelError)
        .arg(maxChannelError)
        .arg(metrics.maxConnectedComponentSize)
        .arg(maxComponentSize);

    QDir runDir(outputDir);
    const QString artifactDirName = "assertion-failures/" +
                                    SafeName(assertion.value("id").toString());
    QJsonArray artifacts;
    QStringList artifactErrors;
    if (runDir.mkpath(artifactDirName))
    {
        QDir artifactDir(runDir.filePath(artifactDirName));
        const struct
        {
            const char* name;
            const QImage* image;
        } images[] = {
            {"actual.png", &actualCrop},
            {"expected.png", &expectedCrop},
            {"difference.png", &difference},
            {"exclusion-mask.png", &maskCrop},
        };
        for (const auto& artifact : images)
        {
            if (artifact.image->isNull())
                continue;
            const QString path = artifactDir.filePath(QString::fromLatin1(artifact.name));
            if (artifact.image->save(path, "PNG"))
                artifacts.push_back(runDir.relativeFilePath(path));
            else
                artifactErrors.push_back(QString("Could not save %1.").arg(path));
        }
    }
    else
    {
        artifactErrors.push_back(QString("Could not create %1.")
            .arg(runDir.filePath(artifactDirName)));
    }
    result["artifacts"] = artifacts;
    if (!artifactErrors.isEmpty())
        result["artifactErrors"] = QJsonArray::fromStringList(artifactErrors);
    return result;
}

quint32 HistogramColorKey(const uchar* pixel, bool includeAlpha)
{
    return static_cast<quint32>(pixel[0]) |
           (static_cast<quint32>(pixel[1]) << 8) |
           (static_cast<quint32>(pixel[2]) << 16) |
           (includeAlpha ? (static_cast<quint32>(pixel[3]) << 24) : 0);
}

quint32 HistogramColorKey(const QJsonArray& components, bool includeAlpha)
{
    return static_cast<quint32>(components[0].toInt()) |
           (static_cast<quint32>(components[1].toInt()) << 8) |
           (static_cast<quint32>(components[2].toInt()) << 16) |
           (includeAlpha ? (static_cast<quint32>(components[3].toInt()) << 24) : 0);
}

QJsonObject EvaluateImageHistogramAssertion(const QJsonObject& assertion,
                                            const QString& expectationsPath,
                                            const QString& outputDir)
{
    QJsonObject result;
    result["id"] = assertion.value("id").toString();
    result["type"] = "imageHistogram";
    result["reference"] = assertion.value("reference").toObject();
    result["actual"] = assertion.value("actual").toObject();
    result["roi"] = assertion.value("roi").toArray();
    result["colorMode"] = assertion.value("colorMode").toString();
    result["temporalScope"] = assertion.value("temporalScope").toString();

    const QString actualPath = ActualImagePath(assertion, outputDir);
    result["actualFile"] = QDir(outputDir).relativeFilePath(actualPath);
    QImage actual(actualPath);
    if (actual.isNull())
    {
        result["passed"] = false;
        result["message"] = QString("Could not load histogram image: %1").arg(actualPath);
        return result;
    }
    actual = actual.convertToFormat(QImage::Format_RGBA8888);

    const QJsonArray roiValues = assertion.value("roi").toArray();
    const QRect roi(roiValues[0].toInt(), roiValues[1].toInt(),
                    roiValues[2].toInt(), roiValues[3].toInt());
    if (!QRect(0, 0, actual.width(), actual.height()).contains(roi))
    {
        result["passed"] = false;
        result["message"] = QString("ROI %1,%2 %3x%4 is outside the %5x%6 image.")
            .arg(roi.x()).arg(roi.y()).arg(roi.width()).arg(roi.height())
            .arg(actual.width()).arg(actual.height());
        return result;
    }

    QImage exclusionMask;
    if (assertion.contains("exclusionMask"))
    {
        QString maskPath;
        if (!ResolveExpectationFile(expectationsPath,
                                    assertion.value("exclusionMask").toString(),
                                    maskPath))
        {
            result["passed"] = false;
            result["message"] = "Exclusion mask path escapes the expectations directory.";
            return result;
        }
        exclusionMask.load(maskPath);
        if (exclusionMask.isNull() || exclusionMask.size() != actual.size())
        {
            result["passed"] = false;
            result["message"] = "Exclusion mask is missing or does not match the histogram image size.";
            return result;
        }
        exclusionMask = exclusionMask.convertToFormat(QImage::Format_RGBA8888);
    }

    const bool includeAlpha = assertion.value("colorMode").toString() == "rgba";
    QHash<quint32, qint64> histogram;
    qint64 comparedPixels = 0;
    for (int y = roi.top(); y <= roi.bottom(); y++)
    {
        const uchar* actualRow = actual.constScanLine(y);
        const uchar* maskRow = exclusionMask.isNull() ? nullptr : exclusionMask.constScanLine(y);
        for (int x = roi.left(); x <= roi.right(); x++)
        {
            const int offset = x * 4;
            if (maskRow &&
                (maskRow[offset] != 0 || maskRow[offset + 1] != 0 || maskRow[offset + 2] != 0))
            {
                continue;
            }
            histogram[HistogramColorKey(actualRow + offset, includeAlpha)]++;
            comparedPixels++;
        }
    }
    if (comparedPixels == 0)
    {
        result["passed"] = false;
        result["message"] = "The exclusion mask removed every pixel in the histogram ROI.";
        return result;
    }

    bool passed = true;
    QStringList failures;
    qint64 listedPixels = 0;
    QJsonArray colorResults;
    const QJsonArray colors = assertion.value("colors").toArray();
    for (int i = 0; i < colors.size(); i++)
    {
        const QJsonObject expectedColor = colors[i].toObject();
        const QJsonArray value = expectedColor.value("value").toArray();
        const qint64 pixels = histogram.value(HistogramColorKey(value, includeAlpha), 0);
        const double fraction = static_cast<double>(pixels) / static_cast<double>(comparedPixels);
        listedPixels += pixels;

        QJsonObject colorResult;
        colorResult["value"] = value;
        colorResult["pixels"] = pixels;
        colorResult["fraction"] = fraction;
        bool colorPassed = true;
        if (expectedColor.contains("pixels"))
        {
            QString description;
            if (!EvaluateCondition(QString::number(pixels),
                                   expectedColor.value("pixels").toObject(),
                                   description))
            {
                colorPassed = false;
                failures.push_back(QString("color %1 pixels were %2; expected %3")
                    .arg(i).arg(pixels).arg(description));
            }
            colorResult["expectedPixels"] = expectedColor.value("pixels").toObject();
        }
        if (expectedColor.contains("fraction"))
        {
            QString description;
            if (!EvaluateCondition(QString::number(fraction, 'g', 17),
                                   expectedColor.value("fraction").toObject(),
                                   description))
            {
                colorPassed = false;
                failures.push_back(QString("color %1 fraction was %2; expected %3")
                    .arg(i).arg(fraction, 0, 'g', 10).arg(description));
            }
            colorResult["expectedFraction"] = expectedColor.value("fraction").toObject();
        }
        colorResult["passed"] = colorPassed;
        passed &= colorPassed;
        colorResults.push_back(colorResult);
    }

    QString uniqueDescription;
    const bool uniquePassed = EvaluateCondition(QString::number(histogram.size()),
        assertion.value("uniqueColors").toObject(), uniqueDescription);
    if (!uniquePassed)
        failures.push_back(QString("unique colors were %1; expected %2")
            .arg(histogram.size()).arg(uniqueDescription));
    passed &= uniquePassed;

    const qint64 unlistedPixels = comparedPixels - listedPixels;
    QString unlistedDescription;
    const bool unlistedPassed = EvaluateCondition(QString::number(unlistedPixels),
        assertion.value("unlistedPixels").toObject(), unlistedDescription);
    if (!unlistedPassed)
        failures.push_back(QString("unlisted pixels were %1; expected %2")
            .arg(unlistedPixels).arg(unlistedDescription));
    passed &= unlistedPassed;

    result["passed"] = passed;
    result["comparedPixels"] = comparedPixels;
    result["uniqueColors"] = histogram.size();
    result["expectedUniqueColors"] = assertion.value("uniqueColors").toObject();
    result["unlistedPixels"] = unlistedPixels;
    result["expectedUnlistedPixels"] = assertion.value("unlistedPixels").toObject();
    result["colors"] = colorResults;
    if (passed)
    {
        result["message"] = QString("Histogram passed for %1 pixel(s), %2 unique color(s).")
            .arg(comparedPixels).arg(histogram.size());
        return result;
    }

    result["message"] = "Histogram assertion failed: " + failures.join("; ") + ".";
    QDir runDir(outputDir);
    const QString artifactDirName = "assertion-failures/" +
                                    SafeName(assertion.value("id").toString());
    QJsonArray artifacts;
    if (runDir.mkpath(artifactDirName))
    {
        const QString path = QDir(runDir.filePath(artifactDirName)).filePath("actual.png");
        if (actual.copy(roi).save(path, "PNG"))
            artifacts.push_back(runDir.relativeFilePath(path));
    }
    result["artifacts"] = artifacts;
    return result;
}

bool LoadPerceptualPrototype(const QString& expectationsPath,
                             const QJsonObject& spec,
                             int gridWidth,
                             int gridHeight,
                             QVector<int>& luma,
                             QString& error)
{
    QString path;
    if (!ResolveExpectationFile(expectationsPath, spec.value("file").toString(), path))
    {
        error = "Prototype path escapes the expectations directory.";
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        error = QString("Could not open prototype %1: %2").arg(path, file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject())
    {
        error = QString("Prototype %1 is not valid JSON: %2").arg(path, parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonArray grid = root.value("grid").toArray();
    const QJsonArray values = root.value("luma").toArray();
    if (root.value("schema").toInt(-1) != 1 || grid.size() != 2 ||
        grid[0].toInt(-1) != gridWidth || grid[1].toInt(-1) != gridHeight ||
        values.size() != gridWidth * gridHeight)
    {
        error = QString("Prototype %1 must use schema 1, grid %2x%3 and %4 luma values.")
            .arg(path).arg(gridWidth).arg(gridHeight).arg(gridWidth * gridHeight);
        return false;
    }
    luma.reserve(values.size());
    for (int i = 0; i < values.size(); i++)
    {
        int value = 0;
        if (!JsonNonNegativeInteger(values[i], value) || value > 255)
        {
            error = QString("Prototype %1 luma[%2] must be an integer from 0 to 255.")
                .arg(path).arg(i);
            return false;
        }
        luma.push_back(value);
    }
    return true;
}

bool ComputePerceptualSignature(const QImage& image,
                                const QImage& exclusionMask,
                                const QRect& roi,
                                int gridWidth,
                                int gridHeight,
                                QVector<int>& signature,
                                QString& error)
{
    signature.reserve(gridWidth * gridHeight);
    for (int gridY = 0; gridY < gridHeight; gridY++)
    {
        const int yStart = roi.y() + gridY * roi.height() / gridHeight;
        const int yEnd = roi.y() + (gridY + 1) * roi.height() / gridHeight;
        for (int gridX = 0; gridX < gridWidth; gridX++)
        {
            const int xStart = roi.x() + gridX * roi.width() / gridWidth;
            const int xEnd = roi.x() + (gridX + 1) * roi.width() / gridWidth;
            qint64 lumaSum = 0;
            qint64 pixelCount = 0;
            for (int y = yStart; y < yEnd; y++)
            {
                const uchar* imageRow = image.constScanLine(y);
                const uchar* maskRow = exclusionMask.isNull() ? nullptr : exclusionMask.constScanLine(y);
                for (int x = xStart; x < xEnd; x++)
                {
                    const int offset = x * 4;
                    if (maskRow &&
                        (maskRow[offset] != 0 || maskRow[offset + 1] != 0 || maskRow[offset + 2] != 0))
                    {
                        continue;
                    }
                    const int luma = (77 * imageRow[offset] +
                                      150 * imageRow[offset + 1] +
                                      29 * imageRow[offset + 2] + 128) >> 8;
                    lumaSum += luma;
                    pixelCount++;
                }
            }
            if (pixelCount == 0)
            {
                error = QString("Exclusion mask removed every pixel from signature cell %1,%2.")
                    .arg(gridX).arg(gridY);
                return false;
            }
            signature.push_back(static_cast<int>((lumaSum + pixelCount / 2) / pixelCount));
        }
    }
    return true;
}

double MeanAbsoluteLumaDistance(const QVector<int>& actual,
                                const QVector<int>& expected)
{
    qint64 total = 0;
    for (int i = 0; i < actual.size(); i++)
        total += std::abs(actual[i] - expected[i]);
    return static_cast<double>(total) / static_cast<double>(actual.size());
}

QImage PerceptualSignatureImage(const QVector<int>& values,
                                int width,
                                int height)
{
    QImage image(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; y++)
    {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < width; x++)
        {
            const uchar value = static_cast<uchar>(values[y * width + x]);
            const int offset = x * 4;
            row[offset] = value;
            row[offset + 1] = value;
            row[offset + 2] = value;
            row[offset + 3] = 255;
        }
    }
    return image.scaled(width * 16, height * 16,
                        Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

QJsonObject EvaluatePerceptualSignatureAssertion(const QJsonObject& assertion,
                                                 const QString& expectationsPath,
                                                 const QString& outputDir)
{
    QJsonObject result;
    result["id"] = assertion.value("id").toString();
    result["type"] = "perceptualSignature";
    result["reference"] = assertion.value("reference").toObject();
    result["actual"] = assertion.value("actual").toObject();
    result["roi"] = assertion.value("roi").toArray();
    result["grid"] = assertion.value("grid").toArray();
    result["prototypes"] = assertion.value("prototypes").toArray();
    result["distanceMetric"] = assertion.value("distanceMetric").toString();
    result["maxDistance"] = assertion.value("maxDistance").toDouble();
    result["expectedPrototype"] = assertion.value("expectedPrototype").toString();
    result["temporalScope"] = assertion.value("temporalScope").toString();

    const QString actualPath = ActualImagePath(assertion, outputDir);
    result["actualFile"] = QDir(outputDir).relativeFilePath(actualPath);
    QImage actual(actualPath);
    if (actual.isNull())
    {
        result["passed"] = false;
        result["message"] = QString("Could not load perceptual image: %1").arg(actualPath);
        return result;
    }
    actual = actual.convertToFormat(QImage::Format_RGBA8888);

    const QJsonArray roiValues = assertion.value("roi").toArray();
    const QRect roi(roiValues[0].toInt(), roiValues[1].toInt(),
                    roiValues[2].toInt(), roiValues[3].toInt());
    if (!QRect(0, 0, actual.width(), actual.height()).contains(roi))
    {
        result["passed"] = false;
        result["message"] = QString("ROI %1,%2 %3x%4 is outside the %5x%6 image.")
            .arg(roi.x()).arg(roi.y()).arg(roi.width()).arg(roi.height())
            .arg(actual.width()).arg(actual.height());
        return result;
    }

    QImage exclusionMask;
    if (assertion.contains("exclusionMask"))
    {
        QString maskPath;
        if (!ResolveExpectationFile(expectationsPath,
                                    assertion.value("exclusionMask").toString(),
                                    maskPath))
        {
            result["passed"] = false;
            result["message"] = "Exclusion mask path escapes the expectations directory.";
            return result;
        }
        exclusionMask.load(maskPath);
        if (exclusionMask.isNull() || exclusionMask.size() != actual.size())
        {
            result["passed"] = false;
            result["message"] = "Exclusion mask is missing or does not match the perceptual image size.";
            return result;
        }
        exclusionMask = exclusionMask.convertToFormat(QImage::Format_RGBA8888);
    }

    const QJsonArray grid = assertion.value("grid").toArray();
    const int gridWidth = grid[0].toInt();
    const int gridHeight = grid[1].toInt();
    QVector<int> actualSignature;
    QString signatureError;
    if (!ComputePerceptualSignature(actual, exclusionMask, roi,
                                    gridWidth, gridHeight,
                                    actualSignature, signatureError))
    {
        result["passed"] = false;
        result["message"] = signatureError;
        return result;
    }

    QStringList nearestIds;
    double nearestDistance = std::numeric_limits<double>::infinity();
    QVector<int> expectedSignature;
    QJsonArray distanceResults;
    const QString expectedId = assertion.value("expectedPrototype").toString();
    for (const QJsonValue& prototypeValue : assertion.value("prototypes").toArray())
    {
        const QJsonObject prototype = prototypeValue.toObject();
        QVector<int> prototypeSignature;
        QString prototypeError;
        if (!LoadPerceptualPrototype(expectationsPath, prototype,
                                     gridWidth, gridHeight,
                                     prototypeSignature, prototypeError))
        {
            result["passed"] = false;
            result["message"] = QString("Prototype %1 failed: %2")
                .arg(prototype.value("id").toString(), prototypeError);
            return result;
        }
        const double distance = MeanAbsoluteLumaDistance(actualSignature,
                                                         prototypeSignature);
        QJsonObject distanceResult;
        distanceResult["id"] = prototype.value("id").toString();
        distanceResult["file"] = prototype.value("file").toString();
        distanceResult["distance"] = distance;
        distanceResults.push_back(distanceResult);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestIds = {prototype.value("id").toString()};
        }
        else if (distance == nearestDistance)
        {
            nearestIds.push_back(prototype.value("id").toString());
        }
        if (prototype.value("id").toString() == expectedId)
            expectedSignature = prototypeSignature;
    }

    const double maxDistance = assertion.value("maxDistance").toDouble();
    const bool uniqueNearest = nearestIds.size() == 1;
    const QString nearestId = uniqueNearest ? nearestIds.front() : QString();
    const bool passed = uniqueNearest && nearestId == expectedId &&
                        nearestDistance <= maxDistance;
    result["passed"] = passed;
    result["nearestPrototypes"] = QJsonArray::fromStringList(nearestIds);
    if (uniqueNearest)
        result["nearestPrototype"] = nearestId;
    result["distance"] = nearestDistance;
    result["prototypeDistances"] = distanceResults;
    if (passed)
    {
        result["message"] = QString("Matched prototype %1 at mean absolute luma distance %2 (max %3).")
            .arg(nearestId)
            .arg(nearestDistance, 0, 'g', 10)
            .arg(maxDistance, 0, 'g', 10);
        return result;
    }

    result["message"] = QString("Perceptual signature nearest prototype(s) %1 at distance %2; expected %3 within %4.")
        .arg(nearestIds.join(", "))
        .arg(nearestDistance, 0, 'g', 10)
        .arg(expectedId)
        .arg(maxDistance, 0, 'g', 10);

    QVector<int> differenceSignature;
    differenceSignature.reserve(actualSignature.size());
    for (int i = 0; i < actualSignature.size(); i++)
        differenceSignature.push_back(std::abs(actualSignature[i] - expectedSignature[i]));

    QDir runDir(outputDir);
    const QString artifactDirName = "assertion-failures/" +
                                    SafeName(assertion.value("id").toString());
    QJsonArray artifacts;
    if (runDir.mkpath(artifactDirName))
    {
        QDir artifactDir(runDir.filePath(artifactDirName));
        const QImage actualRoi = actual.copy(roi);
        const QImage actualGrid = PerceptualSignatureImage(actualSignature, gridWidth, gridHeight);
        const QImage expectedGrid = PerceptualSignatureImage(expectedSignature, gridWidth, gridHeight);
        const QImage differenceGrid = PerceptualSignatureImage(differenceSignature, gridWidth, gridHeight);
        const struct
        {
            const char* name;
            const QImage* image;
        } images[] = {
            {"actual.png", &actualRoi},
            {"actual-signature.png", &actualGrid},
            {"expected-signature.png", &expectedGrid},
            {"difference-signature.png", &differenceGrid},
        };
        for (const auto& artifact : images)
        {
            const QString path = artifactDir.filePath(QString::fromLatin1(artifact.name));
            if (artifact.image->save(path, "PNG"))
                artifacts.push_back(runDir.relativeFilePath(path));
        }
    }
    result["artifacts"] = artifacts;
    return result;
}

QJsonObject EvaluatePerceptualSequenceAssertion(const QJsonObject& assertion,
                                                const QString& expectationsPath,
                                                const QString& outputDir)
{
    QJsonObject result;
    result["id"] = assertion.value("id").toString();
    result["type"] = "perceptualSequence";
    result["reference"] = assertion.value("reference").toObject();
    result["actual"] = assertion.value("actual").toObject();
    result["frames"] = assertion.value("frames").toObject();
    result["roi"] = assertion.value("roi").toArray();
    result["grid"] = assertion.value("grid").toArray();
    result["prototypes"] = assertion.value("prototypes").toArray();
    result["distanceMetric"] = assertion.value("distanceMetric").toString();
    result["maxDistance"] = assertion.value("maxDistance").toDouble();
    result["expectedRuns"] = assertion.value("expectedRuns").toArray();
    result["temporalScope"] = assertion.value("temporalScope").toString();

    const QJsonArray grid = assertion.value("grid").toArray();
    const int gridWidth = grid[0].toInt();
    const int gridHeight = grid[1].toInt();
    QStringList prototypeOrder;
    QHash<QString, QVector<int>> prototypeSignatures;
    for (const QJsonValue& prototypeValue : assertion.value("prototypes").toArray())
    {
        const QJsonObject prototype = prototypeValue.toObject();
        const QString id = prototype.value("id").toString();
        QVector<int> signature;
        QString prototypeError;
        if (!LoadPerceptualPrototype(expectationsPath, prototype,
                                     gridWidth, gridHeight,
                                     signature, prototypeError))
        {
            result["passed"] = false;
            result["message"] = QString("Prototype %1 failed: %2").arg(id, prototypeError);
            return result;
        }
        prototypeOrder.push_back(id);
        prototypeSignatures.insert(id, signature);
    }

    QImage exclusionMask;
    if (assertion.contains("exclusionMask"))
    {
        QString maskPath;
        if (!ResolveExpectationFile(expectationsPath,
                                    assertion.value("exclusionMask").toString(),
                                    maskPath))
        {
            result["passed"] = false;
            result["message"] = "Exclusion mask path escapes the expectations directory.";
            return result;
        }
        exclusionMask.load(maskPath);
        if (exclusionMask.isNull())
        {
            result["passed"] = false;
            result["message"] = "Perceptual sequence exclusion mask is missing.";
            return result;
        }
        exclusionMask = exclusionMask.convertToFormat(QImage::Format_RGBA8888);
    }

    QStringList expectedLabels;
    for (const QJsonValue& runValue : assertion.value("expectedRuns").toArray())
    {
        const QJsonObject run = runValue.toObject();
        const QString id = run.value("id").toString();
        const int length = run.value("length").toInt();
        for (int i = 0; i < length; i++)
            expectedLabels.push_back(id);
    }

    const QJsonObject frameRange = assertion.value("frames").toObject();
    const qint64 minFrame = static_cast<qint64>(frameRange.value("min").toDouble());
    const qint64 maxFrame = static_cast<qint64>(frameRange.value("max").toDouble());
    const QJsonArray roiValues = assertion.value("roi").toArray();
    const QRect roi(roiValues[0].toInt(), roiValues[1].toInt(),
                    roiValues[2].toInt(), roiValues[3].toInt());
    const double maxDistance = assertion.value("maxDistance").toDouble();
    QJsonArray frameResults;
    QStringList actualLabels;
    QVector<double> frameDistances;
    QSet<QString> observedPrototypes;
    QStringList failures;
    QImage failureImage;
    QVector<int> failureSignature;
    QVector<int> failureExpectedSignature;
    qint64 failureFrame = -1;

    for (qint64 frame = minFrame; frame <= maxFrame; frame++)
    {
        const int frameIndex = static_cast<int>(frame - minFrame);
        const QString expectedId = expectedLabels[frameIndex];
        QJsonObject selection = assertion.value("actual").toObject();
        selection["frame"] = static_cast<double>(frame);
        const QString path = SemanticImagePath(selection, outputDir);
        QJsonObject frameResult;
        frameResult["frame"] = static_cast<double>(frame);
        frameResult["expectedPrototype"] = expectedId;
        frameResult["actualFile"] = QDir(outputDir).relativeFilePath(path);
        QImage actual(path);
        if (actual.isNull())
        {
            const QString label = "<missing>";
            actualLabels.push_back(label);
            frameDistances.push_back(std::numeric_limits<double>::infinity());
            frameResult["passed"] = false;
            frameResult["message"] = QString("Could not load %1").arg(path);
            frameResults.push_back(frameResult);
            if (failures.size() < 8)
                failures.push_back(QString("frame %1 image is missing").arg(frame));
            continue;
        }
        actual = actual.convertToFormat(QImage::Format_RGBA8888);
        if (!QRect(0, 0, actual.width(), actual.height()).contains(roi) ||
            (!exclusionMask.isNull() && exclusionMask.size() != actual.size()))
        {
            const QString label = "<invalid-image>";
            actualLabels.push_back(label);
            frameDistances.push_back(std::numeric_limits<double>::infinity());
            frameResult["passed"] = false;
            frameResult["message"] = "ROI or exclusion mask does not match the image.";
            frameResults.push_back(frameResult);
            if (failures.size() < 8)
                failures.push_back(QString("frame %1 image geometry is invalid").arg(frame));
            continue;
        }

        QVector<int> signature;
        QString signatureError;
        if (!ComputePerceptualSignature(actual, exclusionMask, roi,
                                        gridWidth, gridHeight,
                                        signature, signatureError))
        {
            const QString label = "<invalid-signature>";
            actualLabels.push_back(label);
            frameDistances.push_back(std::numeric_limits<double>::infinity());
            frameResult["passed"] = false;
            frameResult["message"] = signatureError;
            frameResults.push_back(frameResult);
            if (failures.size() < 8)
                failures.push_back(QString("frame %1: %2").arg(frame).arg(signatureError));
            continue;
        }

        QStringList nearestIds;
        double nearestDistance = std::numeric_limits<double>::infinity();
        QJsonArray prototypeDistances;
        for (const QString& id : prototypeOrder)
        {
            const double distance = MeanAbsoluteLumaDistance(
                signature, prototypeSignatures.value(id));
            QJsonObject distanceResult;
            distanceResult["id"] = id;
            distanceResult["distance"] = distance;
            prototypeDistances.push_back(distanceResult);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearestIds = {id};
            }
            else if (distance == nearestDistance)
            {
                nearestIds.push_back(id);
            }
        }
        const bool uniqueNearest = nearestIds.size() == 1;
        const QString actualId = uniqueNearest ? nearestIds.front() : "<ambiguous>";
        const bool withinThreshold = nearestDistance <= maxDistance;
        const bool framePassed = uniqueNearest && withinThreshold && actualId == expectedId;
        actualLabels.push_back(actualId);
        frameDistances.push_back(nearestDistance);
        if (uniqueNearest)
            observedPrototypes.insert(actualId);
        frameResult["passed"] = framePassed;
        frameResult["actualPrototype"] = actualId;
        frameResult["nearestPrototypes"] = QJsonArray::fromStringList(nearestIds);
        frameResult["distance"] = nearestDistance;
        frameResult["prototypeDistances"] = prototypeDistances;
        frameResults.push_back(frameResult);
        if (!framePassed)
        {
            if (failures.size() < 8)
            {
                failures.push_back(QString("frame %1 classified %2 at %3; expected %4 within %5")
                    .arg(frame).arg(actualId)
                    .arg(nearestDistance, 0, 'g', 10)
                    .arg(expectedId).arg(maxDistance, 0, 'g', 10));
            }
            if (failureFrame < 0)
            {
                failureFrame = frame;
                failureImage = actual;
                failureSignature = signature;
                failureExpectedSignature = prototypeSignatures.value(expectedId);
            }
        }
    }

    struct PoseRun
    {
        QString id;
        qint64 startFrame = 0;
        qint64 endFrame = 0;
        int length = 0;
        double maxDistance = 0.0;
    };
    QVector<PoseRun> runs;
    for (int i = 0; i < actualLabels.size(); i++)
    {
        if (runs.isEmpty() || runs.back().id != actualLabels[i])
        {
            PoseRun run;
            run.id = actualLabels[i];
            run.startFrame = minFrame + i;
            run.endFrame = minFrame + i;
            run.length = 1;
            run.maxDistance = frameDistances[i];
            runs.push_back(run);
        }
        else
        {
            PoseRun& run = runs.back();
            run.endFrame = minFrame + i;
            run.length++;
            run.maxDistance = std::max(run.maxDistance, frameDistances[i]);
        }
    }
    QJsonArray runResults;
    int invalidCycleTransitions = 0;
    int singleFrameRuns = 0;
    for (int i = 0; i < runs.size(); i++)
    {
        const PoseRun& run = runs[i];
        QJsonObject runResult;
        runResult["id"] = run.id;
        runResult["startFrame"] = static_cast<double>(run.startFrame);
        runResult["endFrame"] = static_cast<double>(run.endFrame);
        runResult["length"] = run.length;
        if (std::isfinite(run.maxDistance))
            runResult["maxDistance"] = run.maxDistance;
        runResults.push_back(runResult);
        if (run.length == 1)
            singleFrameRuns++;
        if (i == 0)
            continue;
        const int previousIndex = prototypeOrder.indexOf(runs[i - 1].id);
        const int currentIndex = prototypeOrder.indexOf(run.id);
        if (previousIndex < 0 || currentIndex < 0 ||
            currentIndex != (previousIndex + 1) % prototypeOrder.size())
        {
            invalidCycleTransitions++;
        }
    }

    QJsonArray observed;
    for (const QString& id : prototypeOrder)
    {
        if (observedPrototypes.contains(id))
            observed.push_back(id);
    }
    const int transitions = runs.isEmpty() ? 0 : static_cast<int>(runs.size()) - 1;
    result["passed"] = failures.isEmpty();
    result["framesCompared"] = actualLabels.size();
    result["frameResults"] = frameResults;
    result["runs"] = runResults;
    result["runCount"] = runs.size();
    result["transitions"] = transitions;
    result["singleFrameRuns"] = singleFrameRuns;
    result["invalidCycleTransitions"] = invalidCycleTransitions;
    result["observedPrototypes"] = observed;
    if (failures.isEmpty())
    {
        result["message"] = QString("Classified %1 consecutive frame(s) into %2 expected pose run(s); %3 transition(s), no mismatches.")
            .arg(actualLabels.size()).arg(runs.size()).arg(transitions);
        return result;
    }

    result["message"] = "Perceptual sequence failed: " + failures.join("; ") + ".";
    QJsonArray artifacts;
    if (failureFrame >= 0 && !failureImage.isNull() && !failureSignature.isEmpty())
    {
        QDir runDir(outputDir);
        const QString artifactDirName = "assertion-failures/" +
                                        SafeName(assertion.value("id").toString()) +
                                        QString("-frame-%1").arg(failureFrame);
        if (runDir.mkpath(artifactDirName))
        {
            QDir artifactDir(runDir.filePath(artifactDirName));
            QVector<int> differenceSignature;
            differenceSignature.reserve(failureSignature.size());
            for (int i = 0; i < failureSignature.size(); i++)
            {
                differenceSignature.push_back(
                    std::abs(failureSignature[i] - failureExpectedSignature[i]));
            }
            const QImage actualRoi = failureImage.copy(roi);
            const QImage actualGrid = PerceptualSignatureImage(
                failureSignature, gridWidth, gridHeight);
            const QImage expectedGrid = PerceptualSignatureImage(
                failureExpectedSignature, gridWidth, gridHeight);
            const QImage differenceGrid = PerceptualSignatureImage(
                differenceSignature, gridWidth, gridHeight);
            const struct
            {
                const char* name;
                const QImage* image;
            } images[] = {
                {"actual.png", &actualRoi},
                {"actual-signature.png", &actualGrid},
                {"expected-signature.png", &expectedGrid},
                {"difference-signature.png", &differenceGrid},
            };
            for (const auto& artifact : images)
            {
                const QString path = artifactDir.filePath(QString::fromLatin1(artifact.name));
                if (artifact.image->save(path, "PNG"))
                    artifacts.push_back(runDir.relativeFilePath(path));
            }
        }
    }
    result["artifacts"] = artifacts;
    return result;
}

struct CsvValueRun
{
    QString value;
    QString startFrame;
    QString endFrame;
    int length = 0;
};

QJsonArray CsvRunsJson(const QVector<CsvValueRun>& runs)
{
    QJsonArray result;
    const int previewEachEnd = 32;
    for (int i = 0; i < runs.size(); i++)
    {
        if (runs.size() > previewEachEnd * 2 &&
            i == previewEachEnd)
        {
            QJsonObject omitted;
            omitted["omittedRuns"] = runs.size() - previewEachEnd * 2;
            result.push_back(omitted);
            i = runs.size() - previewEachEnd - 1;
            continue;
        }
        QJsonObject run;
        run["value"] = runs[i].value;
        run["startFrame"] = runs[i].startFrame;
        run["endFrame"] = runs[i].endFrame;
        run["length"] = runs[i].length;
        result.push_back(run);
    }
    return result;
}

bool CsvColumnsExist(const CsvTable& table,
                     const QJsonObject& group,
                     const QString& groupName,
                     QHash<QString, int>& indexes,
                     QString& error)
{
    for (auto it = group.begin(); it != group.end(); ++it)
    {
        const int index = table.columns.indexOf(it.key());
        if (index < 0)
        {
            error = QString("CSV does not contain %1 column %2.").arg(groupName, it.key());
            return false;
        }
        indexes.insert(it.key(), index);
    }
    return true;
}

QVector<int> SelectCsvRows(const CsvTable& table,
                           const QJsonObject& where,
                           const QHash<QString, int>& indexes)
{
    QVector<int> selected;
    for (int rowIndex = 0; rowIndex < table.rows.size(); rowIndex++)
    {
        bool matches = true;
        for (auto it = where.begin(); it != where.end(); ++it)
        {
            QString ignored;
            matches &= EvaluateCondition(table.rows[rowIndex][indexes.value(it.key())],
                                         it.value().toObject(), ignored);
        }
        if (matches)
            selected.push_back(rowIndex);
    }
    return selected;
}

bool CheckConsecutiveFrames(const CsvTable& table,
                            const QVector<int>& rows,
                            int frameIndex,
                            QString& error)
{
    qint64 previous = 0;
    for (int i = 0; i < rows.size(); i++)
    {
        bool ok = false;
        const qint64 frame = table.rows[rows[i]][frameIndex].toLongLong(&ok);
        if (!ok)
        {
            error = QString("CSV row %1 frame is not an integer.").arg(rows[i] + 2);
            return false;
        }
        if (i > 0 && frame != previous + 1)
        {
            error = QString("Selected frames are not consecutive: %1 is followed by %2.")
                .arg(previous).arg(frame);
            return false;
        }
        previous = frame;
    }
    return true;
}

bool MetricCondition(const QString& name,
                     double actual,
                     const QJsonObject& condition,
                     QStringList& failures)
{
    const QString actualText = QString::number(actual, 'g', 17);
    QString expected;
    const bool passed = EvaluateCondition(actualText, condition, expected);
    if (!passed)
        failures.push_back(QString("%1 was %2; expected %3").arg(name, actualText, expected));
    return passed;
}

QJsonObject EvaluateCsvSequenceAssertion(const QJsonObject& assertion,
                                         const CsvTable& table)
{
    QJsonObject result;
    result["id"] = assertion.value("id").toString();
    result["type"] = "csvSequence";
    result["csv"] = assertion.value("csv").toString();
    result["column"] = assertion.value("column").toString();
    result["reference"] = assertion.value("reference").toObject();

    QHash<QString, int> indexes;
    QString error;
    const QJsonObject where = assertion.value("where").toObject();
    if (!CsvColumnsExist(table, where, "where", indexes, error))
        return FailureResult(assertion, error);
    const QString column = assertion.value("column").toString();
    const int valueIndex = table.columns.indexOf(column);
    const int frameIndex = table.columns.indexOf("frame");
    if (valueIndex < 0 || frameIndex < 0)
        return FailureResult(assertion, valueIndex < 0
            ? QString("CSV does not contain sequence column %1.").arg(column)
            : QString("CSV does not contain frame column."));

    const QVector<int> selected = SelectCsvRows(table, where, indexes);
    if (assertion.value("requireConsecutiveFrames").toBool() &&
        !CheckConsecutiveFrames(table, selected, frameIndex, error))
    {
        return FailureResult(assertion, error);
    }

    QStringList failures;
    MetricCondition("rows", selected.size(), assertion.value("rows").toObject(), failures);
    const QJsonArray allowedValues = assertion.value("allowedValues").toArray();
    QVector<CsvValueRun> runs;
    for (const int rowIndex : selected)
    {
        const QString value = table.rows[rowIndex][valueIndex];
        if (!allowedValues.isEmpty())
        {
            bool allowed = false;
            for (const QJsonValue& expected : allowedValues)
                allowed |= EqualCell(value, expected);
            if (!allowed)
                failures.push_back(QString("frame %1 has disallowed value %2")
                    .arg(table.rows[rowIndex][frameIndex], value));
        }
        if (runs.isEmpty() || runs.back().value != value)
        {
            CsvValueRun run;
            run.value = value;
            run.startFrame = table.rows[rowIndex][frameIndex];
            run.endFrame = run.startFrame;
            run.length = 1;
            runs.push_back(run);
        }
        else
        {
            runs.back().endFrame = table.rows[rowIndex][frameIndex];
            runs.back().length++;
        }
    }

    const int transitions = runs.isEmpty() ? 0 : runs.size() - 1;
    int reversions = 0;
    int singleFrameReversions = 0;
    int longestRun = 0;
    for (int i = 0; i < runs.size(); i++)
    {
        longestRun = std::max(longestRun, runs[i].length);
        if (i > 0 && i + 1 < runs.size() && runs[i - 1].value == runs[i + 1].value)
        {
            reversions++;
            if (runs[i].length == 1)
                singleFrameReversions++;
        }
    }
    MetricCondition("transitions", transitions, assertion.value("transitions").toObject(), failures);
    MetricCondition("reversions", reversions, assertion.value("reversions").toObject(), failures);
    MetricCondition("single-frame reversions", singleFrameReversions,
                    assertion.value("singleFrameReversions").toObject(), failures);
    MetricCondition("longest run", longestRun, assertion.value("longestRun").toObject(), failures);

    result["passed"] = failures.isEmpty();
    result["rows"] = selected.size();
    result["runCount"] = runs.size();
    result["transitions"] = transitions;
    result["reversions"] = reversions;
    result["singleFrameReversions"] = singleFrameReversions;
    result["longestRun"] = longestRun;
    result["runs"] = CsvRunsJson(runs);
    result["message"] = failures.isEmpty()
        ? QString("Sequence passed: %1 row(s), %2 run(s), %3 transition(s).")
            .arg(selected.size()).arg(runs.size()).arg(transitions)
        : QString("Sequence assertion failed: %1.").arg(failures.join("; "));
    return result;
}

QJsonObject EvaluateCsvRunLimitAssertion(const QJsonObject& assertion,
                                         const CsvTable& table)
{
    QJsonObject result;
    result["id"] = assertion.value("id").toString();
    result["type"] = "csvRunLimit";
    result["csv"] = assertion.value("csv").toString();
    result["reference"] = assertion.value("reference").toObject();

    const QJsonObject where = assertion.value("where").toObject();
    const QJsonObject match = assertion.value("match").toObject();
    const QJsonObject matchedExpect = assertion.value("matchedExpect").toObject();
    QHash<QString, int> indexes;
    QString error;
    if (!CsvColumnsExist(table, where, "where", indexes, error) ||
        !CsvColumnsExist(table, match, "match", indexes, error) ||
        !CsvColumnsExist(table, matchedExpect, "matchedExpect", indexes, error))
    {
        return FailureResult(assertion, error);
    }
    const int frameIndex = table.columns.indexOf("frame");
    if (frameIndex < 0)
        return FailureResult(assertion, "CSV does not contain frame column.");
    const QJsonObject area = assertion.value("area").toObject();
    int areaYStartIndex = -1;
    int areaYEndIndex = -1;
    qint64 areaWidth = 0;
    if (!area.isEmpty())
    {
        const QString yStartColumn = area.value("yStartColumn").toString();
        const QString yEndColumn = area.value("yEndColumn").toString();
        areaYStartIndex = table.columns.indexOf(yStartColumn);
        areaYEndIndex = table.columns.indexOf(yEndColumn);
        JsonInteger(area.value("width"), areaWidth);
        if (areaYStartIndex < 0 || areaYEndIndex < 0)
        {
            return FailureResult(
                assertion,
                QString("CSV does not contain area column %1.")
                    .arg(areaYStartIndex < 0 ? yStartColumn : yEndColumn));
        }
    }
    const QVector<int> selected = SelectCsvRows(table, where, indexes);
    if (assertion.value("requireConsecutiveFrames").toBool() &&
        !CheckConsecutiveFrames(table, selected, frameIndex, error))
    {
        return FailureResult(assertion, error);
    }

    QStringList failures;
    MetricCondition("rows", selected.size(), assertion.value("rows").toObject(), failures);
    QVector<CsvValueRun> runs;
    qint64 matchingRows = 0;
    qint64 matchingArea = 0;
    bool previousMatched = false;
    for (const int rowIndex : selected)
    {
        bool rowMatches = true;
        for (auto it = match.begin(); it != match.end(); ++it)
        {
            QString ignored;
            rowMatches &= EvaluateCondition(table.rows[rowIndex][indexes.value(it.key())],
                                            it.value().toObject(), ignored);
        }
        if (!rowMatches)
        {
            previousMatched = false;
            continue;
        }

        matchingRows++;
        if (!area.isEmpty())
        {
            bool yStartOK = false;
            bool yEndOK = false;
            const qint64 yStart =
                table.rows[rowIndex][areaYStartIndex].toLongLong(&yStartOK);
            const qint64 yEnd =
                table.rows[rowIndex][areaYEndIndex].toLongLong(&yEndOK);
            if (!yStartOK || !yEndOK || yEnd < yStart)
            {
                failures.push_back(
                    QString("frame %1 has invalid area range %2-%3")
                        .arg(table.rows[rowIndex][frameIndex],
                             table.rows[rowIndex][areaYStartIndex],
                             table.rows[rowIndex][areaYEndIndex]));
            }
            else
                matchingArea += (yEnd - yStart) * areaWidth;
        }
        for (auto it = matchedExpect.begin(); it != matchedExpect.end(); ++it)
        {
            const QString actual = table.rows[rowIndex][indexes.value(it.key())];
            QString expected;
            if (!EvaluateCondition(actual, it.value().toObject(), expected))
            {
                failures.push_back(QString("frame %1 matched row column %2 was %3; expected %4")
                    .arg(table.rows[rowIndex][frameIndex], it.key(), actual, expected));
            }
        }
        if (!previousMatched)
        {
            CsvValueRun run;
            run.value = "matched";
            run.startFrame = table.rows[rowIndex][frameIndex];
            run.endFrame = run.startFrame;
            run.length = 1;
            runs.push_back(run);
        }
        else
        {
            runs.back().endFrame = table.rows[rowIndex][frameIndex];
            runs.back().length++;
        }
        previousMatched = true;
    }

    int longestRun = 0;
    for (const CsvValueRun& run : runs)
        longestRun = std::max(longestRun, run.length);
    const double fraction = selected.isEmpty() ? 0.0 :
        static_cast<double>(matchingRows) / static_cast<double>(selected.size());
    MetricCondition("matching rows", matchingRows,
                    assertion.value("matchingRows").toObject(), failures);
    MetricCondition("matching fraction", fraction,
                    assertion.value("matchingFraction").toObject(), failures);
    MetricCondition("runs", runs.size(), assertion.value("runs").toObject(), failures);
    MetricCondition("longest run", longestRun,
                    assertion.value("longestRun").toObject(), failures);
    if (!area.isEmpty())
    {
        MetricCondition("matching area", matchingArea,
                        assertion.value("matchingArea").toObject(), failures);
    }

    result["passed"] = failures.isEmpty();
    result["rows"] = selected.size();
    result["matchingRows"] = matchingRows;
    result["matchingFraction"] = fraction;
    result["runCount"] = runs.size();
    result["longestRun"] = longestRun;
    if (!area.isEmpty())
        result["matchingArea"] = matchingArea;
    result["runs"] = CsvRunsJson(runs);
    result["message"] = failures.isEmpty()
        ? QString("Run limit passed: %1 matching row(s) in %2 run(s), longest %3, area %4.")
            .arg(matchingRows).arg(runs.size()).arg(longestRun).arg(matchingArea)
        : QString("Run-limit assertion failed: %1.").arg(failures.join("; "));
    return result;
}

QImage ReducePairedCandidate(const QImage& candidate,
                             int targetWidth,
                             int targetHeight,
                             const QString& reduction)
{
    const int scaleX = candidate.width() / targetWidth;
    const int scaleY = candidate.height() / targetHeight;
    QImage reduced(targetWidth, targetHeight, QImage::Format_RGBA8888);
    for (int y = 0; y < targetHeight; y++)
    {
        uchar* outputRow = reduced.scanLine(y);
        for (int x = 0; x < targetWidth; x++)
        {
            const int outputOffset = x * 4;
            if (reduction == "nearestCenter")
            {
                const uchar* inputRow = candidate.constScanLine(y * scaleY + scaleY / 2);
                const int inputOffset = (x * scaleX + scaleX / 2) * 4;
                for (int channel = 0; channel < 4; channel++)
                    outputRow[outputOffset + channel] = inputRow[inputOffset + channel];
                continue;
            }

            qint64 sums[4] {};
            for (int inputY = y * scaleY; inputY < (y + 1) * scaleY; inputY++)
            {
                const uchar* inputRow = candidate.constScanLine(inputY);
                for (int inputX = x * scaleX; inputX < (x + 1) * scaleX; inputX++)
                {
                    const int inputOffset = inputX * 4;
                    for (int channel = 0; channel < 4; channel++)
                        sums[channel] += inputRow[inputOffset + channel];
                }
            }
            const int samples = scaleX * scaleY;
            for (int channel = 0; channel < 4; channel++)
            {
                outputRow[outputOffset + channel] =
                    static_cast<uchar>((sums[channel] + samples / 2) / samples);
            }
        }
    }
    return reduced;
}

QJsonObject EvaluatePairedImageAssertion(const QJsonObject& assertion,
                                         const QString& expectationsPath,
                                         const QString& outputDir)
{
    QJsonObject result;
    result["id"] = assertion.value("id").toString();
    result["type"] = "pairedImageCompare";
    result["reference"] = assertion.value("reference").toObject();
    result["candidate"] = assertion.value("candidate").toObject();
    result["native"] = assertion.value("native").toObject();
    result["candidateRoi"] = assertion.value("candidateRoi").toArray();
    result["nativeRoi"] = assertion.value("nativeRoi").toArray();
    result["reduction"] = assertion.value("reduction").toString();
    result["colorMode"] = assertion.value("colorMode").toString();
    result["temporalScope"] = assertion.value("temporalScope").toString();

    const QString candidatePath = SemanticImagePath(
        assertion.value("candidate").toObject(), outputDir);
    QString nativePath;
    const QJsonObject nativeSelection = assertion.value("native").toObject();
    if (nativeSelection.contains("file"))
    {
        if (!ResolveExpectationFile(expectationsPath,
                                    nativeSelection.value("file").toString(),
                                    nativePath))
        {
            result["passed"] = false;
            result["message"] = "Native reference path escapes the expectations directory.";
            return result;
        }
        result["nativeFile"] = QDir(QFileInfo(expectationsPath).absolutePath())
            .relativeFilePath(nativePath);
    }
    else
    {
        nativePath = SemanticImagePath(nativeSelection, outputDir);
        result["nativeFile"] = QDir(outputDir).relativeFilePath(nativePath);
    }
    result["candidateFile"] = QDir(outputDir).relativeFilePath(candidatePath);

    QImage candidate(candidatePath);
    QImage native(nativePath);
    if (candidate.isNull() || native.isNull())
    {
        result["passed"] = false;
        result["message"] = QString("Could not load paired %1 image: %2")
            .arg(candidate.isNull() ? "candidate" : "native reference",
                 candidate.isNull() ? candidatePath : nativePath);
        return result;
    }
    candidate = candidate.convertToFormat(QImage::Format_RGBA8888);
    native = native.convertToFormat(QImage::Format_RGBA8888);

    const QJsonArray candidateRoiValues = assertion.value("candidateRoi").toArray();
    const QJsonArray nativeRoiValues = assertion.value("nativeRoi").toArray();
    const QRect candidateRoi(candidateRoiValues[0].toInt(), candidateRoiValues[1].toInt(),
                             candidateRoiValues[2].toInt(), candidateRoiValues[3].toInt());
    const QRect nativeRoi(nativeRoiValues[0].toInt(), nativeRoiValues[1].toInt(),
                          nativeRoiValues[2].toInt(), nativeRoiValues[3].toInt());
    if (!QRect(0, 0, candidate.width(), candidate.height()).contains(candidateRoi) ||
        !QRect(0, 0, native.width(), native.height()).contains(nativeRoi))
    {
        result["passed"] = false;
        result["message"] = "Candidate or native ROI is outside its source image.";
        return result;
    }
    if (candidateRoi.width() < nativeRoi.width() ||
        candidateRoi.height() < nativeRoi.height() ||
        candidateRoi.width() % nativeRoi.width() != 0 ||
        candidateRoi.height() % nativeRoi.height() != 0)
    {
        result["passed"] = false;
        result["message"] = "Candidate ROI must be an integer multiple of the native ROI.";
        return result;
    }

    const int scaleX = candidateRoi.width() / nativeRoi.width();
    const int scaleY = candidateRoi.height() / nativeRoi.height();
    result["scaleX"] = scaleX;
    result["scaleY"] = scaleY;
    const QImage candidateCrop = candidate.copy(candidateRoi);
    const QImage nativeCrop = native.copy(nativeRoi);
    const QImage reduced = ReducePairedCandidate(candidateCrop,
                                                 nativeCrop.width(),
                                                 nativeCrop.height(),
                                                 assertion.value("reduction").toString());

    ImageMetrics metrics;
    QImage reducedMetricImage;
    QImage nativeMetricImage;
    QImage difference;
    QImage unusedMask;
    if (!ComputeImageMetrics(reduced, nativeCrop, QImage(),
                             QRect(0, 0, nativeCrop.width(), nativeCrop.height()),
                             assertion.value("colorMode").toString() == "rgba",
                             metrics, reducedMetricImage, nativeMetricImage,
                             difference, unusedMask))
    {
        result["passed"] = false;
        result["message"] = "Paired comparison selected no pixels.";
        return result;
    }

    const QJsonObject tolerance = assertion.value("tolerance").toObject();
    const double maxChangedFraction = tolerance.value("maxChangedPixelFraction").toDouble();
    const int maxChannelError = tolerance.value("maxChannelError").toInt();
    const int maxComponentSize = tolerance.value("maxConnectedComponentSize").toInt();
    const bool passed = metrics.changedPixelFraction <= maxChangedFraction &&
                        metrics.maxChannelError <= maxChannelError &&
                        metrics.maxConnectedComponentSize <= maxComponentSize;
    result["passed"] = passed;
    result["comparedPixels"] = metrics.comparedPixels;
    result["changedPixels"] = metrics.changedPixels;
    result["changedPixelFraction"] = metrics.changedPixelFraction;
    result["maxChannelError"] = metrics.maxChannelError;
    result["maxConnectedComponentSize"] = metrics.maxConnectedComponentSize;
    result["tolerance"] = tolerance;
    if (!metrics.changedBounds.isNull())
    {
        QJsonObject bounds;
        bounds["x"] = metrics.changedBounds.x();
        bounds["y"] = metrics.changedBounds.y();
        bounds["width"] = metrics.changedBounds.width();
        bounds["height"] = metrics.changedBounds.height();
        result["changedBounds"] = bounds;
    }
    if (passed)
    {
        result["message"] = QString("Reduced %1x%2 candidate to %3x%4; %5 of %6 pixels changed.")
            .arg(candidateCrop.width()).arg(candidateCrop.height())
            .arg(nativeCrop.width()).arg(nativeCrop.height())
            .arg(metrics.changedPixels).arg(metrics.comparedPixels);
        return result;
    }

    result["message"] = QString(
        "Paired comparison failed: changed fraction %1 (max %2), channel error %3 (max %4), component %5 (max %6).")
        .arg(metrics.changedPixelFraction, 0, 'g', 10)
        .arg(maxChangedFraction, 0, 'g', 10)
        .arg(metrics.maxChannelError).arg(maxChannelError)
        .arg(metrics.maxConnectedComponentSize).arg(maxComponentSize);
    QDir runDir(outputDir);
    const QString artifactDirName = "assertion-failures/" +
                                    SafeName(assertion.value("id").toString());
    QJsonArray artifacts;
    if (runDir.mkpath(artifactDirName))
    {
        QDir artifactDir(runDir.filePath(artifactDirName));
        const struct
        {
            const char* name;
            const QImage* image;
        } images[] = {
            {"candidate.png", &candidateCrop},
            {"reduced.png", &reduced},
            {"native.png", &nativeCrop},
            {"difference.png", &difference},
        };
        for (const auto& artifact : images)
        {
            const QString path = artifactDir.filePath(QString::fromLatin1(artifact.name));
            if (artifact.image->save(path, "PNG"))
                artifacts.push_back(runDir.relativeFilePath(path));
        }
    }
    result["artifacts"] = artifacts;
    return result;
}

QJsonObject EvaluateCsvAssertion(const QJsonObject& assertion,
                                 const CsvTable& table)
{
    QHash<QString, int> columnIndexes;
    for (int i = 0; i < table.columns.size(); i++)
        columnIndexes.insert(table.columns[i], i);

    const QJsonObject where = assertion.value("where").toObject();
    const QJsonObject expect = assertion.value("expect").toObject();
    for (auto it = where.begin(); it != where.end(); ++it)
    {
        if (!columnIndexes.contains(it.key()))
            return FailureResult(assertion, QString("CSV does not contain where column %1.").arg(it.key()));
    }
    for (auto it = expect.begin(); it != expect.end(); ++it)
    {
        if (!columnIndexes.contains(it.key()))
            return FailureResult(assertion, QString("CSV does not contain expected column %1.").arg(it.key()));
    }

    QVector<int> matches;
    for (int rowIndex = 0; rowIndex < table.rows.size(); rowIndex++)
    {
        bool selected = true;
        for (auto it = where.begin(); it != where.end(); ++it)
        {
            QString ignored;
            selected &= EvaluateCondition(table.rows[rowIndex][columnIndexes.value(it.key())],
                                          it.value().toObject(), ignored);
        }
        if (selected)
            matches.push_back(rowIndex);
    }

    QString rowExpectation;
    const bool countPassed = EvaluateCondition(QString::number(matches.size()),
                                               assertion.value("rows").toObject(),
                                               rowExpectation);
    QJsonObject result;
    result["id"] = assertion.value("id").toString();
    result["type"] = "csv";
    result["csv"] = assertion.value("csv").toString();
    result["matchedRows"] = matches.size();
    result["reference"] = assertion.value("reference").toObject();
    if (!countPassed)
    {
        result["passed"] = false;
        result["message"] = QString("Matched %1 row(s); expected %2.")
            .arg(matches.size())
            .arg(rowExpectation);
        result["actual"] = matches.size();
        result["expected"] = rowExpectation;
        return result;
    }

    for (const int rowIndex : matches)
    {
        for (auto it = expect.begin(); it != expect.end(); ++it)
        {
            const QString& actual = table.rows[rowIndex][columnIndexes.value(it.key())];
            QString expected;
            if (!EvaluateCondition(actual, it.value().toObject(), expected))
            {
                result["passed"] = false;
                result["message"] = QString("CSV row %1 column %2 was %3; expected %4.")
                    .arg(rowIndex + 2)
                    .arg(it.key(), actual, expected);
                result["failedCsvRow"] = rowIndex + 2;
                result["failedColumn"] = it.key();
                result["actual"] = actual;
                result["expected"] = expected;
                const int frameColumn = columnIndexes.value("frame", -1);
                if (frameColumn >= 0)
                    result["frame"] = table.rows[rowIndex][frameColumn];
                return result;
            }
        }
    }

    result["passed"] = true;
    result["message"] = QString("Matched %1 row(s); all expected columns passed.")
        .arg(matches.size());
    return result;
}
}

bool RendererTestAssertions::load(const QString& path,
                                  QJsonArray& assertions,
                                  QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        error = QString("Could not open expectations %1: %2").arg(path, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject())
    {
        error = QString("Expectations %1 are not valid JSON: %2").arg(path, parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value("schema").toInt(-1) != 1 || !root.value("assertions").isArray())
    {
        error = QString("Expectations %1 require schema 1 and an assertions array.").arg(path);
        return false;
    }

    QSet<QString> ids;
    const QJsonArray specs = root.value("assertions").toArray();
    for (int i = 0; i < specs.size(); i++)
    {
        if (!specs[i].isObject())
        {
            error = QString("Expectations %1 assertion %2 must be an object.").arg(path).arg(i);
            return false;
        }
        const QJsonObject assertion = specs[i].toObject();
        const QString id = assertion.value("id").toString().trimmed();
        if (id.isEmpty() || ids.contains(id))
        {
            error = QString("Expectations %1 assertion %2 has a missing or duplicate id.").arg(path).arg(i);
            return false;
        }
        ids.insert(id);

        QString assertionError;
        if (!ValidateReference(assertion, assertionError))
        {
            error = QString("Expectations %1 assertion %2 (%3): %4.")
                .arg(path).arg(i).arg(id, assertionError);
            return false;
        }
        const QString type = assertion.value("type").toString();
        bool valid = false;
        if (type == "csv")
            valid = ValidateCsvAssertion(assertion, assertionError);
        else if (type == "imageCompare")
            valid = ValidateImageCompareAssertion(assertion, assertionError);
        else if (type == "imageHistogram")
            valid = ValidateImageHistogramAssertion(assertion, assertionError);
        else if (type == "perceptualSignature")
            valid = ValidatePerceptualSignatureAssertion(assertion, assertionError);
        else if (type == "perceptualSequence")
            valid = ValidatePerceptualSequenceAssertion(assertion, assertionError);
        else if (type == "csvSequence")
            valid = ValidateCsvSequenceAssertion(assertion, assertionError);
        else if (type == "csvRunLimit")
            valid = ValidateCsvRunLimitAssertion(assertion, assertionError);
        else if (type == "pairedImageCompare")
            valid = ValidatePairedImageAssertion(assertion, assertionError);
        else
            assertionError = QString("unsupported assertion type %1").arg(type);
        if (!valid)
        {
            error = QString("Expectations %1 assertion %2 (%3): %4.")
                .arg(path).arg(i).arg(id, assertionError);
            return false;
        }
    }

    assertions = specs;
    return true;
}

RendererTestAssertionRun RendererTestAssertions::evaluate(
    const QJsonArray& assertions,
    const QString& expectationsPath,
    const QString& outputDir,
    const QString& summaryCsvPath,
    const QString& rendererDetailsCsvPath)
{
    RendererTestAssertionRun run;
    CsvTable summary;
    CsvTable rendererDetails;
    bool summaryLoaded = false;
    bool detailsLoaded = false;
    QString summaryError;
    QString detailsError;

    for (const QJsonValue& value : assertions)
    {
        const QJsonObject assertion = value.toObject();
        QJsonObject result;
        const QString type = assertion.value("type").toString();
        if (type == "imageCompare")
        {
            result = EvaluateImageCompareAssertion(assertion, expectationsPath, outputDir);
        }
        else if (type == "imageHistogram")
        {
            result = EvaluateImageHistogramAssertion(assertion, expectationsPath, outputDir);
        }
        else if (type == "perceptualSignature")
        {
            result = EvaluatePerceptualSignatureAssertion(assertion, expectationsPath, outputDir);
        }
        else if (type == "perceptualSequence")
        {
            result = EvaluatePerceptualSequenceAssertion(assertion, expectationsPath, outputDir);
        }
        else if (type == "pairedImageCompare")
        {
            result = EvaluatePairedImageAssertion(assertion, expectationsPath, outputDir);
        }
        else
        {
            const bool useSummary = assertion.value("csv").toString() == "summary";
            CsvTable* table = useSummary ? &summary : &rendererDetails;
            bool* loaded = useSummary ? &summaryLoaded : &detailsLoaded;
            QString* loadError = useSummary ? &summaryError : &detailsError;
            const QString path = useSummary ? summaryCsvPath : rendererDetailsCsvPath;
            if (!*loaded && loadError->isEmpty())
                *loaded = LoadCsv(path, *table, *loadError);

            if (!*loaded)
                result = FailureResult(assertion, *loadError);
            else if (type == "csvSequence")
                result = EvaluateCsvSequenceAssertion(assertion, *table);
            else if (type == "csvRunLimit")
                result = EvaluateCsvRunLimitAssertion(assertion, *table);
            else
                result = EvaluateCsvAssertion(assertion, *table);
        }

        const bool passed = result.value("passed").toBool(false);
        if (passed)
            run.passed++;
        else
        {
            run.failed++;
            if (run.firstFailure.isEmpty())
            {
                run.firstFailure = QString("Assertion %1 failed: %2")
                    .arg(result.value("id").toString(), result.value("message").toString());
            }
        }
        run.results.push_back(result);
    }
    return run;
}
