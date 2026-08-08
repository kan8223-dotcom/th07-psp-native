#pragma once

#include "AnmVm.hpp"
#include "ScreenEffect.hpp"
#include "ZunResult.hpp"
#include "ZunTimer.hpp"
#include "utils.hpp"

struct StageAnms
{
    const char *anmPath1;
    const char *anmPath2;
};
extern StageAnms g_EnemyAnmStageFiles[9];

struct StdRawHeader
{
    i16 objectsCount;
    i16 quadCount;
    u32 facesOffset;
    u32 scriptOffset;
    u32 unused_c;
    char stageName[128];
    char bgmNames[4][128];
    char bgmPaths[4][128];
};
static_assert(sizeof(StdRawHeader) == 0x490);

struct StdRawQuadBasic
{
    i16 type;
    i16 byteSize;
    i16 anmScript;
    i16 vmIndex;
    ZunVec3 pos;
    Float2 size;
};
static_assert(sizeof(StdRawQuadBasic) == 0x1c);

struct StdRawObject
{
    u16 id;
    i8 zLevel;
    i8 flags;
    ZunVec3 pos;
    ZunVec3 size;
    StdRawQuadBasic firstQuad;
};
static_assert(sizeof(StdRawObject) == 0x38);

struct StdRawInstance
{
    i16 id;
    i16 field1_0x2;
    ZunVec3 pos;
};
static_assert(sizeof(StdRawInstance) == 0x10);

struct StdRawInstrArgs
{
    AnyArg args[3];

    ZunVec3 *AsVec()
    {
        return (ZunVec3 *)args;
    }
};
static_assert(sizeof(StdRawInstrArgs) == 0xc);

struct StdRawInstr
{
    i32 frame;
    i16 opcode;
    i16 size;
    StdRawInstrArgs args;
};
static_assert(sizeof(StdRawInstr) == 0x14);

struct StageCameraSky
{
    f32 nearPlane;
    f32 farPlane;
};

struct StageCamera
{
    ZunVec3 pos;
    ZunVec3 lookAt;
    ZunVec3 up;
    ZunVec3 lookAtDir;
    ZunVec3 right;
    f32 fov;
};

struct StageFog
{
    f32 nearPlane;
    f32 farPlane;
    ZunColor color;
};

struct Stage
{
    Stage();

    static ZunResult RegisterChain(i32 stage);
    static void CutChain();

    static ZunResult AddedCallback(Stage *arg);
    static ZunResult DeletedCallback(Stage *arg);
    static u32 OnUpdate(Stage *arg);
    static u32 OnDrawHighPrio(Stage *arg);
    static u32 OnDrawLowPrio(Stage *arg);

    static void DrawColoredQuad(ZunRect *rect, u32 param_2, u32 param_3, u32 param_4,
                                u32 param_5);
    ZunResult LoadStageData(const char *stdPath);
    i32 RenderObjects(i32 param_1);
    void SmoothBlendColor(ZunColor param_1);
    void UpdateCamera();
    ZunResult UpdateObjects();
    void SetupCameraStageBackground();
    static void UpdateScriptAndCamera(Stage *stage, i32 param_2, ZunVec3 *param_3, ZunVec3 *param_4,
                                      ZunVec3 *param_5, ZunVec3 *param_6, ZunVec3 *param_7);

    AnmVm *quadVms;
    AnmVm vm1;
    AnmVm vm2;
    StdRawHeader *stdData;
    i32 quadCount;
    i32 objectsCount;
    StdRawObject **objects;
    StdRawInstance *objectInstances;
    StdRawInstr *beginningOfScript;
    ZunTimer scriptTime;
    i32 instructionIndex;
    i32 stageFrameCounter;
    u32 stage;
    ZunVec3 position;
    u32 color;
    StageFog skyFog;
    StageFog fogEnd;
    StageFog fogStart;
    i32 skyFogInterpDuration;
    ZunTimer skyFogInterpTimer;
    u8 renderStateWasReset;
    // pad 3
    i32 spellCardState;
    i32 ticksSinceSpellcardStarted;
    i32 clearBackground;
    i32 numSpellcardVms;
    i32 spellcardVmsIdx;
    AnmVm spellcardVms[32];
    AnmVm unusedVm;
    i32 scriptWaitTime;
    StageCamera camEnd;
    StageCamera camStart;
    StageCamera camTangentEnd;
    StageCamera camTangentStart;
    StageCamera cam;
    i32 timersMax[4];
    ZunTimer timers[4];
    i32 interpModes[4];
    ZunVec3 positionStart;
    i32 positionInterpEndTime;
    ZunVec3 positionInterpInitial;
    i32 positionInterpStartTime;
    u8 cameraTeleported;
    // pad 3
    ZunColor color2;
    i32 isDarkening;
#if defined(TH07_PSP)
    // Stage 4/5 contain enough repeated background instances that walking the
    // full STD instance list once for every Z layer becomes a measurable SC
    // cost.  These render-only caches preserve source order inside each layer.
    StdRawInstance *pspInstancesByZ[4];
    i32 pspInstanceCounts[4];
    f32 *pspObjectCullRadii;
    i32 pspInstanceBucketsReady;
    i32 pspCameraMatrixFrame;
    ZunMatrix pspCameraViewMatrix;
    ZunMatrix pspCameraProjectionMatrix;
    // Shared by all four background Z passes in one frame.
    ZunVec3 pspBillboardViewDir;
#endif
};

extern Stage g_Stage;
