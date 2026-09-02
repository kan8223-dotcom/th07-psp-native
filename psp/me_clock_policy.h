#pragma once

// GO-ME2 policy math is kept PSP-independent so the 3000-neutral identity
// case and every fixed-point boundary can be proved on the host.  Wall times
// are produced by the runtime calibration; the ME endpoint is an amortized
// command round trip (dispatch/poll/retire included), not a CP0 clock claim.
// This header never reads a power or clock API.
enum
{
    TH07_PSP_ME_CLOCK_Q16_ONE = 65536u,
    TH07_PSP_ME_CLOCK_UNITY_LOW_PERMILLE = 970u,
    TH07_PSP_ME_CLOCK_UNITY_HIGH_PERMILLE = 1030u,
    TH07_PSP_ME_CLOCK_LEGACY_BUDGET_TICKS = 2220044u,
    TH07_PSP_ME_CLOCK_LEGACY_VETO_PERCENT = 85u
};

typedef struct Th07PspMeClockPolicy
{
    unsigned int rawRatioPermille;
    unsigned int meShareQ16;
    unsigned int admissionBudgetTicks;
    unsigned int vetoPercent;
    unsigned int snappedToUnity;
} Th07PspMeClockPolicy;

static inline unsigned int th07_psp_me_clock_ratio_permille(
    unsigned int scWallUs, unsigned int meWallUs)
{
    if (scWallUs == 0u)
        return 0u;
    return (unsigned int)(((unsigned long long)meWallUs * 1000u +
                           scWallUs / 2u) /
                          scWallUs);
}

static inline unsigned int th07_psp_me_clock_scale_floor(
    unsigned int value, unsigned int scaleQ16)
{
    return (unsigned int)(((unsigned long long)value * scaleQ16) >> 16);
}

static inline unsigned int th07_psp_me_clock_scale_round(
    unsigned int value, unsigned int scaleQ16)
{
    return (unsigned int)(
        ((unsigned long long)value * scaleQ16 +
         TH07_PSP_ME_CLOCK_Q16_ONE / 2u) >> 16);
}

static inline Th07PspMeClockPolicy th07_psp_me_clock_make_policy(
    unsigned int scWallUs, unsigned int meWallUs,
    unsigned int measurementValid)
{
    Th07PspMeClockPolicy policy;
    policy.rawRatioPermille =
        th07_psp_me_clock_ratio_permille(scWallUs, meWallUs);
    policy.meShareQ16 = TH07_PSP_ME_CLOCK_Q16_ONE;
    policy.admissionBudgetTicks =
        TH07_PSP_ME_CLOCK_LEGACY_BUDGET_TICKS;
    policy.vetoPercent = TH07_PSP_ME_CLOCK_LEGACY_VETO_PERCENT;
    policy.snappedToUnity = 1u;

    if (!measurementValid || scWallUs == 0u || meWallUs == 0u)
        return policy;

    // A 3% dead band makes equal-clock PSP-3000 decisions bit-identical in
    // the presence of boot-time scheduling/cache noise.  If ME is at least as
    // fast as SC, retain the established 80%-of-frame ceiling rather than
    // admitting more work than the proven GO-ME1/PSP-3000 contract.
    if (policy.rawRatioPermille >=
            TH07_PSP_ME_CLOCK_UNITY_LOW_PERMILLE &&
        policy.rawRatioPermille <=
            TH07_PSP_ME_CLOCK_UNITY_HIGH_PERMILLE)
        return policy;
    if (meWallUs <= scWallUs)
        return policy;

    policy.meShareQ16 = (unsigned int)(
        ((unsigned long long)scWallUs * TH07_PSP_ME_CLOCK_Q16_ONE) /
        meWallUs);
    if (policy.meShareQ16 == 0u)
        policy.meShareQ16 = 1u;
    if (policy.meShareQ16 > TH07_PSP_ME_CLOCK_Q16_ONE)
        policy.meShareQ16 = TH07_PSP_ME_CLOCK_Q16_ONE;
    policy.admissionBudgetTicks = th07_psp_me_clock_scale_floor(
        TH07_PSP_ME_CLOCK_LEGACY_BUDGET_TICKS, policy.meShareQ16);
    // Budget ticks remain a conservative floor.  The usage meter is already
    // an integer percentage and rejects on >=, so nearest rounding preserves
    // the scaled 85% decision boundary more faithfully than another floor.
    policy.vetoPercent = th07_psp_me_clock_scale_round(
        TH07_PSP_ME_CLOCK_LEGACY_VETO_PERCENT, policy.meShareQ16);
    if (policy.vetoPercent == 0u)
        policy.vetoPercent = 1u;
    policy.snappedToUnity = 0u;
    return policy;
}
