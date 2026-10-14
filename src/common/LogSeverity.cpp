#include "common/LogSeverity.h"

#include <QRegularExpression>

namespace {

// The three shapes the supported cores put their level in. Kept as separate
// patterns rather than one alternation so that each can be covered by its own
// test data and a miss names the shape it came from.
//
//   Xray      ERROR[206174] [3631501782 5.7s] connection: ...
//   sing-box  2026-10-12 18:45:13 ERROR inbound/http[location-probe]: ...
//   bracketed [Warning] some component: ...
const QString kLeadingLevelPattern = QStringLiteral(
    "^\\s*(?:\\+\\d{4}\\s+)?"
    "(?:\\d{4}-\\d{2}-\\d{2}[ T]\\d{2}:\\d{2}:\\d{2}(?:\\.\\d+)?\\s+)?"
    "(trace|debug|info|warn|warning|error|fatal|panic)\\b");

const QString kBracketedLevelPattern = QStringLiteral(
    "^\\s*\\[\\s*(trace|debug|info|warn|warning|error|fatal|panic)\\s*\\]");

// mihomo writes 'time="..." level=error msg="..."', so its level is a field
// rather than the first token.
const QString kLevelFieldPattern = QStringLiteral(
    "\\blevel=(trace|debug|info|warn|warning|error|fatal|panic)\\b");

LogSeverity severityForLevelWord(const QString& word)
{
    const QString normalized = word.toLower();
    if (normalized == QStringLiteral("warn") || normalized == QStringLiteral("warning")) {
        return LogSeverity::Warning;
    }
    if (normalized == QStringLiteral("error") || normalized == QStringLiteral("fatal")
        || normalized == QStringLiteral("panic")) {
        return LogSeverity::Error;
    }
    return LogSeverity::Info;
}

} // namespace

QString logSeverityMarker(LogSeverity severity)
{
    switch (severity) {
    case LogSeverity::Warning:
        return QStringLiteral("[W]");
    case LogSeverity::Error:
        return QStringLiteral("[E]");
    case LogSeverity::Info:
        break;
    }

    return QStringLiteral("[I]");
}

LogSeverity logSeverityFromCoreOutput(const QString& line)
{
    static const QRegularExpression leadingLevel(
        kLeadingLevelPattern, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bracketedLevel(
        kBracketedLevelPattern, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression levelField(
        kLevelFieldPattern, QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch leadingMatch = leadingLevel.match(line);
    if (leadingMatch.hasMatch()) {
        return severityForLevelWord(leadingMatch.captured(1));
    }

    const QRegularExpressionMatch bracketedMatch = bracketedLevel.match(line);
    if (bracketedMatch.hasMatch()) {
        return severityForLevelWord(bracketedMatch.captured(1));
    }

    const QRegularExpressionMatch fieldMatch = levelField.match(line);
    if (fieldMatch.hasMatch()) {
        return severityForLevelWord(fieldMatch.captured(1));
    }

    return LogSeverity::Info;
}
