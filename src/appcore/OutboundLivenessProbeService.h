#pragma once

#include <QString>
#include <QStringList>

#include "common/OutboundErrorClassifier.h"

// Answers one question: can traffic that goes through the core's outbound actually get somewhere?
//
// It exists because the startup gate used to be the location probe, and that probe asks a different
// question. A location probe succeeds only when a third-party geo service returns a payload this
// program can parse, so an exit that those five services rate-limit, block, or answer with a body
// the parser rejects looks exactly like a dead tunnel. The two are not the same thing, and the cost
// of confusing them is not symmetric: a location the user cannot see is a missing flag, while a
// tunnel wrongly declared dead costs the user the whole proxy -- the gate rolls the startup back.
//
// So the verdict here is a *status code* and nothing else. `generate_204` endpoints answer 204 with
// an empty body by design, which is what makes "reachable" answerable without parsing anything.
struct OutboundLivenessResult
{
    bool reachable = false;
    // Every host's own reason, joined, when the answer is "no". Never a single generic line: the
    // whole point of splitting this out is that the reason has to survive to the log.
    QString error;
    // What `error` says about the path, so the caller can tell "the node cannot be dialled" apart
    // from "the node answered and then the tunnel died" instead of reporting both as a timeout.
    // Unknown when reachable, or when no reason matches a shape the classifier knows.
    OutboundFailureShape shape = OutboundFailureShape::Unknown;
    // How many hosts were actually asked -- not the size of the pool. A gate that passed on the
    // first pair and one that walked all ten are the same verdict and very different facts, so a
    // failure log can say which of the two happened instead of leaving "it failed" as the only one.
    int attemptedHosts = 0;
    // Wall-clock time the probe spent. A gate that failed in 30 ms and one that burned the whole
    // budget read very differently to whoever is looking at the log afterwards.
    qint64 elapsedMs = 0;
};

class OutboundLivenessProbeService
{
public:
    // The gate runs before the system proxy is applied, so the user is unproxied while it runs.
    // Three seconds is the whole budget. The pool is walked two hosts at a time rather than in one
    // burst: two concurrent requests are already enough to tell "the tunnel is dead" from "this one
    // host is rate-limiting", and the extra hosts are only there so a single blocked host cannot
    // carry the verdict on its own.
    static constexpr int LivenessProbeTimeoutMs = 3000;
    static constexpr int LivenessProbeMaxConcurrent = 2;
    // The budget for the unproxied cross-check that runs only after the gate has already failed.
    // It answers a different question -- "can this machine reach anything at all?" -- and it is
    // deliberately shorter than the gate: it is a tie-breaker, not a second attempt, and the user
    // is waiting behind a startup overlay while it runs.
    static constexpr int LivenessProbeDirectTimeoutMs = 2000;

    // Every host a browser or the OS itself uses to detect a captive portal, each over both schemes.
    // Both schemes are deliberate: a Qt build without a TLS backend fails every https host before the
    // request even leaves, so the plain-http half is what keeps the gate answerable there.
    static QStringList probeUrls();

    OutboundLivenessResult probe(int httpPort) const;
    // The deadline as an argument, for the same reason ThreadShutdownBudget and LocationProbeTimings
    // are: the production budget is three seconds of wall clock, so a test that cannot shorten it
    // cannot assert on the branch that matters.
    OutboundLivenessResult probe(int httpPort, int timeoutMs) const;

    // The same pool, asked with no proxy at all. The gate answers "does traffic leaving through the
    // node get anywhere"; this answers "can this machine get anywhere on its own". Only the two
    // together can tell a dead node apart from a broken local network or DNS, and only the caller
    // that ran the gate knows which question it still needs answered.
    OutboundLivenessResult probeDirect(int timeoutMs) const;
    // The pool as an argument, so the no-proxy path can be driven against a local server instead of
    // the public hosts -- the same reason probe() takes its deadline.
    OutboundLivenessResult probeDirect(const QStringList& urls, int timeoutMs) const;

private:
    // `httpPort <= 0` means "no proxy": the direct probe is this same routine with the proxy left
    // unset, not a second implementation that could drift away from it.
    static OutboundLivenessResult probeOnce(const QStringList& urls, int httpPort, int timeoutMs);
};
