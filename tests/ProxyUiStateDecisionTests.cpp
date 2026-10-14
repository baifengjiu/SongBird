#include <QtTest>

#include "appcore/ProxyUiStateDecision.h"

class ProxyUiStateDecisionTests : public QObject
{
    Q_OBJECT

private slots:
    void activeWhenTheCoreIsReadyTheProxyIsAppliedAndTheOutboundAnswers();
    void activeIsRefusedWithoutAReachabilitySignal();
    void transitioningWinsOverEveryOtherInput();
    void idleOnlyWhenTheCoreIsNeitherReadyNorRunning();
    void aRunningButNotReadyCoreIsInconsistent();
};

void ProxyUiStateDecisionTests::activeWhenTheCoreIsReadyTheProxyIsAppliedAndTheOutboundAnswers()
{
    QCOMPARE(decideProxyUiState(false, true, true, true, true), ProxyUiState::Active);
}

// The guard AGENTS.md asks for: a core that is up and a system proxy that is applied are not
// enough. Without a real reachability signal the honest answer is Inconsistent, because the
// alternative is a window that says "connected" while the user cannot load a page.
void ProxyUiStateDecisionTests::activeIsRefusedWithoutAReachabilitySignal()
{
    QCOMPARE(decideProxyUiState(false, true, true, true, false), ProxyUiState::Inconsistent);
}

void ProxyUiStateDecisionTests::transitioningWinsOverEveryOtherInput()
{
    QCOMPARE(decideProxyUiState(true, true, true, true, true), ProxyUiState::Transitioning);
    QCOMPARE(decideProxyUiState(true, false, false, false, false), ProxyUiState::Transitioning);
}

void ProxyUiStateDecisionTests::idleOnlyWhenTheCoreIsNeitherReadyNorRunning()
{
    QCOMPARE(decideProxyUiState(false, false, false, false, false), ProxyUiState::Idle);
}

void ProxyUiStateDecisionTests::aRunningButNotReadyCoreIsInconsistent()
{
    QCOMPARE(decideProxyUiState(false, false, true, false, false), ProxyUiState::Inconsistent);
}

QTEST_MAIN(ProxyUiStateDecisionTests)

#include "ProxyUiStateDecisionTests.moc"
