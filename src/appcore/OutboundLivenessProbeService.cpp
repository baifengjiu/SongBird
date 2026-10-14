#include "appcore/OutboundLivenessProbeService.h"

#include <functional>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include "common/PortValidator.h"
#include "common/UserAgent.h"

namespace {

bool isReachableStatusCode(int statusCode)
{
    return statusCode == 200 || statusCode == 204;
}

QString livenessProbeErrorMessage(const QUrl& url, const QString& reason)
{
    const QString host = url.host();
    return QCoreApplication::translate("OutboundLivenessProbeService", "%1: %2").arg(host, reason);
}

QString replyFailureReason(QNetworkReply* reply)
{
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (statusCode > 0) {
        return QCoreApplication::translate("OutboundLivenessProbeService", "HTTP %1").arg(statusCode);
    }
    if (reply->error() != QNetworkReply::NoError) {
        return reply->errorString();
    }
    return QCoreApplication::translate("OutboundLivenessProbeService", "No response.");
}

// Which of two shapes describes the failure better. A certificate or tunnel problem names a
// specific layer, so it outranks the generic "could not connect"; the first shape seen wins a tie.
int failureShapeRank(OutboundFailureShape shape)
{
    switch (shape) {
    case OutboundFailureShape::CertificateMismatch:
        return 3;
    case OutboundFailureShape::TunnelBroken:
        return 2;
    case OutboundFailureShape::Unreachable:
        return 1;
    case OutboundFailureShape::Unknown:
        break;
    }
    return 0;
}

} // namespace

QStringList OutboundLivenessProbeService::probeUrls()
{
    return {
        QStringLiteral("http://www.gstatic.com/generate_204"),
        QStringLiteral("https://www.gstatic.com/generate_204"),
        QStringLiteral("http://cp.cloudflare.com/generate_204"),
        QStringLiteral("https://cp.cloudflare.com/generate_204"),
        QStringLiteral("http://connectivitycheck.gstatic.com/generate_204"),
        QStringLiteral("https://connectivitycheck.gstatic.com/generate_204"),
        QStringLiteral("http://www.msftconnecttest.com/connecttest.txt"),
        QStringLiteral("https://www.msftconnecttest.com/connecttest.txt"),
        QStringLiteral("http://detectportal.firefox.com/success.txt"),
        QStringLiteral("https://detectportal.firefox.com/success.txt")
    };
}

OutboundLivenessResult OutboundLivenessProbeService::probe(int httpPort) const
{
    return probe(httpPort, LivenessProbeTimeoutMs);
}

OutboundLivenessResult OutboundLivenessProbeService::probe(int httpPort, int timeoutMs) const
{
    if (!isValidTcpPort(httpPort)) {
        OutboundLivenessResult result;
        result.error = QCoreApplication::translate(
            "OutboundLivenessProbeService", "Outbound connection probe is unavailable.");
        return result;
    }
    return probeOnce(probeUrls(), httpPort, timeoutMs);
}

OutboundLivenessResult OutboundLivenessProbeService::probeDirect(int timeoutMs) const
{
    return probeDirect(probeUrls(), timeoutMs);
}

OutboundLivenessResult OutboundLivenessProbeService::probeDirect(const QStringList& urls, int timeoutMs) const
{
    return probeOnce(urls, 0, timeoutMs);
}

OutboundLivenessResult OutboundLivenessProbeService::probeOnce(
    const QStringList& urls,
    int httpPort,
    int timeoutMs)
{
    OutboundLivenessResult result;
    if (urls.isEmpty() || timeoutMs <= 0) {
        result.attemptedHosts = static_cast<int>(urls.size());
        result.error = QCoreApplication::translate(
            "OutboundLivenessProbeService", "Outbound connection probe is unavailable.");
        return result;
    }

    QElapsedTimer elapsed;
    elapsed.start();

    QNetworkAccessManager manager;
    // A non-positive port is the direct mode: the manager is left with whatever proxy the
    // environment gives it, which is what "this machine on its own" means.
    if (httpPort > 0) {
        manager.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), httpPort));
    }

    QEventLoop loop;
    QList<QNetworkReply*> replies;
    replies.reserve(urls.size());
    QStringList failureReasons;
    // The shape is decided per host and the most specific one wins, so a single host with a
    // certificate problem is not averaged away by four that merely refused to connect.
    OutboundFailureShape failureShape = OutboundFailureShape::Unknown;
    const auto recordFailure = [&failureReasons, &failureShape](const QString& reason) {
        failureReasons.append(reason);
        const OutboundFailureShape reasonShape = classifyOutboundFailure(reason);
        if (failureShapeRank(reasonShape) > failureShapeRank(failureShape)) {
            failureShape = reasonShape;
        }
    };
    bool completed = false;
    const int total = static_cast<int>(urls.size());
    int nextIndex = 0;
    int finishedCount = 0;

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        if (!completed) {
            const QString reason = QCoreApplication::translate(
                "OutboundLivenessProbeService", "Outbound connection request timed out.");
            for (int i = 0; i < replies.size(); ++i) {
                if (!replies.at(i)->isFinished()) {
                    recordFailure(livenessProbeErrorMessage(QUrl(urls.at(i)), reason));
                }
            }
        }
        loop.quit();
    });

    // Walk the pool two at a time. `replies.size() - finishedCount` is the number still in flight;
    // holding it under the limit keeps the gate from firing the whole pool at once, and refilling on
    // every completion keeps a reachable host from having to wait for a slow one ahead of it.
    std::function<void()> startMore;
    startMore = [&]() {
        while (!completed && (replies.size() - finishedCount) < LivenessProbeMaxConcurrent
            && nextIndex < total) {
            const QUrl probeUrl(urls.at(nextIndex));
            ++nextIndex;
            QNetworkRequest request(probeUrl);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            request.setHeader(QNetworkRequest::UserAgentHeader, fallbackUserAgent());

            QNetworkReply* reply = manager.get(request);
            replies.append(reply);
            const auto acceptReachableStatus = [&result, &loop, &completed, reply]() {
                if (!completed && isReachableStatusCode(
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())) {
                    result.reachable = true;
                    completed = true;
                    loop.quit();
                }
            };
            // 收到状态即可判定，不等待或解析响应体。
            QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, acceptReachableStatus);
            QObject::connect(reply, &QNetworkReply::finished, &loop,
                [&, reply, probeUrl, acceptReachableStatus]() {
                    acceptReachableStatus();
                    if (!completed) {
                        recordFailure(livenessProbeErrorMessage(probeUrl, replyFailureReason(reply)));
                    }
                    ++finishedCount;
                    if (completed) {
                        return;
                    }
                    if (finishedCount == total) {
                        loop.quit();
                    } else {
                        startMore();
                    }
                });
        }
    };
    startMore();

    if (!completed && finishedCount < total) {
        timeoutTimer.start(timeoutMs);
        loop.exec();
        timeoutTimer.stop();
    }

    for (QNetworkReply* reply : replies) {
        QObject::disconnect(reply, nullptr, &loop, nullptr);
        if (!reply->isFinished()) {
            reply->abort();
        }
        delete reply;
    }

    if (!completed) {
        result.error = failureReasons.join(QStringLiteral("; "));
        result.shape = failureShape;
    }
    // How many hosts were actually asked, not how many the pool held: a gate that passed on the
    // first pair and one that walked all ten are the same verdict and very different facts.
    result.attemptedHosts = nextIndex;
    result.elapsedMs = elapsed.elapsed();
    return result;
}
