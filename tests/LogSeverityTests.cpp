#include <QtTest>

#include "common/LogSeverity.h"

class LogSeverityTests : public QObject {
    Q_OBJECT

private slots:
    void markerIsStablePerSeverity();
    void coreOutputLevelComesFromTheLine_data();
    void coreOutputLevelComesFromTheLine();
    void levelWordInsideTheMessageBodyIsNotALevel_data();
    void levelWordInsideTheMessageBodyIsNotALevel();
};

void LogSeverityTests::markerIsStablePerSeverity()
{
    // Fixed-width, untranslated: the marker is a column the user scans and greps.
    QCOMPARE(logSeverityMarker(LogSeverity::Info), QStringLiteral("[I]"));
    QCOMPARE(logSeverityMarker(LogSeverity::Warning), QStringLiteral("[W]"));
    QCOMPARE(logSeverityMarker(LogSeverity::Error), QStringLiteral("[E]"));
}

void LogSeverityTests::coreOutputLevelComesFromTheLine_data()
{
    QTest::addColumn<QString>("line");
    QTest::addColumn<QString>("expectedMarker");

    // Xray. The first two are verbatim lines from the report this work started
    // from, so the shapes are taken from a real session rather than from the
    // cores' documentation.
    QTest::newRow("xray-error")
        << QStringLiteral(
               "ERROR[206174] [3631501782 5.7s] connection: open connection to api.onedrive.com:443 "
               "using outbound/anytls[proxy]: failed to create session: dial tcp 151.242.182.37:48633: "
               "i/o timeout")
        << QStringLiteral("[E]");
    QTest::newRow("xray-inbound-error")
        << QStringLiteral(
               "ERROR[205984] [1253981558 378ms] inbound/http[http]: process connection from "
               "127.0.0.1:64023: read http request: read tcp 127.0.0.1:10809->127.0.0.1:64023: use of "
               "closed network connection")
        << QStringLiteral("[E]");
    QTest::newRow("xray-warning")
        << QStringLiteral("WARNING[206174] [1 2s] transport: something recoverable")
        << QStringLiteral("[W]");
    QTest::newRow("xray-info")
        << QStringLiteral("INFO[206174] [1 2s] app: started")
        << QStringLiteral("[I]");

    // sing-box. With log.timestamp on (the default) the level sits behind a
    // timestamp, and sing-box may print the zone offset first.
    QTest::newRow("sing-box-error-with-zone")
        << QStringLiteral(
               "+0800 2026-10-12 18:45:13 ERROR inbound/http[location-probe]: process connection from "
               "127.0.0.1:64023: read http request: EOF")
        << QStringLiteral("[E]");
    QTest::newRow("sing-box-error-without-zone")
        << QStringLiteral("2026-10-12 18:45:13 ERROR route: rule-set not found")
        << QStringLiteral("[E]");
    QTest::newRow("sing-box-fatal")
        << QStringLiteral("2026-10-12 18:45:13 FATAL router: default domain resolver not found")
        << QStringLiteral("[E]");
    QTest::newRow("sing-box-warn")
        << QStringLiteral("2026-10-12 18:45:13 WARN dns: lookup took 5s")
        << QStringLiteral("[W]");
    QTest::newRow("sing-box-debug-with-t-separator")
        << QStringLiteral("2026-10-12T18:45:13 DEBUG dns: exchanged with 223.5.5.5")
        << QStringLiteral("[I]");

    // mihomo writes the level as a field.
    QTest::newRow("mihomo-error")
        << QStringLiteral("time=\"2026-10-12T18:45:13+08:00\" level=error msg=\"[DNS] resolve failed\"")
        << QStringLiteral("[E]");
    QTest::newRow("mihomo-warning")
        << QStringLiteral("time=\"2026-10-12T18:45:13+08:00\" level=warning msg=\"deprecated\"")
        << QStringLiteral("[W]");
    QTest::newRow("mihomo-info")
        << QStringLiteral("time=\"2026-10-12T18:45:13+08:00\" level=info msg=\"started\"")
        << QStringLiteral("[I]");

    // Bracketed level.
    QTest::newRow("bracketed-error")
        << QStringLiteral("[Error] transport: connection reset")
        << QStringLiteral("[E]");
    QTest::newRow("bracketed-warning")
        << QStringLiteral("[Warning] app: retrying")
        << QStringLiteral("[W]");

    // A line that carries no level at all is not an error by default.
    QTest::newRow("no-level-at-all")
        << QStringLiteral("inbound/http[http]: process connection from 127.0.0.1:64023: read http request: EOF")
        << QStringLiteral("[I]");
    QTest::newRow("empty-line") << QString() << QStringLiteral("[I]");
}

void LogSeverityTests::coreOutputLevelComesFromTheLine()
{
    QFETCH(QString, line);
    QFETCH(QString, expectedMarker);

    QCOMPARE(logSeverityMarker(logSeverityFromCoreOutput(line)), expectedMarker);
}

void LogSeverityTests::levelWordInsideTheMessageBodyIsNotALevel_data()
{
    QTest::addColumn<QString>("line");

    // Only the leading token, a leading bracket, or a "level=" field counts. A
    // level word appearing anywhere else is part of the message, and promoting
    // the line because of it would make the marker worthless as a filter.
    QTest::newRow("word-in-parentheses")
        << QStringLiteral("connection: dial tcp 1.2.3.4:443: i/o timeout (ERROR)");
    QTest::newRow("word-inside-a-hostname")
        << QStringLiteral("dns: lookup my.error.example.com: no such host");
    QTest::newRow("word-as-a-prefix-of-a-longer-word")
        << QStringLiteral("errors=0 warnings=0");
    QTest::newRow("word-after-a-component-name")
        << QStringLiteral("dns: level=notalevel");
}

void LogSeverityTests::levelWordInsideTheMessageBodyIsNotALevel()
{
    QFETCH(QString, line);

    QCOMPARE(logSeverityMarker(logSeverityFromCoreOutput(line)), QStringLiteral("[I]"));
}

QTEST_MAIN(LogSeverityTests)
#include "LogSeverityTests.moc"
