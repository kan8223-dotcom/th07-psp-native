/* SC/ME使用率メーター実装。usage_meter.h と
 * FABLE_USAGE_METER_SPEC_20260831.md を参照。
 *
 * 状態規律（Codex指定条件）: 描画前に必要状態を明示設定し、描画後に
 * texture/depth-test/depth-mask/blend/fog をレンダラ初期化ブロック
 * （PspGuGraphics.cpp Init: blend=ON, alphaTest=ON, depthTest=OFF,
 *  depthMask=GU_FALSE, fog=OFF, texture=OFF）の基準値へ復元する。
 * ここが本体の状態キャッシュと矛盾しないことはCodexレビューで確認する。
 */
#if defined(TH07_PSP_USAGE_METER) || defined(TH07_PSP_ME_BUSY_METER)

#if defined(TH07_PSP_USAGE_METER_TOGGLE)
#include <pspctrl.h>
#endif
#if defined(TH07_PSP_USAGE_METER)
#include <pspgu.h>
#endif
#include "usage_meter.h"

#if defined(TH07_PSP_USAGE_METER)
/* ---- 配置（480x272画面座標、ロゴ上の空きに仮置き。実機目視で調整） ---- */
#define UM_X 408
#define UM_W 64
#define UM_H 26
#define UM_SC_Y 172
#define UM_ME_Y 208
#define UM_HISTORY 64

/* ABGR (PSPは0xAABBGGRR) */
#define C_PANEL 0xFF000000u
#define C_GRID 0xFF006600u
#define C_LINE 0xFF21FF00u
#define C_OVER 0xFF3C3CE6u
#define C_FRAME 0xFF99A8ACu

typedef struct
{
    unsigned int color;
    short x, y, z;
    short pad;
} UmVertex;

static unsigned char gScPct[UM_HISTORY];
static unsigned char gMePct[UM_HISTORY];
static unsigned int gHead;
#endif
static unsigned int gMeCycleAccum;
static unsigned int gLastMePct;

void th07_usage_meter_add_me_cycles(unsigned int kernelCycles)
{
    gMeCycleAccum += kernelCycles;
}

void th07_usage_meter_frame(unsigned int criticalUs)
{
    /* ME実測: busyCycles × 2 ÷ 333 = µs (CP0 Countは半速) */
    const unsigned int meUs = (unsigned int)(
        ((unsigned long long)gMeCycleAccum * 2ull) / 333ull);
    gMeCycleAccum = 0u;
    unsigned int me = meUs / 167u;
    gLastMePct = me;

#if defined(TH07_PSP_USAGE_METER)
    unsigned int sc = criticalUs / 167u; /* /16667×100 ≒ /167 (%) */
    if (sc > 200u)
        sc = 200u;
    if (me > 200u)
        me = 200u;
    gHead = (gHead + 1u) % UM_HISTORY;
    gScPct[gHead] = (unsigned char)sc;
    gMePct[gHead] = (unsigned char)me;
#else
    (void)criticalUs;
#endif
}

unsigned int th07_usage_meter_last_me_percent(void)
{
    return gLastMePct;
}

#if defined(TH07_PSP_USAGE_METER)
static UmVertex *um_quad(UmVertex *v, int x0, int y0, int x1, int y1,
                         unsigned int color)
{
    v[0].color = color;
    v[0].x = (short)x0;
    v[0].y = (short)y0;
    v[0].z = 0;
    v[1].color = color;
    v[1].x = (short)x1;
    v[1].y = (short)y1;
    v[1].z = 0;
    return v + 2;
}

static void um_panel(int px, int py, const unsigned char *hist)
{
    UmVertex *base = (UmVertex *)sceGuGetMemory(
        (2 * 2 + 3 * 2 + UM_HISTORY) * sizeof(UmVertex));
    UmVertex *v = base;
    int i;
    if (!base)
        return;

    v = um_quad(v, px - 1, py - 1, px + UM_W + 1, py + UM_H + 1, C_FRAME);
    v = um_quad(v, px, py, px + UM_W, py + UM_H, C_PANEL);
    sceGuDrawArray(GU_SPRITES,
                   GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 4, 0,
                   base);

    /* 25/50/75%横グリッド */
    {
        UmVertex *g = v;
        for (i = 1; i <= 3; ++i)
        {
            const int gy = py + UM_H - (UM_H * 25 * i) / 100;
            g[0].color = C_GRID;
            g[0].x = (short)px;
            g[0].y = (short)gy;
            g[0].z = 0;
            g[1].color = C_GRID;
            g[1].x = (short)(px + UM_W);
            g[1].y = (short)gy;
            g[1].z = 0;
            g += 2;
        }
        sceGuDrawArray(GU_LINES,
                       GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 6,
                       0, v);
        v = g;
    }

    /* 履歴折れ線: 右端が最新。100%超は上端振切り+赤。 */
    for (i = 0; i < UM_HISTORY; ++i)
    {
        const unsigned int idx = (gHead + 1u + (unsigned int)i) % UM_HISTORY;
        const unsigned int pct = hist[idx];
        const unsigned int clipped = pct > 100u ? 100u : pct;
        v[i].color = pct > 100u ? C_OVER : C_LINE;
        v[i].x = (short)(px + (i * UM_W) / (UM_HISTORY - 1));
        v[i].y = (short)(py + UM_H - (int)((UM_H * clipped) / 100u));
        v[i].z = 0;
    }
    sceGuDrawArray(GU_LINE_STRIP,
                   GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                   UM_HISTORY, 0, v);
}

void th07_usage_meter_draw(void)
{
#if defined(TH07_PSP_USAGE_METER_TOGGLE)
    /* Lトリガの立ち上がりで表示トグル（田中さん依頼）。ペーク読みなので
     * ゲーム側の入力消費とは干渉しない。非表示中も記録は継続する。 */
    static unsigned int sVisible = 1u;
    static unsigned int sPrevButtons;
    SceCtrlData pad;
    if (sceCtrlPeekBufferPositive(&pad, 1) > 0)
    {
        if ((pad.Buttons & PSP_CTRL_LTRIGGER) &&
            !(sPrevButtons & PSP_CTRL_LTRIGGER))
        {
            sVisible ^= 1u;
        }
        sPrevButtons = pad.Buttons;
    }
    if (!sVisible)
    {
        return;
    }
#endif

    /* 必要状態を明示設定（不透明オーバーレイ、Z非書込・非判定） */
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_TRUE); /* Z書込禁止 */

    um_panel(UM_X, UM_SC_Y, gScPct);
    um_panel(UM_X, UM_ME_Y, gMePct);

    /* レンダラInitブロック基準へ復元（texture/depth/blend/fog/depth-mask） */
    sceGuEnable(GU_BLEND);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_TEXTURE_2D);
}
#endif

#endif /* TH07_PSP_USAGE_METER || TH07_PSP_ME_BUSY_METER */
