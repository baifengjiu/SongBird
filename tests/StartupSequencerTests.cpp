#include <QtTest>

#include <QDir>

#include "app/StartupBanner.h"
#include "app/StartupCorePromptPolicy.h"
#include "app/StartupWindowPolicy.h"
#include "common/StartupSequencer.h"

class StartupSequencerTests : public QObject {
    Q_OBJECT

private slots:
    void runsStepsInDeclarationOrder();
    void reportsStepNamesInDeclarationOrder();
    void abortsAtFirstFailingGateAndSkipsTheRest();
    void stepsAndGatesInterleaveInDeclarationOrder();
    void reportsNoFailureWhenEveryEntrySucceeds();
    void emptySequenceCompletesImmediately();
    void rerunResetsTheExecutionTrace();
    void decideStartupWindowActionFollowsHideRequestAndTrayAvailability();
    void decideStartupCorePromptOffersDownloadWhenNothingWasFound();
    void decideStartupCorePromptSkipsWhenAnyCoreExists();
    void decideStartupCorePromptSkipsWhenTheOfferWasAlreadySpent();
    void decideStartupCorePromptSkipsWhenCoreChecksAreSkipped();
    void decideStartupCorePromptSkipsWhenPromptsAreDisabled();
    void startupBannerNamesTheVersionAndThePlatform();
    void startupBannerOmitsTheConfigLineWhenThereIsNoConfigPath();
    void startupBannerDropsOnlyTheMissingPathLines();
};

void StartupSequencerTests::runsStepsInDeclarationOrder()
{
    QStringList order;
    StartupSequencer sequencer;
    sequencer.addStep(QStringLiteral("first"), [&order]() { order.append(QStringLiteral("first")); });
    sequencer.addStep(QStringLiteral("second"), [&order]() { order.append(QStringLiteral("second")); });
    sequencer.addStep(QStringLiteral("third"), [&order]() { order.append(QStringLiteral("third")); });

    QVERIFY(sequencer.runAll());
    QCOMPARE(
        order,
        QStringList({QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third")}));
    QCOMPARE(sequencer.executedStepCount(), 3);
}

void StartupSequencerTests::reportsStepNamesInDeclarationOrder()
{
    StartupSequencer sequencer;
    sequencer.addStep(QStringLiteral("reload-config"), []() {});
    sequencer.addStep(QStringLiteral("present-main-window"), []() {});

    QCOMPARE(sequencer.stepCount(), 2);
    QCOMPARE(
        sequencer.stepNames(),
        QStringList({QStringLiteral("reload-config"), QStringLiteral("present-main-window")}));
}

void StartupSequencerTests::abortsAtFirstFailingGateAndSkipsTheRest()
{
    QStringList visited;
    StartupSequencer sequencer;
    sequencer.addStep(QStringLiteral("cleanup"), [&visited]() { visited.append(QStringLiteral("cleanup")); });
    sequencer.addGate(QStringLiteral("reload-config"), [&visited]() {
        visited.append(QStringLiteral("reload-config"));
        return false;
    });
    sequencer.addStep(QStringLiteral("present-main-window"), [&visited]() {
        visited.append(QStringLiteral("present-main-window"));
    });

    QVERIFY(!sequencer.runAll());
    QCOMPARE(
        visited,
        QStringList({QStringLiteral("cleanup"), QStringLiteral("reload-config")}));
    QCOMPARE(sequencer.executedStepCount(), 2);
    QCOMPARE(sequencer.failedStepName(), QStringLiteral("reload-config"));
}

void StartupSequencerTests::stepsAndGatesInterleaveInDeclarationOrder()
{
    QStringList order;
    StartupSequencer sequencer;
    sequencer.addGate(QStringLiteral("gate-a"), [&order]() {
        order.append(QStringLiteral("gate-a"));
        return true;
    });
    sequencer.addStep(QStringLiteral("step-b"), [&order]() { order.append(QStringLiteral("step-b")); });
    sequencer.addGate(QStringLiteral("gate-c"), [&order]() {
        order.append(QStringLiteral("gate-c"));
        return true;
    });

    QVERIFY(sequencer.runAll());
    QCOMPARE(
        order,
        QStringList({QStringLiteral("gate-a"), QStringLiteral("step-b"), QStringLiteral("gate-c")}));
}

void StartupSequencerTests::reportsNoFailureWhenEveryEntrySucceeds()
{
    StartupSequencer sequencer;
    sequencer.addStep(QStringLiteral("only"), []() {});

    QVERIFY(sequencer.runAll());
    QCOMPARE(sequencer.executedStepCount(), sequencer.stepCount());
    QVERIFY(sequencer.failedStepName().isEmpty());
}

void StartupSequencerTests::emptySequenceCompletesImmediately()
{
    StartupSequencer sequencer;

    QVERIFY(sequencer.runAll());
    QCOMPARE(sequencer.stepCount(), 0);
    QCOMPARE(sequencer.executedStepCount(), 0);
    QVERIFY(sequencer.failedStepName().isEmpty());
}

void StartupSequencerTests::rerunResetsTheExecutionTrace()
{
    StartupSequencer sequencer;
    sequencer.addGate(QStringLiteral("gate"), []() { return false; });
    sequencer.addStep(QStringLiteral("after"), []() {});

    QVERIFY(!sequencer.runAll());
    QCOMPARE(sequencer.executedStepCount(), 1);
    QCOMPARE(sequencer.failedStepName(), QStringLiteral("gate"));

    QVERIFY(!sequencer.runAll());
    QCOMPARE(sequencer.executedStepCount(), 1);
    QCOMPARE(sequencer.failedStepName(), QStringLiteral("gate"));
}

void StartupSequencerTests::decideStartupWindowActionFollowsHideRequestAndTrayAvailability()
{
    QCOMPARE(decideStartupWindowAction(false, true), StartupWindowAction::ShowNormally);
    QCOMPARE(decideStartupWindowAction(false, false), StartupWindowAction::ShowNormally);
    QCOMPARE(decideStartupWindowAction(true, true), StartupWindowAction::HideToTray);
    QCOMPARE(decideStartupWindowAction(true, false), StartupWindowAction::ShowMinimizedWithoutTray);
}

void StartupSequencerTests::decideStartupCorePromptOffersDownloadWhenNothingWasFound()
{
    QCOMPARE(
        decideStartupCorePrompt(0, false),
        StartupCorePromptDecision::OfferCoreDownload);
}

void StartupSequencerTests::decideStartupCorePromptSkipsWhenAnyCoreExists()
{
    QCOMPARE(
        decideStartupCorePrompt(1, false),
        StartupCorePromptDecision::SkipNoPrompt);
    QCOMPARE(
        decideStartupCorePrompt(3, false),
        StartupCorePromptDecision::SkipNoPrompt);
}

void StartupSequencerTests::decideStartupCorePromptSkipsWhenTheOfferWasAlreadySpent()
{
    QCOMPARE(
        decideStartupCorePrompt(0, true),
        StartupCorePromptDecision::SkipNoPrompt);
}

void StartupSequencerTests::decideStartupCorePromptSkipsWhenCoreChecksAreSkipped()
{
    // --skip-core asks the app not to look at cores at startup, and the prompt is
    // made of that lookup; the macOS smoke run would otherwise hang on the dialog.
    QCOMPARE(
        decideStartupCorePrompt(0, false, /*skipCoreChecks=*/true),
        StartupCorePromptDecision::SkipNoPrompt);
    QCOMPARE(
        decideStartupCorePrompt(0, false, /*skipCoreChecks=*/true, /*interactivePrompts=*/false),
        StartupCorePromptDecision::SkipNoPrompt);
}

void StartupSequencerTests::decideStartupCorePromptSkipsWhenPromptsAreDisabled()
{
    // --non-interactive is the flag whose whole purpose is "no blocking prompts".
    QCOMPARE(
        decideStartupCorePrompt(0, false, /*skipCoreChecks=*/false, /*interactivePrompts=*/false),
        StartupCorePromptDecision::SkipNoPrompt);
    // With prompts allowed and nothing skipped, it is still offered.
    QCOMPARE(
        decideStartupCorePrompt(0, false, /*skipCoreChecks=*/false, /*interactivePrompts=*/true),
        StartupCorePromptDecision::OfferCoreDownload);
}

// The banner exists because a pasted log used to carry no version at all, so the
// assertion that matters is simply that the version is in the line. It is the one
// fact in the panel that cannot be recovered from anything else in the report.
void StartupSequencerTests::startupBannerNamesTheVersionAndThePlatform()
{
    const QStringList lines = startupBannerLines(
        QStringLiteral("2.5.0-beta3"),
        QStringLiteral("5.15.2"),
        QStringLiteral("Windows 11 Version 23H2 x86_64"),
        QStringLiteral("C:/Users/someone/songbird.json"),
        QStringLiteral("C:/Users/someone/AppData/Roaming/SongBird/SongBird/logs/songbird-2026-10-13.000.log"));

    QCOMPARE(lines.size(), 3);
    QVERIFY2(lines.at(0).contains(QStringLiteral("2.5.0-beta3")), qPrintable(lines.at(0)));
    QVERIFY2(lines.at(0).contains(QStringLiteral("5.15.2")), qPrintable(lines.at(0)));
    QVERIFY2(
        lines.at(0).contains(QStringLiteral("Windows 11 Version 23H2 x86_64")),
        qPrintable(lines.at(0)));
    QVERIFY2(
        lines.at(1).contains(QDir::toNativeSeparators(QStringLiteral("C:/Users/someone/songbird.json"))),
        qPrintable(lines.at(1)));
    // The line that answers "which file do I attach?", which is the first question
    // anyone asking for a log has to ask and the user has no way to answer.
    QVERIFY2(
        lines.at(2).contains(
            QDir::toNativeSeparators(
                QStringLiteral("C:/Users/someone/AppData/Roaming/SongBird/SongBird/logs/songbird-2026-10-13.000.log"))),
        qPrintable(lines.at(2)));
}

void StartupSequencerTests::startupBannerOmitsTheConfigLineWhenThereIsNoConfigPath()
{
    // A build with no --config and no default yet. "Config: " with nothing after
    // it would be worse than no line, so the line has to disappear rather than
    // degrade.
    const QStringList empty = startupBannerLines(
        QStringLiteral("2.5.0-beta3"), QStringLiteral("5.15.2"), QStringLiteral("windows x86_64"), QString(), QString());
    QCOMPARE(empty.size(), 1);

    const QStringList blank = startupBannerLines(
        QStringLiteral("2.5.0-beta3"),
        QStringLiteral("5.15.2"),
        QStringLiteral("windows x86_64"),
        QStringLiteral("   "),
        QStringLiteral("   "));
    QCOMPARE(blank.size(), 1);

    // The version line is never dropped, whatever the config path is.
    QVERIFY2(empty.at(0).startsWith(QStringLiteral("SongBird 2.5.0-beta3")), qPrintable(empty.at(0)));
    QVERIFY2(blank.at(0).startsWith(QStringLiteral("SongBird 2.5.0-beta3")), qPrintable(blank.at(0)));
}

// The two paths are independent: a resolved config with no log file is the normal
// state when the writer could not be constructed, and reporting the log path in
// that case would name a file that is not being written.
void StartupSequencerTests::startupBannerDropsOnlyTheMissingPathLines()
{
    const QStringList noLogFile = startupBannerLines(
        QStringLiteral("2.5.0-beta3"),
        QStringLiteral("5.15.2"),
        QStringLiteral("windows x86_64"),
        QStringLiteral("C:/config/songbird.json"),
        QString());
    QCOMPARE(noLogFile.size(), 2);
    QVERIFY2(noLogFile.at(1).startsWith(QStringLiteral("Config: ")), qPrintable(noLogFile.at(1)));

    const QStringList noConfig = startupBannerLines(
        QStringLiteral("2.5.0-beta3"),
        QStringLiteral("5.15.2"),
        QStringLiteral("windows x86_64"),
        QString(),
        QStringLiteral("C:/logs/songbird-2026-10-13.000.log"));
    QCOMPARE(noConfig.size(), 2);
    QVERIFY2(noConfig.at(1).startsWith(QStringLiteral("Log: ")), qPrintable(noConfig.at(1)));
}

QTEST_MAIN(StartupSequencerTests)
#include "StartupSequencerTests.moc"