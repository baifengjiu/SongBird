#include "services/CoreUpdateVersion.h"

#include <QChar>
#include <QList>
#include <QtGlobal>

namespace {

struct PrereleaseToken {
    bool numeric = false;
    int number = 0;
    QString text;
};

struct ParsedVersion {
    QList<int> numbers;
    // Empty means "a release", not "a prerelease".
    QList<PrereleaseToken> prerelease;
};

// Splits one prerelease identifier into alternating digit / non-digit runs so that both spellings
// in the wild order numerically: the semver form `1.2.3-beta.1` and the compact form `1.2.3-beta1`
// used by this project's own tags.
QList<PrereleaseToken> splitPrereleaseIdentifier(const QString& identifier)
{
    QList<PrereleaseToken> tokens;
    QString current;
    bool currentIsDigits = false;

    const auto flush = [&tokens, &current, &currentIsDigits]() {
        if (current.isEmpty()) {
            return;
        }
        PrereleaseToken token;
        token.numeric = currentIsDigits;
        if (currentIsDigits) {
            token.number = current.toInt();
        } else {
            token.text = current;
        }
        tokens.append(token);
        current.clear();
    };

    for (const QChar character : identifier) {
        const bool isDigit = character.isDigit();
        if (!current.isEmpty() && isDigit != currentIsDigits) {
            flush();
        }
        currentIsDigits = isDigit;
        current.append(character);
    }
    flush();

    return tokens;
}

ParsedVersion parseVersion(const QString& version)
{
    QString stripped = CoreUpdateVersion::normalizeTag(version);
    if (stripped.startsWith(QChar('v'))) {
        stripped = stripped.mid(1);
    }

    // Build metadata never participates in ordering (semver 2.0.0, section 10).
    const int plusIndex = stripped.indexOf(QChar('+'));
    if (plusIndex >= 0) {
        stripped = stripped.left(plusIndex);
    }

    QString prereleaseText;
    const int dashIndex = stripped.indexOf(QChar('-'));
    if (dashIndex >= 0) {
        prereleaseText = stripped.mid(dashIndex + 1);
        stripped = stripped.left(dashIndex);
    }

    ParsedVersion parsed;
    for (const QString& segment : stripped.split(QChar('.'))) {
        bool ok = false;
        const int value = segment.toInt(&ok);
        parsed.numbers.append(ok ? value : 0);
    }

    if (!prereleaseText.isEmpty()) {
        for (const QString& identifier : prereleaseText.split(QChar('.'))) {
            if (!identifier.isEmpty()) {
                parsed.prerelease.append(splitPrereleaseIdentifier(identifier));
            }
        }
    }

    return parsed;
}

// Returns <0, 0 or >0 for "candidate is older than / equal to / newer than current".
int comparePrerelease(const QList<PrereleaseToken>& candidate, const QList<PrereleaseToken>& current)
{
    const int maxLen = qMax(candidate.size(), current.size());
    for (int i = 0; i < maxLen; ++i) {
        // A shorter identifier list is older when every preceding token matched.
        if (i >= candidate.size()) {
            return -1;
        }
        if (i >= current.size()) {
            return 1;
        }

        const PrereleaseToken& candidateToken = candidate.at(i);
        const PrereleaseToken& currentToken = current.at(i);
        if (candidateToken.numeric != currentToken.numeric) {
            // Numeric identifiers always have lower precedence than alphanumeric ones.
            return candidateToken.numeric ? -1 : 1;
        }
        if (candidateToken.numeric) {
            if (candidateToken.number != currentToken.number) {
                return candidateToken.number > currentToken.number ? 1 : -1;
            }
            continue;
        }

        const int order = QString::compare(candidateToken.text, currentToken.text);
        if (order != 0) {
            return order;
        }
    }
    return 0;
}

} // namespace

namespace CoreUpdateVersion {

QString normalizeTag(QString value)
{
    value = value.trimmed().toLower();
    if (value.startsWith(QStringLiteral("version "))) {
        value = value.mid(QStringLiteral("version ").size()).trimmed();
    }

    if (!value.isEmpty() && value.front().isDigit()) {
        value.prepend(QChar('v'));
    }

    return value;
}

bool isPrerelease(const QString& tag)
{
    return !parseVersion(tag).prerelease.isEmpty();
}

bool isNewerThan(const QString& candidate, const QString& current)
{
    const ParsedVersion candidateVersion = parseVersion(candidate);
    const ParsedVersion currentVersion = parseVersion(current);

    const int maxLen = qMax(candidateVersion.numbers.size(), currentVersion.numbers.size());
    for (int i = 0; i < maxLen; ++i) {
        const int c = i < candidateVersion.numbers.size() ? candidateVersion.numbers.at(i) : 0;
        const int r = i < currentVersion.numbers.size() ? currentVersion.numbers.at(i) : 0;
        if (c > r) {
            return true;
        }
        if (c < r) {
            return false;
        }
    }

    // Same release numbers. A prerelease is always older than the release it leads up to, so
    // 2.5.0 is newer than 2.5.0-beta1 and 2.5.0-beta2 is newer than 2.5.0-beta1. Dropping the
    // suffix here would make an installed beta deaf to the release it exists to test.
    const bool candidateIsPrerelease = !candidateVersion.prerelease.isEmpty();
    const bool currentIsPrerelease = !currentVersion.prerelease.isEmpty();
    if (candidateIsPrerelease != currentIsPrerelease) {
        return !candidateIsPrerelease;
    }
    if (!candidateIsPrerelease) {
        return false;
    }

    return comparePrerelease(candidateVersion.prerelease, currentVersion.prerelease) > 0;
}

} // namespace CoreUpdateVersion
