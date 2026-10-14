#include "common/LogFileWriter.h"

#include <algorithm>
#include <utility>

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

// `<prefix>-<yyyy-MM-dd>.<NNN>.log`, anchored, so a name that merely begins with
// the prefix does not match. The prefix is escaped and appended last on purpose:
// it is the one part that comes from outside, and inserting it into a string that
// still has placeholders to fill would let a prefix containing "%2" take the
// index's place.
QRegularExpression ownedFileNamePattern(const QString& prefix)
{
    const QString tail =
        QStringLiteral("-(\\d{4}-\\d{2}-\\d{2})\\.(\\d{%1})\\.log$").arg(LogFileNaming::kIndexDigits);
    return QRegularExpression(QStringLiteral("^") + QRegularExpression::escape(prefix) + tail);
}

QString paddedIndex(int index)
{
    return QStringLiteral("%1").arg(index, LogFileNaming::kIndexDigits, 10, QLatin1Char('0'));
}

} // namespace

namespace LogFileNaming {

QString fileName(const QString& prefix, const QDate& date, int index)
{
    return QStringLiteral("%1-%2.%3.log")
        .arg(prefix, date.toString(QStringLiteral("yyyy-MM-dd")), paddedIndex(index));
}

QStringList filesBeyondRetention(const QStringList& existingNames, const QString& prefix, int keepFiles)
{
    if (keepFiles <= 0) {
        return {};
    }

    const QRegularExpression pattern = ownedFileNamePattern(prefix);
    QStringList owned;
    for (const QString& name : existingNames) {
        if (pattern.match(name).hasMatch()) {
            owned.append(name);
        }
    }

    if (owned.size() <= keepFiles) {
        return {};
    }

    // Ascending, which the zero padded index turns into chronological -- the only
    // reason the index is padded at all. The expired files are then the first
    // ones, and they come back oldest first.
    std::sort(owned.begin(), owned.end());
    return owned.mid(0, owned.size() - keepFiles);
}

} // namespace LogFileNaming

LogFileWriter::LogFileWriter(
    QString directory,
    QString filePrefix,
    int keepFiles,
    qint64 maxFileBytes,
    Clock clock)
    : directory_(std::move(directory))
    , prefix_(std::move(filePrefix))
    , keepFiles_(keepFiles)
    , maxFileBytes_(maxFileBytes)
    , clock_(std::move(clock))
{
    // Opened here rather than on the first append: the panel's opening lines name
    // the file they are being written to, so the path has to be known -- and known
    // to work -- before anything is logged.
    openFor(now().date());
}

QString LogFileWriter::defaultDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.trimmed().isEmpty()) {
        return {};
    }

    return QDir(base).filePath(QStringLiteral("logs"));
}

void LogFileWriter::append(LogSeverity severity, const QString& message)
{
    const QString text = message.trimmed();
    if (text.isEmpty()) {
        return;
    }

    const QDateTime moment = now();
    openFor(moment.date());
    if (!enabled_) {
        return;
    }

    const QByteArray record =
        (moment.toString(Qt::ISODate) + QLatin1Char(' ') + logSeverityMarker(severity)
         + QLatin1Char(' ') + text + QLatin1Char('\n'))
            .toUtf8();

    // The size cap is checked against the record about to be written, not against
    // the file as it stands, so a file never ends up over the cap by one record.
    // A record larger than the whole cap is still written -- refusing it would lose
    // the line, which is the opposite of the point.
    if (maxFileBytes_ > 0 && index_ < LogFileNaming::kMaxIndex
        && file_.size() + record.size() > maxFileBytes_) {
        ++index_;
        file_.close();
        retentionPending_ = true;
        openFor(moment.date());
        if (!enabled_) {
            return;
        }
    }

    file_.write(record);
    // Every record reaches the disk before the call returns. A buffered log is
    // exactly the log that is missing the last lines when the process dies, which
    // is the case this file exists for.
    file_.flush();
}

QString LogFileWriter::currentFilePath() const
{
    return currentPath_;
}

bool LogFileWriter::isEnabled() const
{
    return enabled_;
}

QDateTime LogFileWriter::now() const
{
    return clock_ ? clock_() : QDateTime::currentDateTime();
}

void LogFileWriter::openFor(const QDate& date)
{
    if (date != openDate_) {
        file_.close();
        openDate_ = date;
        index_ = 0;
        // A new day is a new file, and opening one is the only moment retention
        // has to run. Doing it here rather than per record keeps a directory
        // listing off the hot path.
        retentionPending_ = true;
    }

    if (directory_.trimmed().isEmpty()) {
        currentPath_.clear();
        enabled_ = false;
        return;
    }

    const QString path = QDir(directory_).filePath(LogFileNaming::fileName(prefix_, openDate_, index_));
    currentPath_ = path;

    if (enabled_ && openPath_ == path) {
        return;
    }

    // One attempt per path. Without this a failure would be retried on every line,
    // and a failure does not clear while the reason for it is still true.
    if (!enabled_ && attemptedPath_ == path) {
        return;
    }
    attemptedPath_ = path;

    file_.close();
    if (!QDir().mkpath(directory_)) {
        enabled_ = false;
        return;
    }

    file_.setFileName(path);
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        enabled_ = false;
        return;
    }

    openPath_ = path;
    enabled_ = true;
    if (retentionPending_) {
        retentionPending_ = false;
        removeFilesBeyondRetention();
    }
}

void LogFileWriter::removeFilesBeyondRetention() const
{
    if (keepFiles_ <= 0) {
        return;
    }

    const QDir directory(directory_);
    const QStringList expired = LogFileNaming::filesBeyondRetention(
        directory.entryList(QDir::Files, QDir::Name), prefix_, keepFiles_);
    for (const QString& name : expired) {
        // Best effort, and deliberately unchecked. Retention is a disk-space guard
        // rather than a guarantee: a file that cannot be removed right now --
        // another process has it open, or the directory is read-only -- is left for
        // the next rotation instead of being turned into a startup failure.
        QFile::remove(directory.filePath(name));
    }
}
