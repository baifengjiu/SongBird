#include "runtime/SingBoxDnsConfigBuilder.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include "runtime/DnsHosts.h"
#include "runtime/SingBoxDnsConfigSupport.h"
#include "runtime/SingBoxDnsRuleConfig.h"

namespace DnsSupport = SingBoxDnsConfigSupport;
namespace DnsRules = SingBoxDnsRuleConfig;

namespace SingBoxDnsConfigBuilder {

QJsonObject build(const Config& config, const RoutingItem* selectedRouting)
{
    const QString remoteDns = config.dns().remoteDns.trimmed();
    if (!remoteDns.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(remoteDns.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError
            && document.isObject()
            && document.object().contains(QStringLiteral("servers"))) {
            // A custom object replaces the whole `dns` section, so the only tags that exist are the
            // ones it declares. The node resolver is appended on top of them, because it is the one
            // server this app has to be able to name: the node's own hostname cannot be resolved
            // through the proxy that is about to dial it.
            QJsonObject dnsObject = document.object();
            QJsonObject proxyServerDns = DnsSupport::parseDnsAddress(config.dns().proxyServerDns);
            if (!proxyServerDns.isEmpty()) {
                proxyServerDns.insert(QStringLiteral("tag"), DnsSupport::proxyServerDnsTag());
                QJsonArray customServers = dnsObject.value(QStringLiteral("servers")).toArray();
                customServers.append(proxyServerDns);
                dnsObject.insert(QStringLiteral("servers"), customServers);
            }
            return dnsObject;
        }
    }

    const bool useDirectFinal = DnsSupport::usesDirectDnsAsFinalServer(selectedRouting);

    QJsonObject dns;
    // `independent_cache` is deprecated since sing-box 1.14.0 and removed in 1.16.0. The documented
    // migration is to drop the field, and 1.14 warns about it on every start, so it is gone rather
    // than carried forward.

    QJsonArray servers;
    QJsonObject localDns = DnsSupport::parseDnsAddress(config.dns().bootstrapDns);
    if (!localDns.isEmpty()) {
        localDns.insert(QStringLiteral("tag"), DnsSupport::localDnsTag());
        servers.append(localDns);
    }

    QJsonObject remoteDnsServer = DnsSupport::parseDnsAddress(config.dns().remoteDns);
    if (!remoteDnsServer.isEmpty()) {
        remoteDnsServer.insert(QStringLiteral("tag"), DnsSupport::remoteDnsTag());
        remoteDnsServer.insert(QStringLiteral("detour"), QStringLiteral("proxy"));
        remoteDnsServer.insert(QStringLiteral("domain_resolver"), DnsSupport::localDnsTag());
        servers.append(remoteDnsServer);
    }

    QJsonObject directDnsServer = DnsSupport::parseDnsAddress(config.dns().directDns);
    if (!directDnsServer.isEmpty()) {
        directDnsServer.insert(QStringLiteral("tag"), DnsSupport::directDnsTag());
        directDnsServer.insert(QStringLiteral("domain_resolver"), DnsSupport::localDnsTag());
        servers.append(directDnsServer);
    }

    // The node's own hostname has to be resolved before the node can be dialled, so it can never
    // go through the proxy. A server of its own keeps that lookup out of `Direct DNS` and out of
    // `route.default_domain_resolver`, which is what used to cover it by accident.
    QJsonObject proxyServerDnsServer = DnsSupport::parseDnsAddress(config.dns().proxyServerDns);
    if (!proxyServerDnsServer.isEmpty()) {
        proxyServerDnsServer.insert(QStringLiteral("tag"), DnsSupport::proxyServerDnsTag());
        // A hostname-addressed resolver needs a bootstrap of its own, but only name one that is
        // really emitted: a dangling tag resolves to nothing at runtime.
        if (!localDns.isEmpty()) {
            proxyServerDnsServer.insert(QStringLiteral("domain_resolver"), DnsSupport::localDnsTag());
        }
        servers.append(proxyServerDnsServer);
    }

    const QMap<QString, QStringList> hostsMap = DnsHosts::parseConfigured(config.dns().dnsHosts);
    const QJsonObject predefinedHosts = DnsSupport::predefinedHosts(config, hostsMap);
    const bool hasPredefinedHosts = !predefinedHosts.isEmpty();
    // QJsonObject keeps its keys sorted, so the emitted domain list is deterministic without a sort.
    const QStringList predefinedHostDomains = predefinedHosts.keys();
    const QJsonObject hostsDnsServer = DnsSupport::hostsDnsServer(predefinedHosts);
    DnsSupport::applyHostsResolver(localDns, predefinedHosts);
    DnsSupport::applyHostsResolver(remoteDnsServer, predefinedHosts);
    DnsSupport::applyHostsResolver(directDnsServer, predefinedHosts);
    DnsSupport::applyHostsResolver(proxyServerDnsServer, predefinedHosts);
    for (qsizetype index = 0; index < servers.size(); ++index) {
        const QString tag = servers.at(index).toObject().value(QStringLiteral("tag")).toString();
        if (tag == DnsSupport::localDnsTag()) {
            servers[index] = localDns;
        } else if (tag == DnsSupport::remoteDnsTag()) {
            servers[index] = remoteDnsServer;
        } else if (tag == DnsSupport::directDnsTag()) {
            servers[index] = directDnsServer;
        } else if (tag == DnsSupport::proxyServerDnsTag()) {
            servers[index] = proxyServerDnsServer;
        }
    }
    if (hasPredefinedHosts) {
        servers.append(hostsDnsServer);
    }

    if (config.dns().fakeIp) {
        QJsonObject fakeDnsServer;
        fakeDnsServer.insert(QStringLiteral("tag"), DnsSupport::fakeDnsTag());
        fakeDnsServer.insert(QStringLiteral("type"), QStringLiteral("fakeip"));
        fakeDnsServer.insert(QStringLiteral("inet4_range"), QStringLiteral("198.18.0.0/15"));
        fakeDnsServer.insert(QStringLiteral("inet6_range"), QStringLiteral("fc00::/18"));
        servers.append(fakeDnsServer);
    }

    if (servers.isEmpty()) {
        return {};
    }

    dns.insert(QStringLiteral("servers"), servers);
    const QString finalServerTag = DnsSupport::finalDnsServerTag(
        !remoteDnsServer.isEmpty(),
        !directDnsServer.isEmpty(),
        hasPredefinedHosts,
        useDirectFinal);
    if (!finalServerTag.isEmpty()) {
        dns.insert(QStringLiteral("final"), finalServerTag);
    }

    // sing-box 1.14 removed `strategy` as a DNS rule action option, so the per-rule strategies the
    // direct and proxy rules used to carry collapse into this one global default. The proxy
    // strategy wins when both are configured, because `final` resolves through the proxy path.
    QString dnsStrategy = DnsSupport::mapDomainStrategy(config.dns().domainStrategyForProxy);
    if (dnsStrategy.isEmpty()) {
        dnsStrategy = DnsSupport::mapDomainStrategy(config.dns().domainStrategyForFreedom);
    }
    if (!dnsStrategy.isEmpty()) {
        dns.insert(QStringLiteral("strategy"), dnsStrategy);
    }

    QJsonArray rules;
    DnsRules::appendModeRules(
        rules,
        predefinedHostDomains,
        !remoteDnsServer.isEmpty(),
        !directDnsServer.isEmpty());
    DnsRules::appendHostRules(rules, hostsMap);

    if (config.dns().blockBindingQuery) {
        DnsRules::appendBlockBindingRule(rules);
    }

    if (config.dns().fakeIp && config.dns().globalFakeIp) {
        DnsRules::appendGlobalFakeIpRule(rules);
    }
    DnsRules::appendRoutingRules(rules, config, selectedRouting);

    if (config.dns().fakeIp && !config.dns().globalFakeIp && !useDirectFinal) {
        DnsRules::appendScopedFakeIpFallbackRule(rules);
    }

    if (!rules.isEmpty()) {
        dns.insert(QStringLiteral("rules"), rules);
    }

    return dns;
}

} // namespace SingBoxDnsConfigBuilder
