#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <utility>

#include "appcore/OutboundLivenessProbeService.h"

// Stands in for the core's local http inbound.
//
// A plain request gets the configured status code and body. A CONNECT tunnel is completed and then
// closed, so the https hosts fail within milliseconds -- deliberately, because whether they even
// send a CONNECT depends on the build: a Qt without a TLS backend aborts them at initialization
// before the request leaves, while one with a TLS backend opens a tunnel here. Answering and
// hanging up makes both builds fail those hosts the same way, so a case below sees every host
// answer instead of seeing two of them sit on an open tunnel until the deadline.
//
// That asymmetry is the point of most of these cases: the two plain-http hosts are the ones that
// have to be able to carry the verdict on their own.
class LivenessProxyServer
{
public:
    explicit LivenessProxyServer(int plainStatusCode, QByteArray body = {}, bool answersPlainRequests = true,
        int responseDelayMs = 0)
        : plainStatusCode_(plainStatusCode)
        , body_(std::move(body))
        , answersPlainRequests_(answersPlainRequests)
        , responseDelayMs_(responseDelayMs)
    {
        QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this]() {
            while (QTcpSocket* socket = server_.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    respond(socket);
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, [this, socket]() {
                    requests_.remove(socket);
                    answered_.remove(socket);
                });
            }
        });
    }

    ~LivenessProxyServer()
    {
        // Tear the sockets down here rather than letting ~QTcpServer do it after this destructor
        // body has run: deleting them now happens while the hash and set above are still alive, and
        // unhooking their signals first means none of the handlers above can run at all. A socket
        // whose destruction walks a half-torn-down container is a crash this project has hit.
        server_.close();
        const QList<QTcpSocket*> sockets = server_.findChildren<QTcpSocket*>();
        for (QTcpSocket* socket : sockets) {
            socket->disconnect();
            delete socket;
        }
    }

    LivenessProxyServer(const LivenessProxyServer&) = delete;
    LivenessProxyServer& operator=(const LivenessProxyServer&) = delete;

    bool listen()
    {
        return server_.listen(QHostAddress::LocalHost, 0);
    }

    int port() const
    {
        return server_.serverPort();
    }

    // The high-water mark of requests the probe had in flight at once. A request counts from the
    // moment it arrives until the moment it is answered, so the count never depends on how long a
    // torn-down socket takes to disappear -- the previous batch is decremented before its response
    // is even written, which is before the next batch can be sent.
    int maxConcurrentRequests() const
    {
        return maxInFlightRequests_;
    }

private:
    void respond(QTcpSocket* socket)
    {
        if (answered_.contains(socket)) {
            return;
        }

        requests_[socket].append(socket->readAll());
        const QByteArray& request = requests_[socket];
        if (!request.contains("\r\n\r\n")) {
            return; // header not complete yet
        }
        answered_.insert(socket);

        if (!answersPlainRequests_) {
            return; // accepted and deliberately left unanswered
        }

        ++inFlightRequests_;
        maxInFlightRequests_ = qMax(maxInFlightRequests_, inFlightRequests_);

        const QByteArray response = buildResponse(request);
        if (responseDelayMs_ > 0) {
            // Hold the request open long enough for a second one to overlap it. The context object
            // is the socket, so a socket torn down first takes the pending send with it.
            QTimer::singleShot(responseDelayMs_, socket, [this, socket, response]() {
                --inFlightRequests_;
                socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
            });
            return;
        }

        --inFlightRequests_;
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }

    QByteArray buildResponse(const QByteArray& request) const
    {
        if (request.startsWith("CONNECT ")) {
            return QByteArrayLiteral("HTTP/1.1 200 Connection Established\r\n\r\n");
        }

        QByteArray response = "HTTP/1.1 " + QByteArray::number(plainStatusCode_) + " "
            + reasonPhrase(plainStatusCode_) + "\r\n";
        response += "Content-Type: text/plain\r\n";
        response += "Content-Length: " + QByteArray::number(body_.size()) + "\r\n";
        response += "Connection: close\r\n\r\n";
        response += body_;
        return response;
    }

    static QByteArray reasonPhrase(int statusCode)
    {
        switch (statusCode) {
        case 200:
            return QByteArrayLiteral("OK");
        case 204:
            return QByteArrayLiteral("No Content");
        case 502:
            return QByteArrayLiteral("Bad Gateway");
        default:
            return QByteArrayLiteral("Status");
        }
    }

    // Declared before server_ on purpose: members are destroyed in reverse order, so server_ goes
    // first and the sockets it deletes still find a live hash when their disconnected handler runs.
    int plainStatusCode_ = 200;
    QByteArray body_;
    bool answersPlainRequests_ = true;
    int responseDelayMs_ = 0;
    int inFlightRequests_ = 0;
    int maxInFlightRequests_ = 0;
    QHash<QTcpSocket*, QByteArray> requests_;
    QSet<QTcpSocket*> answered_;
    QTcpServer server_;
};

class OutboundLivenessProbeServiceTests : public QObject
{
    Q_OBJECT

private slots:
    void thePoolCoversEveryCaptivePortalHost();
    void noMoreThanTwoHostsAreEverInFlight();
    void plainHttpHostsCarryTheVerdictWhenEveryHttpsHostFails();
    void aTwoHundredAndFourIsReachable();
    void anUnparseableTwoHundredStillCounts();
    void everyHostsReasonSurvivesInsteadOfOnlyTheDeadline();
    void aHostThatNeverAnswersIsBoundedByTheDeadline();
    void anUnusablePortOrBudgetIsReportedRatherThanWaitedOut();
    void theVerdictCarriesTheHostCountAndTheTimeItTook();
    void theHostCountStopsAtTheFirstAnswer();
    void theDirectProbeUsesTheSamePoolWithNoProxy();
    void theDirectProbeAgainstADeadPortFailsWithReasons();
};

// The gate must not be able to pass or fail on one host alone. Every host a browser or the OS
// itself uses for captive-portal detection is in the pool, so a single blocked or rate-limited
// service can never be the whole answer.
void OutboundLivenessProbeServiceTests::thePoolCoversEveryCaptivePortalHost()
{
    const QStringList urls = OutboundLivenessProbeService::probeUrls();
    const QStringList hosts{
        QStringLiteral("www.gstatic.com"),
        QStringLiteral("cp.cloudflare.com"),
        QStringLiteral("connectivitycheck.gstatic.com"),
        QStringLiteral("www.msftconnecttest.com"),
        QStringLiteral("detectportal.firefox.com")};

    for (const QString& host : hosts) {
        bool covered = false;
        for (const QString& url : urls) {
            if (url.contains(host)) {
                covered = true;
                break;
            }
        }
        QVERIFY2(covered, qPrintable(host));
    }
}

// Two at a time, not the whole pool. Every host answers with a status the gate rejects, so the probe
// has to walk all of them -- and the proxy, holding each connection open for a moment, is what makes
// "how many at once" observable at all. Without the cap this is the burst the gate used to fire.
void OutboundLivenessProbeServiceTests::noMoreThanTwoHostsAreEverInFlight()
{
    LivenessProxyServer proxy(502, {}, true, 120);
    QVERIFY(proxy.listen());

    OutboundLivenessProbeService probe;
    const OutboundLivenessResult result = probe.probe(proxy.port(), 5000);

    QVERIFY(!result.reachable);
    // Two at a time is the spec, not just a number the implementation happens to honour: pinning it
    // here means raising the cap is a deliberate act with a test to update, not a silent change.
    QCOMPARE(OutboundLivenessProbeService::LivenessProbeMaxConcurrent, 2);
    // And the implementation must actually stay inside its own cap. This bound is not vacuous: with
    // the cap removed the proxy sees five requests in flight at once.
    QVERIFY2(
        proxy.maxConcurrentRequests() <= OutboundLivenessProbeService::LivenessProbeMaxConcurrent,
        qPrintable(QString::number(proxy.maxConcurrentRequests())));
    // Walking to the last host proves the batches actually refill rather than stopping after the
    // first pair.
    QVERIFY2(result.error.contains(QStringLiteral("detectportal.firefox.com")), qPrintable(result.error));
}

// The case the split exists for. The node's TLS is broken, so every https host dies on the CONNECT
// tunnel -- and the gate must still pass, because the two plain-http hosts answer. Reading a parsed
// payload instead of a status code is what made this shape read as "no proxy at all".
void OutboundLivenessProbeServiceTests::plainHttpHostsCarryTheVerdictWhenEveryHttpsHostFails()
{
    LivenessProxyServer proxy(200);
    QVERIFY(proxy.listen());

    OutboundLivenessProbeService probe;
    const OutboundLivenessResult result = probe.probe(proxy.port(), 3000);

    QVERIFY2(result.reachable, qPrintable(result.error));
    QVERIFY(result.error.isEmpty());
}

void OutboundLivenessProbeServiceTests::aTwoHundredAndFourIsReachable()
{
    LivenessProxyServer proxy(204);
    QVERIFY(proxy.listen());

    OutboundLivenessProbeService probe;
    const OutboundLivenessResult result = probe.probe(proxy.port(), 3000);

    QVERIFY2(result.reachable, qPrintable(result.error));
}

// generate_204 endpoints answer an empty body, but a captive portal answers the same request with a
// 200 and an HTML page. Both are reachable; neither is parsed. This is the guard that keeps a body
// parser from creeping back in.
void OutboundLivenessProbeServiceTests::anUnparseableTwoHundredStillCounts()
{
    LivenessProxyServer proxy(200, QByteArrayLiteral("<html>sign in to continue</html>"));
    QVERIFY(proxy.listen());

    OutboundLivenessProbeService probe;
    const OutboundLivenessResult result = probe.probe(proxy.port(), 3000);

    QVERIFY2(result.reachable, qPrintable(result.error));
}

void OutboundLivenessProbeServiceTests::everyHostsReasonSurvivesInsteadOfOnlyTheDeadline()
{
    LivenessProxyServer proxy(502);
    QVERIFY(proxy.listen());

    OutboundLivenessProbeService probe;
    QElapsedTimer elapsed;
    elapsed.start();
    const OutboundLivenessResult result = probe.probe(proxy.port(), 4000);
    const qint64 spent = elapsed.elapsed();

    QVERIFY(!result.reachable);
    // Every host answered with a status the gate rejects, so all of their reasons are known and
    // none may be dropped in favour of one generic line.
    QVERIFY2(result.error.contains(QStringLiteral("www.gstatic.com")), qPrintable(result.error));
    QVERIFY2(result.error.contains(QStringLiteral("cp.cloudflare.com")), qPrintable(result.error));
    QVERIFY2(result.error.contains(QStringLiteral("502")), qPrintable(result.error));
    // Nothing was still in flight, so nothing timed out: claiming one would be a lie, and sitting
    // out the deadline to be able to tell it is pure delay.
    QVERIFY2(
        !result.error.contains(QCoreApplication::translate(
            "OutboundLivenessProbeService", "Outbound connection request timed out.")),
        qPrintable(result.error));
    // Half the budget: far above the cost of a round, far below what waiting it out would cost, so
    // the bound keeps its teeth on every platform.
    QVERIFY2(spent < 2000, qPrintable(QString::number(spent)));
}

// A host that accepts the connection and then says nothing is the case the deadline exists for.
void OutboundLivenessProbeServiceTests::aHostThatNeverAnswersIsBoundedByTheDeadline()
{
    LivenessProxyServer proxy(200, {}, false);
    QVERIFY(proxy.listen());

    OutboundLivenessProbeService probe;
    QElapsedTimer elapsed;
    elapsed.start();
    const OutboundLivenessResult result = probe.probe(proxy.port(), 400);
    const qint64 spent = elapsed.elapsed();

    QVERIFY(!result.reachable);
    QVERIFY2(
        result.error.contains(QCoreApplication::translate(
            "OutboundLivenessProbeService", "Outbound connection request timed out.")),
        qPrintable(result.error));
    QVERIFY2(spent >= 300, qPrintable(QString::number(spent)));
    QVERIFY2(spent < 2000, qPrintable(QString::number(spent)));
}

void OutboundLivenessProbeServiceTests::anUnusablePortOrBudgetIsReportedRatherThanWaitedOut()
{
    OutboundLivenessProbeService probe;

    const OutboundLivenessResult badPort = probe.probe(0, 3000);
    QVERIFY(!badPort.reachable);
    QVERIFY2(!badPort.error.isEmpty(), "an unusable port must say so rather than report silence");

    LivenessProxyServer proxy(204);
    QVERIFY(proxy.listen());
    const OutboundLivenessResult badBudget = probe.probe(proxy.port(), 0);
    QVERIFY(!badBudget.reachable);
    QVERIFY2(!badBudget.error.isEmpty(), "an unusable budget must say so rather than report silence");
}

// The gate's log line is only worth reading if it can say how many hosts were tried and how long
// the attempt took, and both have to come from the probe itself -- the caller cannot measure a
// budget the probe decided to spend differently.
void OutboundLivenessProbeServiceTests::theVerdictCarriesTheHostCountAndTheTimeItTook()
{
    LivenessProxyServer proxy(502);
    QVERIFY(proxy.listen());

    OutboundLivenessProbeService probe;
    const OutboundLivenessResult result = probe.probe(proxy.port(), 3000);

    QVERIFY(!result.reachable);
    // Every host rejected the request, so the probe had to walk the whole pool -- and it must report
    // that rather than the number it happened to reach before giving up.
    QCOMPARE(result.attemptedHosts, static_cast<int>(OutboundLivenessProbeService::probeUrls().size()));
    // A rejected round costs milliseconds, so this only pins the field as populated and sane: a
    // probe that reported the budget it never spent would be just as wrong as one reporting zero.
    QVERIFY2(result.elapsedMs > 0, qPrintable(QString::number(result.elapsedMs)));
    QVERIFY2(result.elapsedMs < 3000, qPrintable(QString::number(result.elapsedMs)));
}

// The count is the number asked, not the size of the pool: a reachable host stops the walk, and a
// line claiming ten hosts were tried would misdescribe exactly the case the log is read for.
void OutboundLivenessProbeServiceTests::theHostCountStopsAtTheFirstAnswer()
{
    LivenessProxyServer proxy(204);
    QVERIFY(proxy.listen());

    OutboundLivenessProbeService probe;
    const OutboundLivenessResult result = probe.probe(proxy.port(), 3000);

    QVERIFY2(result.reachable, qPrintable(result.error));
    QVERIFY2(result.attemptedHosts < static_cast<int>(OutboundLivenessProbeService::probeUrls().size()),
        qPrintable(QString::number(result.attemptedHosts)));
    QVERIFY2(result.attemptedHosts > 0, qPrintable(QString::number(result.attemptedHosts)));
}

// The cross-check the fail-open decision rests on. It has to be answerable against a local server,
// which is only true if it genuinely leaves the proxy unset -- with a proxy configured the request
// would be sent to it instead of to the host in the URL.
void OutboundLivenessProbeServiceTests::theDirectProbeUsesTheSamePoolWithNoProxy()
{
    LivenessProxyServer proxy(204);
    QVERIFY(proxy.listen());

    OutboundLivenessProbeService probe;
    const QStringList urls{
        QStringLiteral("http://127.0.0.1:%1/generate_204").arg(proxy.port())};
    const OutboundLivenessResult result = probe.probeDirect(urls, 3000);

    QVERIFY2(result.reachable, qPrintable(result.error));
    QCOMPARE(result.attemptedHosts, 1);
}

void OutboundLivenessProbeServiceTests::theDirectProbeAgainstADeadPortFailsWithReasons()
{
    int deadPort = 0;
    {
        LivenessProxyServer proxy(204);
        QVERIFY(proxy.listen());
        deadPort = proxy.port();
    } // The destructor closes the listener, so nothing is bound to that port any more.

    OutboundLivenessProbeService probe;
    const QStringList urls{
        QStringLiteral("http://127.0.0.1:%1/generate_204").arg(deadPort)};
    const OutboundLivenessResult result = probe.probeDirect(urls, 3000);

    QVERIFY(!result.reachable);
    QVERIFY2(result.error.contains(QStringLiteral("127.0.0.1")), qPrintable(result.error));
    QCOMPARE(result.attemptedHosts, 1);
}

QTEST_MAIN(OutboundLivenessProbeServiceTests)

#include "OutboundLivenessProbeServiceTests.moc"
