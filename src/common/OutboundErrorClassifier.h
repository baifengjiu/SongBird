#pragma once

#include <QString>

// What a failed outbound attempt actually says about the path, as opposed to how it was reported.
//
// A single "timed out" tells the user nothing useful: it is the same sentence whether the node's
// hostname does not resolve, the port refuses, the certificate names the wrong host, or the tunnel
// was established and then went silent. Those four need four different reactions, and the shapes
// are already distinguishable from the text the cores print -- which is how a user log was read by
// hand to tell "the node is reachable but the server behind it is dead" apart from "the node itself
// cannot be dialled".
//
// The classification is deliberately text-shape only. It never guesses from a generic word: a bare
// "i/o timeout" is Unknown, because on its own it does not say whether the dial or the tunnel
// timed out -- only "dial tcp ... i/o timeout" does.
enum class OutboundFailureShape {
    // Nothing to classify from, or a failure whose shape no pattern below covers.
    Unknown,
    // The path never carried a byte: DNS failed, the port refused, or the dial timed out.
    Unreachable,
    // The connection was made, but the certificate does not name the host being dialled.
    CertificateMismatch,
    // The connection was made and then closed with nothing on it -- the node answered TCP and then
    // said nothing, which is what a live node in front of a dead server looks like.
    TunnelBroken,
};

// `rawError` is one core log line or one reply error, not a joined blob: the shapes are decided per
// message, and a caller that has several reasons classifies the one that best describes the path.
inline OutboundFailureShape classifyOutboundFailure(const QString& rawError)
{
    const QString text = rawError.trimmed();
    if (text.isEmpty()) {
        return OutboundFailureShape::Unknown;
    }
    const QString lower = text.toLower();

    // The certificate named the wrong host. The cores phrase it as "x509: cannot validate
    // certificate for <ip> because it doesn't contain any IP SANs"; Qt's own wording names the
    // certificate too. Decided first: a message that names the certificate is never a plain dial
    // failure, so it must not fall through to the patterns below.
    if (lower.contains(QStringLiteral("x509")) || lower.contains(QStringLiteral("certificate"))) {
        return OutboundFailureShape::CertificateMismatch;
    }

    // The path never carried a byte. The cores say "dial tcp <host>:<port>: <cause>"; Qt says
    // "Connection refused" or "Host not found" for the same causes.
    const bool dialFailure = lower.contains(QStringLiteral("dial tcp"))
        || lower.contains(QStringLiteral("connection refused"))
        || lower.contains(QStringLiteral("no such host"))
        || lower.contains(QStringLiteral("host not found"))
        || lower.contains(QStringLiteral("name resolution"))
        || lower.contains(QStringLiteral("network is unreachable"));
    if (dialFailure) {
        return OutboundFailureShape::Unreachable;
    }

    // Connected, then closed with nothing on it: a *bare* EOF is the whole message. "unexpected
    // EOF" is a different shape and is left Unknown on purpose.
    if (lower == QStringLiteral("eof") || lower.endsWith(QStringLiteral(": eof"))) {
        return OutboundFailureShape::TunnelBroken;
    }

    return OutboundFailureShape::Unknown;
}
