#pragma once

#include <QJsonArray>
#include <QJsonObject>

#include "domain/models/Config.h"
#include "domain/models/RoutingItem.h"

class SingBoxRoutingConfigFragments {
public:
    /// `dnsServers` is the server list of the `dns` object built for the same config; it decides
    /// which tag `route.default_domain_resolver` can name. See DnsConfigFragments for why the field
    /// cannot simply be dropped when the direct DNS is missing.
    static QJsonObject buildRoute(
        const Config& config,
        const RoutingItem* selectedRouting,
        const QJsonArray& dnsServers);
    static void migrateGeoToRuleSet(QJsonObject& root);
};
