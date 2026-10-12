#include <QtTest>

#include "backends/singbox/SingBoxRuleSetAvailability.h"

class SingBoxRuleSetAvailabilityTests : public QObject {
    Q_OBJECT

private slots:
    void acceptsTheCategoriesUpstreamPublishes();
    void rejectsTheCategoriesUpstreamDoesNotPublish();
};

void SingBoxRuleSetAvailabilityTests::acceptsTheCategoriesUpstreamPublishes()
{
    // Every one of these is a real file on the vendor's rule-set branch. A name
    // missing from the positive cases below the line is how `geoip-private`
    // slipped through: the default "Bypass CN" profile named it for months while
    // sing-geoip published no such file, so the rule could never match.
    const QStringList published = {
        QStringLiteral("geosite-category-ads-all"),
        QStringLiteral("geosite-cn"),
        QStringLiteral("geosite-greatfire"),
        QStringLiteral("geosite-tld-!cn"),
        QStringLiteral("geoip-cn"),
    };

    for (const QString& tag : published) {
        QVERIFY2(isSingBoxRuleSetPublishedUpstream(tag),
                 qPrintable(QStringLiteral("%1 must be treated as published").arg(tag)));
    }
}

void SingBoxRuleSetAvailabilityTests::rejectsTheCategoriesUpstreamDoesNotPublish()
{
    const QStringList unpublished = {
        QStringLiteral("geosite-gfw"),
        QStringLiteral("geoip-telegram"),
        QStringLiteral("geoip-private"),
    };

    for (const QString& tag : unpublished) {
        QVERIFY2(!isSingBoxRuleSetPublishedUpstream(tag),
                 qPrintable(QStringLiteral("%1 has no file upstream and must be dropped").arg(tag)));
    }
}

QTEST_MAIN(SingBoxRuleSetAvailabilityTests)
#include "SingBoxRuleSetAvailabilityTests.moc"
