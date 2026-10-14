#include <QtTest>

#include <QString>

#include "common/OutboundErrorClassifier.h"

namespace {

// Readable failures: the enum itself has no stream operator, and a QCOMPARE that prints
// "<unknown>" would hide which shape was actually returned.
QString shapeName(OutboundFailureShape shape)
{
    switch (shape) {
    case OutboundFailureShape::Unknown:
        return QStringLiteral("Unknown");
    case OutboundFailureShape::Unreachable:
        return QStringLiteral("Unreachable");
    case OutboundFailureShape::CertificateMismatch:
        return QStringLiteral("CertificateMismatch");
    case OutboundFailureShape::TunnelBroken:
        return QStringLiteral("TunnelBroken");
    }
    return QStringLiteral("?");
}

} // namespace

// The three shapes the criteria name, plus the two ways they must NOT fire: a message that is only
// ambiguous ("i/o timeout" without the dial context, "unexpected EOF") stays Unknown, because a
// wrong shape sends the user to fix the wrong thing.
class OutboundErrorClassifierTests : public QObject
{
    Q_OBJECT

private slots:
    void certificateMismatchIsRecognisedFromTheCoreWording();
    void certificateMismatchIsRecognisedFromQtsWording();
    void certificateIsDecidedBeforeTheDialPatterns();
    void dialTcpFailuresAreUnreachable();
    void qtDialWordingIsUnreachable();
    void bareEofIsATunnelThatCarriedNothing();
    void ambiguousMessagesStayUnknown();
};

void OutboundErrorClassifierTests::certificateMismatchIsRecognisedFromTheCoreWording()
{
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral(
            "dial tcp 127.0.0.1:443: x509: cannot validate certificate for 127.0.0.1 because it "
            "doesn't contain any IP SANs"))),
        QStringLiteral("CertificateMismatch"));
}

void OutboundErrorClassifierTests::certificateMismatchIsRecognisedFromQtsWording()
{
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("The certificate for this host could not be verified"))),
        QStringLiteral("CertificateMismatch"));
}

// "x509: ..." often travels inside a dial line, so the certificate has to be decided first or the
// dial pattern below would swallow it.
void OutboundErrorClassifierTests::certificateIsDecidedBeforeTheDialPatterns()
{
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("dial tcp 1.2.3.4:443: certificate is valid for a.example"))),
        QStringLiteral("CertificateMismatch"));
}

void OutboundErrorClassifierTests::dialTcpFailuresAreUnreachable()
{
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("dial tcp 1.2.3.4:443: connect: connection refused"))),
        QStringLiteral("Unreachable"));
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("dial tcp: lookup node.example: no such host"))),
        QStringLiteral("Unreachable"));
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("dial tcp 1.2.3.4:443: i/o timeout"))),
        QStringLiteral("Unreachable"));
}

void OutboundErrorClassifierTests::qtDialWordingIsUnreachable()
{
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("Connection refused"))),
        QStringLiteral("Unreachable"));
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("Host not found"))),
        QStringLiteral("Unreachable"));
}

void OutboundErrorClassifierTests::bareEofIsATunnelThatCarriedNothing()
{
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("EOF"))),
        QStringLiteral("TunnelBroken"));
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("inbound/http[location-probe]: read http request: EOF"))),
        QStringLiteral("TunnelBroken"));
}

void OutboundErrorClassifierTests::ambiguousMessagesStayUnknown()
{
    // No dial context, so nothing says whether the dial or the tunnel timed out.
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("i/o timeout"))),
        QStringLiteral("Unknown"));
    // A different shape from a bare EOF, and not one the criteria cover.
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("unexpected EOF"))),
        QStringLiteral("Unknown"));
    // Our own gate wording: it is a summary, not a shape.
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("Outbound connection request timed out."))),
        QStringLiteral("Unknown"));
    QCOMPARE(
        shapeName(classifyOutboundFailure(QStringLiteral("HTTP 502"))),
        QStringLiteral("Unknown"));
    QCOMPARE(shapeName(classifyOutboundFailure(QString())), QStringLiteral("Unknown"));
}

QTEST_MAIN(OutboundErrorClassifierTests)

#include "OutboundErrorClassifierTests.moc"
