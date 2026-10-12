#pragma once

#include <QSet>
#include <QString>

// Which sing-box rule-set tags upstream actually publishes.
//
// `SingBoxRoutingConfigFragments::migrateGeoToRuleSet()` rewrites every
// `geosite:<value>` / `geoip:<value>` list into a `rule_set` entry, and emits a
// `remote` rule-set pointing at the raw file on the vendor's `rule-set` branch
// unless a same-named local copy exists in <app dir>/rule-set. Most categories
// are published there; the ones listed below are not, and a rule that names one
// of them can never match because sing-box cannot fetch its database. Dropping
// the value at migration time is what keeps the generated config free of rules
// that point at a 404.
//
// Kept in its own header, away from the config fragments, because it is pure
// data with no I/O and is therefore unit-testable without the whole runtime
// chain -- and because it is the list the offline release bundle has to agree
// with (see .github/workflows/release.yml).
//
// Note the asymmetry: `geosite-cn` and `geoip-cn` ARE published, so a missing
// entry here is a bug, not a preference. `geoip-private` was added only after
// it was found to be absent from sing-geoip's rule-set branch despite the
// default "Bypass CN" profile naming it.
inline bool isSingBoxRuleSetPublishedUpstream(const QString& tag)
{
    static const QSet<QString> unpublishedRuleSets = {
        QStringLiteral("geosite-gfw"),
        QStringLiteral("geoip-telegram"),
        QStringLiteral("geoip-private"),
    };
    return !unpublishedRuleSets.contains(tag);
}
