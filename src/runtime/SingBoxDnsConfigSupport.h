#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QStringList>

#include "domain/models/Config.h"
#include "domain/models/RoutingItem.h"

namespace SingBoxDnsConfigSupport {

QString directDnsTag();
QString remoteDnsTag();
QString localDnsTag();
QString hostsDnsTag();
QString fakeDnsTag();
QString proxyServerDnsTag();

QJsonObject parseDnsAddress(const QString& value);
bool isCustomDnsObjectText(const QString& value);

/// True when the user configured a DNS server dedicated to the proxy node's own hostname.
bool hasProxyServerDns(const Config& config);

/// Tag of the server that should be named by `route.default_domain_resolver`, picked from the
/// servers the generated config actually carries.
///
/// This has to be a real tag, because sing-box refuses to start when a config declares more than
/// one DNS server and names no default resolver -- and it is a hard failure, not a warning:
///
///   FATAL to continuing using this feature, set environment variable
///   ENABLE_DEPRECATED_MISSING_DOMAIN_RESOLVER=true
///
/// The preference order keeps the historical answer first (`direct_dns`), then falls back to
/// servers that still exist when the user cleared the direct DNS or replaced the whole `dns`
/// section with a custom object. An empty result means the config carries no server at all, which
/// is the one case sing-box accepts without a resolver.
QString defaultDomainResolverTag(const QJsonArray& dnsServers);
QString mapDomainStrategy(const QString& strategy);
bool usesDirectDnsAsFinalServer(const RoutingItem* routing);

/// Adds the match field for one `RoutingRule::domain` value to a sing-box DNS rule.
///
/// Returns false when the value cannot be expressed as a DNS match at all, so the caller can
/// skip it instead of emitting a rule that matches nothing.
///
/// `plainAsDomain` picks the field for a value that carries no prefix: a hosts file entry is a
/// literal hostname and wants the exact-match "domain" field, while a routing rule's bare value
/// is a keyword and wants "domain_keyword". Every caller currently passes true.
bool appendDomainField(QJsonObject& rule, const QString& value, bool plainAsDomain);

QString mapRcode(int code);
QJsonObject fakeIpFilterRule();
QJsonObject predefinedHosts(const Config& config, const QMap<QString, QStringList>& hostsMap);
QJsonObject hostsDnsServer(const QJsonObject& predefinedHosts);
void applyHostsResolver(QJsonObject& dnsServer, const QJsonObject& predefinedHosts);
QString finalDnsServerTag(
    bool hasRemoteDnsServer,
    bool hasDirectDnsServer,
    bool hasPredefinedHosts,
    bool useDirectFinal);

} // namespace SingBoxDnsConfigSupport
