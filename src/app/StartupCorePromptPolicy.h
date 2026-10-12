#pragma once

// What the startup sequence should do about a missing core executable.
//
// A fresh install has no core anywhere on disk, and the main window's log only says
// *which file names it expected* -- it never offers to fetch one, so a user who
// cannot reach GitHub ends up with a client that starts and connects to nothing.
// The prompt used to be unthinkable inside run() because the "already asked, do
// not nag" rule needs persisted state; pulling the decision out keeps that rule
// covered without driving a real window, dialog or download.
enum class StartupCorePromptDecision {
    SkipNoPrompt,
    OfferCoreDownload,
};

// `existingCoreCount` is how many core executables were located in the candidate
// directories, `promptAlreadyShown` is the persisted one-shot flag. Either being
// non-zero/true means the prompt is either unnecessary or already spent.
//
// `skipCoreChecks` and `interactivePrompts` come from the command line
// (--skip-core) and its non-interactive mode, and they exist so automation never
// has to answer a modal prompt. Offering anyway parks the process on a dialog it
// cannot close, which is how the macOS smoke run came to hang: it passes both
// flags plus --quit-after-ms, and the app therefore never reached the timer that
// was supposed to quit it.
inline StartupCorePromptDecision decideStartupCorePrompt(
    int existingCoreCount,
    bool promptAlreadyShown,
    bool skipCoreChecks = false,
    bool interactivePrompts = true)
{
    if (skipCoreChecks || !interactivePrompts) {
        return StartupCorePromptDecision::SkipNoPrompt;
    }

    if (existingCoreCount > 0 || promptAlreadyShown) {
        return StartupCorePromptDecision::SkipNoPrompt;
    }

    return StartupCorePromptDecision::OfferCoreDownload;
}
