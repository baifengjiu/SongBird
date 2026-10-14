#pragma once

#include <QString>

// Severity of one line in the information panel.
//
// The panel used to render every line identically, so a failed activation and a
// routine cleanup step carried the same visual weight: a user reading the panel
// -- or pasting it into a bug report -- had no way to tell them apart, and the
// error that mattered was buried in whatever the core happened to print next to
// it. The value is always derived from information that already exists (the
// OperationResult for lines this process generates, the level token for lines a
// core printed itself) and never guessed from the message text.
enum class LogSeverity {
    Info,
    Warning,
    Error,
};

// Bracketed marker printed between the timestamp and the message: "[I]", "[W]",
// "[E]". Deliberately not translated -- it is a fixed-width column the user
// scans and greps, not prose.
QString logSeverityMarker(LogSeverity severity);

// Severity implied by a core's own log line, or Info when the line does not
// open with a level the core printed itself.
//
// All three supported cores put their level in the line, so the app reads it
// instead of re-judging the text: Xray writes "ERROR[206174] ...", sing-box
// writes "2026-10-12 18:45:13 ERROR ...", mihomo writes 'level=error'. The line
// is still forwarded verbatim, which is why the level then appears twice in the
// panel -- once as the marker, once exactly as the core wrote it. Dropping the
// core's copy would make a line copied out of the panel stop matching the same
// line in the core's own log file, which is a worse trade than the repetition.
LogSeverity logSeverityFromCoreOutput(const QString& line);
