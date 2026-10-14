#pragma once

#include "domain/models/RuntimeState.h"

inline ProxyUiState decideProxyUiState(
    bool proxyTransitioning,
    bool coreReady,
    bool coreRunning,
    bool systemProxyApplied,
    bool outboundReachable)
{
    if (proxyTransitioning) {
        return ProxyUiState::Transitioning;
    }
    if (coreReady && systemProxyApplied && outboundReachable) {
        return ProxyUiState::Active;
    }
    if (!coreReady && !coreRunning) {
        return ProxyUiState::Idle;
    }
    return ProxyUiState::Inconsistent;
}
