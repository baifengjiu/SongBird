#pragma once

#include <QDir>
#include <QString>
#include <QStringList>

// The identification lines the information panel opens with.
//
// Kept as a pure function over its inputs rather than assembled inline in
// AppBootstrap: the version in the first line is the entire point of the feature
// -- a pasted log used to say nothing about which build produced it, so the one
// fact that decides whether a report is reproducible had to be asked for
// separately -- and "the banner names the version" is not something a build or a
// manual run can prove.
//
// The lines are deliberately not translated. Every slot is a version, a platform
// name or a path, so there is no prose for a translator to move, and a reordered
// or localised template would break the only thing the line is for: being read
// back by whoever is diagnosing the report.
inline QStringList startupBannerLines(
    const QString& applicationVersion,
    const QString& qtVersion,
    const QString& platform,
    const QString& configPath,
    const QString& logFilePath)
{
    QStringList lines;
    lines.append(QStringLiteral("SongBird %1 | Qt %2 | %3")
                     .arg(applicationVersion, qtVersion, platform));

    // A build can start without a resolved config path (no --config and no
    // default yet). Printing "Config: " with nothing after it would be worse
    // than printing nothing, so the line is dropped rather than left empty.
    const QString trimmedConfigPath = configPath.trimmed();
    if (!trimmedConfigPath.isEmpty()) {
        lines.append(QStringLiteral("Config: %1").arg(QDir::toNativeSeparators(trimmedConfigPath)));
    }

    // Where this very text is also being written. The panel is what a user
    // screenshots, but the file is what they can attach, and "which file?" is the
    // first thing anyone asking for a log has to answer.
    const QString trimmedLogFilePath = logFilePath.trimmed();
    if (!trimmedLogFilePath.isEmpty()) {
        lines.append(QStringLiteral("Log: %1").arg(QDir::toNativeSeparators(trimmedLogFilePath)));
    }

    return lines;
}
