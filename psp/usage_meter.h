/* SC/ME使用率メーター（XPタスクマネージャ様式・履歴グラフ）
 *
 * 読み取り専用の表示機能。VM/RNG/ゲーム状態に一切触れない。
 * 有効化: TH07_PSP_USAGE_METER（Makefile: PSP_USAGE_METER=1）。
 * 未定義時は全APIが空マクロになり、フック行を残しても全ビルド無害。
 *
 * 配線（このリビジョンで実施済み）:
 *  - BulletManager.cpp ME job完了経路: th07_usage_meter_add_me_cycles(
 *        invalidate + kernel + writeback)  … ME実測busy cycleの供給
 *  - PspGuGraphics.cpp AccumulateAndReportPerf: th07_usage_meter_frame(
 *        sampleUs)  … ACCEPT統計と同一のcritical値
 *  - PspGuGraphics.cpp EndScene: FlushDeferredSpriteDraw()直後に
 *        th07_usage_meter_draw()
 *
 * ME%換算: busyCycles × 2 ÷ 333 (µs)。CP0 CountはME worker
 * 起動時に初期化し、invalidate/kernel/writebackの全busy区間を合算する。
 */
#ifndef TH07_PSP_USAGE_METER_H
#define TH07_PSP_USAGE_METER_H

/* Two panels, each with 4 frame/panel vertices, 6 grid vertices and a
 * 64-sample history line.  The renderer reserves this much display-list
 * arena space before the raw GU overlay is appended. */
#define TH07_PSP_USAGE_METER_VERTEX_BYTES 1776u

#ifdef __cplusplus
extern "C" {
#endif

#if defined(TH07_PSP_USAGE_METER) || defined(TH07_PSP_ME_BUSY_METER)
void th07_usage_meter_add_me_cycles(unsigned int kernelCycles);
void th07_usage_meter_frame(unsigned int criticalUs);
/* Lagging safety signal for optional ME work.  It may veto admission but
 * must never be the positive admission condition. */
unsigned int th07_usage_meter_last_me_percent(void);
#if defined(TH07_PSP_USAGE_METER)
void th07_usage_meter_draw(void);
#else
#define th07_usage_meter_draw() ((void)0)
#endif
#else
#define th07_usage_meter_add_me_cycles(kernelCycles) ((void)0)
#define th07_usage_meter_frame(criticalUs) ((void)0)
#define th07_usage_meter_last_me_percent() (0u)
#define th07_usage_meter_draw() ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif
