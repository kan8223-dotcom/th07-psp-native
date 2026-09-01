#pragma once

// A6v3 token for one synchronous MainMenu title01.anm load.  The token is
// local to ActualAddedCallback, so retry attempts cannot inherit state from a
// previous partial menu registration.
struct Th07PspTitleFontHoleSwap
{
    bool wasMainRam;
    bool demoteCallSucceeded;
    bool restoreRequired;
    unsigned long long startUs;
    unsigned long long demoteUs;
};

Th07PspTitleFontHoleSwap Th07PspBeginTitleFontHoleSwap();
void Th07PspEndTitleFontHoleSwap(const Th07PspTitleFontHoleSwap &swap,
                                bool titleLoadSucceeded);
