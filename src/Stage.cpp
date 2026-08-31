#include "Stage.hpp"

#include "AnmIdx.hpp"
#include "AnmManager.hpp"
#include "Chain.hpp"
#include "EffectManager.hpp"
#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "GameManager.hpp"
#include "Gui.hpp"
#include "ScreenEffect.hpp"
#include "Supervisor.hpp"
#include "ZunResult.hpp"
#include "dxutil.hpp"
#include "utils.hpp"
#if defined(TH07_PSP_PERF_DIAG)
#include "fileio.hpp"
#endif
#if defined(TH07_PSP_PERF_DETAIL)
#include <pspkernel.h>

#include "graphics/PspGuGraphics.hpp"
#endif

StageAnms g_EnemyAnmStageFiles[9] = {
    {"dummy", "dummy"},         {"data/stg1enm.anm", NULL}, {"data/stg2enm.anm", NULL},
    {"data/stg3enm.anm", NULL}, {"data/stg4enm.anm", NULL}, {"data/stg5enm.anm", NULL},
    {"data/stg6enm.anm", NULL}, {"data/stg7enm.anm", NULL}, {"data/stg8enm.anm", NULL},
};

const char *g_StageFiles[9] = {
    "dummy",           "data/stage1.std", "data/stage2.std", "data/stage3.std", "data/stage4.std",
    "data/stage5.std", "data/stage6.std", "data/stage7.std", "data/stage8.std",
};

ChainElem g_UnusedChain;

ChainElem g_StageOnDrawHighPrioChain;

Stage g_Stage;

ChainElem g_StageOnDrawLowPrioChain;

ChainElem g_StageCalcChain;

Stage::Stage()
{
    memset(this, 0, sizeof(Stage));
    this->cam.pos = ZunVec3(0.0f, 0.0f, 1000.0f);
    this->cam.lookAt = ZunVec3(0.0f, 0.0f, 0.0f);
    this->cam.up = ZunVec3(0.0f, 1.0f, 0.0f);
    this->cam.fov = ZUN_PI / 6.0f;
    this->camEnd = this->cam;
    this->camStart = this->cam;
}

f32 InterpCubic(f32 p0, f32 p1, f32 p2, f32 p3, f32 t)
{
    f32 v[4];
    v[0] = (t - 1.0f) * (t - 1.0f) * (2.0f * t + 1.0f);
    v[2] = t * t * (3.0f - 2.0f * t);
    v[1] = (1.0f - t) * (1.0f - t) * t;
    v[3] = (t - 1.0f) * t * t;

    return v[0] * p0 + v[2] * p1 + v[1] * p2 + v[3] * p3;
}

void Stage::UpdateScriptAndCamera(Stage *stage, i32 param_2, ZunVec3 *param_3, ZunVec3 *param_4,
                                  ZunVec3 *param_5, ZunVec3 *param_6, ZunVec3 *param_7)
{
    f32 t;

    if (stage->timers[param_2] < stage->timersMax[param_2])
    {
        stage->timers[param_2]++;
        t = stage->timers[param_2].AsFloat() / (f32)stage->timersMax[param_2];
    }
    else
    {
        stage->timers[param_2] = stage->timersMax[param_2];
        t = 1.0f;
        stage->timersMax[param_2] = 0;
    }
    switch (stage->interpModes[param_2])
    {
    case 1:
        t = 1.0f - t;
        t = 1.0f - t * t;
        break;
    case 2:
        t = 1.0f - t;
        t = 1.0f - t * t * t;
        break;
    case 3:
        t = 1.0f - t;
        t = 1.0f - t * t * t * t;
        break;
    case 4:
        t = t * t;
        break;
    case 5:
        t = t * t * t;
        break;
    case 6:
        t = t * t * t * t;
    }
    if (stage->interpModes[param_2] != 7)
    {
        *param_3 = *param_5 - *param_4;
        *param_3 = t * *param_3 + *param_4;
    }
    else
    {
        param_3->x = InterpCubic(param_4->x, param_5->x, param_6->x, param_7->x, t);
        param_3->y = InterpCubic(param_4->y, param_5->y, param_6->y, param_7->y, t);
        param_3->z = InterpCubic(param_4->z, param_5->z, param_6->z, param_7->z, t);
    }
}

u32 Stage::OnUpdate(Stage *arg)
{
    ZunVec3 pos;
    StdRawInstr *curInstr;

    if (!arg->stdData)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }
    if (arg->scriptWaitTime != 0)
    {
        i32 instrIdx = 0;
        curInstr = arg->beginningOfScript;
        arg->instructionIndex = 0;
        while ((curInstr->opcode != 31 || arg->scriptWaitTime != curInstr->args.args[0].i) &&
               curInstr->frame != -1)
        {
            curInstr++;
            instrIdx++;
        }
        if (curInstr->frame != -1)
        {
            arg->instructionIndex = instrIdx + 1;
            arg->scriptTime = curInstr->frame;
            arg->scriptWaitTime = 0;
        }
    }
loop_begin:
    curInstr = arg->beginningOfScript + arg->instructionIndex;
    if (arg->scriptTime >= curInstr->frame && curInstr->frame != -1)
    {
        switch (curInstr->opcode)
        {
        case 0:
            if (curInstr->frame == -1)
            {
                arg->positionInterpInitial = *curInstr->args.AsVec();
                arg->position.x = arg->positionInterpInitial.x;
                arg->position.y = arg->positionInterpInitial.y;
                arg->position.z = arg->positionInterpInitial.z;
            }
            else
            {
                pos = *curInstr->args.AsVec();
                arg->position.x = pos.x;
                arg->position.y = pos.y;
                arg->position.z = pos.z;
                arg->positionInterpInitial = pos;
                arg->positionInterpStartTime = curInstr->frame;
                curInstr++;
                while (curInstr->opcode != 0)
                {
                    curInstr++;
                }
                arg->positionInterpEndTime = curInstr->frame;
                arg->positionStart = *curInstr->args.AsVec();
            }
            break;
        case 1:
            arg->skyFog.color.color = curInstr->args.args[0].u;
            arg->skyFog.nearPlane = curInstr->args.args[1].f;
            arg->skyFog.farPlane = curInstr->args.args[2].f;
            arg->fogStart = arg->skyFog;
            break;
        case 2:
            arg->fogEnd = arg->skyFog;
            arg->skyFogInterpDuration = curInstr->args.args[0].i;
            arg->skyFogInterpTimer = 0;
            break;
        case 5:
            if (arg->cameraTeleported)
            {
                ZunVec3 diff = *curInstr->args.AsVec() - arg->camEnd.pos;
                EffectManager::DoSomethingWithEffects(&diff);
                arg->cameraTeleported = 0;
            }
            arg->camStart.pos = arg->camEnd.pos;
            arg->camEnd.pos = *curInstr->args.AsVec();
            if (arg->timersMax[0] == 0)
            {
                arg->cam.pos = *curInstr->args.AsVec();
            }
            break;
        case 6:
            arg->timersMax[0] = curInstr->args.args[0].i;
            arg->timers[0] = 0;
            arg->interpModes[0] = curInstr->args.args[1].i;
            break;
        case 7:
            arg->camStart.lookAt = arg->camEnd.lookAt;
            arg->camEnd.lookAt = *curInstr->args.AsVec();
            if (arg->timersMax[1] == 0)
            {
                arg->cam.lookAt = *curInstr->args.AsVec();
            }
            break;
        case 8:
            arg->timersMax[1] = curInstr->args.args[0].i;
            arg->timers[1] = 0;
            arg->interpModes[1] = curInstr->args.args[1].i;
            break;
        case 9:
            arg->camStart.up = arg->camEnd.up;
            arg->camEnd.up = *curInstr->args.AsVec();
            if (arg->timersMax[2] == 0)
            {
                arg->cam.up = *curInstr->args.AsVec();
            }
            break;
        case 10:
            arg->timersMax[2] = curInstr->args.args[0].i;
            arg->interpModes[2] = curInstr->args.args[1].i;
            arg->timers[2] = 0;
            break;
        case 11:
            arg->camStart.fov = arg->camEnd.fov;
            arg->camEnd.fov = curInstr->args.args[0].f;
            if (arg->timersMax[3] == 0)
            {
                arg->cam.fov = curInstr->args.args[0].f;
            }
            break;
        case 12:
            arg->timersMax[3] = curInstr->args.args[0].i;
            arg->timers[3] = 0;
            arg->interpModes[3] = curInstr->args.args[1].i;
            break;
        case 13:
            arg->color = curInstr->args.args[0].u;
            break;
        case 3:
            if (arg->scriptWaitTime != 0)
            {
                arg->scriptWaitTime = 0;
                break;
            }
            goto LAB_004061aa;
        case 4:
            arg->instructionIndex = curInstr->args.args[0].i;
            arg->scriptTime = curInstr->args.args[1].i;
            arg->timersMax[0] = 0;
            arg->cameraTeleported = 1;
            goto loop_begin;
        case 14:
            arg->camStart.pos = *curInstr->args.AsVec();
            break;
        case 15:
            arg->camEnd.pos = *curInstr->args.AsVec();
            break;
        case 16:
            arg->camTangentStart.pos = *curInstr->args.AsVec();
            break;
        case 17:
            arg->camTangentEnd.pos = *curInstr->args.AsVec();
            break;
        case 18:
            arg->timersMax[0] = curInstr->args.args[0].i;
            arg->timers[0] = 0;
            arg->interpModes[0] = 7;
            break;
        case 19:
            arg->camStart.lookAt = *curInstr->args.AsVec();
            break;
        case 20:
            arg->camEnd.lookAt = *curInstr->args.AsVec();
            break;
        case 21:
            arg->camTangentStart.lookAt = *curInstr->args.AsVec();
            break;
        case 22:
            arg->camTangentEnd.lookAt = *curInstr->args.AsVec();
            break;
        case 23:
            arg->timersMax[1] = curInstr->args.args[0].i;
            arg->timers[1] = 0;
            arg->interpModes[1] = 7;
            break;
        case 24:
            arg->camStart.up = *curInstr->args.AsVec();
            break;
        case 25:
            arg->camEnd.up = *curInstr->args.AsVec();
            break;
        case 26:
            arg->camTangentStart.up = *curInstr->args.AsVec();
            break;
        case 27:
            arg->camTangentEnd.up = *curInstr->args.AsVec();
            break;
        case 28:
            arg->timersMax[2] = curInstr->args.args[0].i;
            arg->timers[2] = 0;
            arg->interpModes[2] = 7;
            break;
        case 29:
            if (curInstr->args.args[0].i >= 0)
            {
                g_AnmManager->ExecuteAnmIdx(&arg->vm1,
                                            curInstr->args.args[0].i + ANM_OFFSET_STAGE_BG1);
            }
            else
            {
                arg->vm1.activeSpriteIdx = -1;
            }
            break;
        case 30:
            if (curInstr->args.args[0].i >= 0)
            {
                g_AnmManager->ExecuteAnmIdx(&arg->vm2,
                                            curInstr->args.args[0].i + ANM_OFFSET_STAGE_BG1);
            }
            else
            {
                arg->vm1.activeSpriteIdx = -1;
            }
            break;
        }
        arg->instructionIndex++;
        goto loop_begin;
    }
LAB_004061aa: {
    i32 camIdx = 0;
    if (arg->timersMax[camIdx] != 0)
    {
        UpdateScriptAndCamera(arg, camIdx, &arg->cam.pos, &arg->camStart.pos, &arg->camEnd.pos,
                              &arg->camTangentStart.pos, &arg->camTangentEnd.pos);
    }
    camIdx = 1;
    if (arg->timersMax[camIdx] != 0)
    {
        UpdateScriptAndCamera(arg, camIdx, &arg->cam.lookAt, &arg->camStart.lookAt,
                              &arg->camEnd.lookAt, &arg->camTangentStart.lookAt,
                              &arg->camTangentEnd.lookAt);
    }
    camIdx = 2;
    if (arg->timersMax[camIdx] != 0)
    {
        UpdateScriptAndCamera(arg, camIdx, &arg->cam.up, &arg->camStart.up, &arg->camEnd.up,
                              &arg->camTangentStart.up, &arg->camTangentEnd.up);
    }
    camIdx = 3;

    if (arg->timersMax[camIdx] != 0)
    {
        f32 t;
        f32 fovDiff;

        if (arg->timers[camIdx] < arg->timersMax[camIdx])
        {
            arg->timers[camIdx]++;
            t = arg->timers[camIdx].AsFloat() / (f32)arg->timersMax[camIdx];
        }
        else
        {
            arg->timers[camIdx] = arg->timersMax[camIdx];
            t = 1.0f;
            arg->timersMax[camIdx] = 0;
        }
        switch (arg->interpModes[camIdx])
        {
        case 1:
            t = 1.0f - t;
            t = 1.0f - t * t;
            break;
        case 2:
            t = 1.0f - t;
            t = 1.0f - t * t * t;
            break;
        case 3:
            t = 1.0f - t;
            t = 1.0f - t * t * t * t;
            break;
        case 4:
            t = t * t;
            break;
        case 5:
            t = t * t * t;
            break;
        case 6:
            t = t * t * t * t;
        }
        fovDiff = arg->camEnd.fov - arg->camStart.fov;
        arg->cam.fov = fovDiff * t + arg->camStart.fov;
    }
}
    arg->cam.lookAtDir.Normalize(&arg->cam.lookAt);
    if (arg->skyFogInterpDuration != 0)
    {
        arg->skyFogInterpTimer++;
        f32 t = arg->skyFogInterpTimer.AsFloat() / (f32)arg->skyFogInterpDuration;
        if (t >= 1.0f)
        {
            t = 1.0f;
        }
        for (i32 i = 0; i < 4; i++)
        {
            arg->skyFog.color.raw[i] =
                (u8)(((f32)arg->fogStart.color.raw[i] - (f32)arg->fogEnd.color.raw[i]) * t +
                     (f32)arg->fogEnd.color.raw[i]);
        }
        arg->skyFog.nearPlane =
            (arg->fogStart.nearPlane - arg->fogEnd.nearPlane) * t + arg->fogEnd.nearPlane;
        arg->skyFog.farPlane =
            (arg->fogStart.farPlane - arg->fogEnd.farPlane) * t + arg->fogEnd.farPlane;

        if (arg->skyFogInterpTimer >= arg->skyFogInterpDuration)
        {
            arg->skyFogInterpDuration = 0;
        }
    }
    if (curInstr->opcode != 3)
    {
        arg->scriptTime++;
    }
    arg->UpdateObjects();
    if (arg->spellCardState >= 1)
    {
        if (arg->ticksSinceSpellcardStarted == 60)
        {
            arg->spellCardState++;
        }
        arg->ticksSinceSpellcardStarted++;
        for (i32 j = 0; j < arg->numSpellcardVms; j++)
        {
            g_AnmManager->ExecuteScript(&arg->spellcardVms[j]);
        }
    }
    if (arg->vm1.activeSpriteIdx > 0)
    {
        g_AnmManager->ExecuteScript(&arg->vm1);
    }
    if (arg->vm2.activeSpriteIdx > 0)
    {
        g_AnmManager->ExecuteScript(&arg->vm2);
    }
    arg->stageFrameCounter++;
    if (arg->stageFrameCounter % 500 == 250)
    {
        if (g_GameManager.CheckGameIntegrity())
        {
            return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
        }
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

void Stage::SmoothBlendColor(ZunColor param_1)
{
    ZunColor color;

    if (!this->color2.bytes.a)
    {
        this->color2 = param_1;
    }
    else
    {
        color = param_1;
        this->color2.bytes.r = (u8)((color.bytes.r + (u32)this->color2.bytes.r) >> 1);
        this->color2.bytes.g = (u8)((color.bytes.g + (u32)this->color2.bytes.g) >> 1);
        this->color2.bytes.b = (u8)((color.bytes.b + (u32)this->color2.bytes.b) >> 1);
        this->color2.bytes.a = (u8)((color.bytes.a + (u32)this->color2.bytes.a) >> 1);
    }
}

u32 Stage::OnDrawHighPrio(Stage *arg)
{
    ZunColor fogColor;
    ZunViewport viewport;

#if defined(TH07_PSP) && defined(TH07_PSP_LOWRES_STAGE_BG)
    // Half-resolution stage pass: measured 30->50-59 FPS in the stage-4 Lily
    // section, but the upscale blur was rejected on sight ("あらすぎて論外").
    // The mechanism stays compiled out unless explicitly enabled.
    Th07PspBeginLowResStagePass();
#endif
#if defined(TH07_PSP)
    // Lossless: the GUI frame opaquely covers everything outside the
    // playfield, so background fill there never reaches the eye.
    Th07PspBeginStagePlayfieldScissor();
#endif
    g_AnmManager->ResetVertexBuffer();
    g_AnmManager->SetVertexShader(255);
    g_AnmManager->SetSprite(NULL);
    g_AnmManager->SetTexture(0);
    g_AnmManager->SetColorOp(255);
    g_AnmManager->SetBlendMode(255);
    g_AnmManager->SetZWriteDisable(255);
    g_AnmManager->ClearFrameState();
    g_AnmManager->SetCameraMode(255);
    if (!g_Supervisor.cfg.disableFog)
    {
        g_Supervisor.DisableFog();
    }
    g_AnmManager->Flush();
    if (arg->clearBackground)
    {
        viewport.x = 32;
        viewport.y = 16;
        viewport.width = 384;
        viewport.height = 448;
        g_Supervisor.gfxDevice->SetViewport(viewport);
        g_Supervisor.gfxDevice->SetClearColor({0xff000000});
        g_Supervisor.gfxDevice->Clear(CLEAR_COLOR_BUFFER);
        arg->clearBackground = 0;
    }
    g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);
    if (arg->color2.bytes.a > 0)
    {
        g_AnmManager->SetColorWithMulEnabled(arg->color2.color);
    }
    arg->color2.bytes.a = 0;
    arg->color2.bytes.r = 128;
    arg->color2.bytes.g = 128;
    arg->color2.bytes.b = 128;
    if (arg->spellCardState <= 1)
    {
        if (!g_Gui.IsStageFinished())
        {
            if (0 < arg->vm1.activeSpriteIdx)
            {
                g_AnmManager->DrawAndFlush(&arg->vm1);
            }
            if (0 < arg->vm2.activeSpriteIdx)
            {
                g_AnmManager->DrawAndFlush(&arg->vm2);
            }
        }
    }
    if (arg->color != 0)
    {
        g_Supervisor.gfxDevice->SetClearColor({arg->color});
        g_Supervisor.gfxDevice->Clear(CLEAR_COLOR_BUFFER | CLEAR_DEPTH_BUFFER);
    }
    else
    {
        g_Supervisor.gfxDevice->Clear(CLEAR_DEPTH_BUFFER);
    }
    g_Supervisor.gfxDevice->SetDepthFunc(DEPTH_FUNC_LEQUAL);
    if (!g_AnmManager->colorMulEnabled)
    {
        g_Supervisor.gfxDevice->SetFogColor(arg->skyFog.color);
    }
    else
    {
        fogColor.color = arg->skyFog.color.color;
        fogColor.bytes.r = ZunColor::Multiply(fogColor.bytes.r, g_AnmManager->color.bytes.r);
        fogColor.bytes.g = ZunColor::Multiply(fogColor.bytes.g, g_AnmManager->color.bytes.g);
        fogColor.bytes.b = ZunColor::Multiply(fogColor.bytes.b, g_AnmManager->color.bytes.b);
        g_Supervisor.gfxDevice->SetFogColor(fogColor);
    }
    g_Supervisor.gfxDevice->SetFogRange(arg->skyFog.nearPlane, arg->skyFog.farPlane);
    if (!g_Supervisor.cfg.disableFog)
    {
        g_Supervisor.EnableFog();
    }
    if (arg->spellCardState <= 1)
    {
        if (!g_Gui.IsStageFinished())
        {
            arg->RenderObjects(0);
            arg->RenderObjects(1);
        }
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

u32 Stage::OnDrawLowPrio(Stage *arg)
{
    i32 i;
    ZunRect local_1c;
    i32 alpha;

    if (arg->spellCardState <= 1)
    {
        if (g_Gui.IsStageFinished() == 0)
        {
            arg->RenderObjects(2);
            arg->RenderObjects(3);
            if (!g_Supervisor.cfg.disableFog)
            {
                g_Supervisor.DisableFog();
            }
            g_EffectManager.UpdateSpecialEffect();
            if (arg->spellCardState == 1)
            {
                local_1c.left = 32.0f;
                local_1c.top = 16.0f;
                local_1c.right = 416.0f;
                local_1c.bottom = 464.0f;
                alpha = arg->ticksSinceSpellcardStarted * 255 / 60;
                g_AnmManager->Flush();
                g_Supervisor.gfxDevice->SetDepthFunc(DEPTH_FUNC_ALWAYS);
                if (!g_Supervisor.cfg.disableFog)
                {
                    g_Supervisor.gfxDevice->Disable(CAPS_FOG);
                }
                ScreenEffect::DrawSquare(&local_1c, alpha << 24);
            }
        }
    }
    g_AnmManager->Flush();
    g_Supervisor.gfxDevice->SetDepthFunc(DEPTH_FUNC_ALWAYS);
    if (!g_Supervisor.cfg.disableFog)
    {
        g_Supervisor.DisableFog();
    }
    if (arg->spellCardState >= 1)
    {
        for (i = 0; i < arg->numSpellcardVms; i++)
        {
            g_AnmManager->DrawAndFlush(&arg->spellcardVms[i]);
        }
    }
    AnmManager::SetCameraModeStatic(g_AnmManager, 0);
    arg->SetupCameraStageBackground();
    g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);
    g_Supervisor.gfxDevice->SetFogRange(1000.0f, 2000.0f);
    if (!arg->isDarkening)
    {
        g_AnmManager->SetColor(0x80808080);
    }
    arg->isDarkening = 0;
#if defined(TH07_PSP)
    Th07PspEndStagePlayfieldScissor();
#endif
#if defined(TH07_PSP) && defined(TH07_PSP_LOWRES_STAGE_BG)
    Th07PspEndLowResStagePass();
#endif
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

ZunResult Stage::AddedCallback(Stage *arg)
{
    i32 i;

    arg->scriptTime = 0;
    arg->instructionIndex = 0;
    arg->position.x = 0.0f;
    arg->position.y = 0.0f;
    arg->position.z = 0.0f;
    arg->spellCardState = 0;
    arg->skyFogInterpDuration = 0;
    switch (g_GameManager.currentStage)
    {
    case 1:
        if (g_AnmManager->LoadAnms(ANM_FILE_STAGE_BG1, "data/stg1bg.anm", ANM_OFFSET_STAGE_BG1) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 2:
        if (g_AnmManager->LoadAnms(ANM_FILE_STAGE_BG1, "data/stg2bg.anm", ANM_OFFSET_STAGE_BG1) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 3:
        if (g_AnmManager->LoadAnms(ANM_FILE_STAGE_BG1, "data/stg3bg.anm", ANM_OFFSET_STAGE_BG1) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 4:
        if (g_AnmManager->LoadAnms(ANM_FILE_STAGE_BG1, "data/stg4bg.anm", ANM_OFFSET_STAGE_BG1) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }

        if (g_AnmManager->LoadAnms(ANM_FILE_STAGE_BG2, "data/stg4bg2.anm", ANM_OFFSET_STAGE_BG2) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }

        if (g_AnmManager->LoadAnms(ANM_FILE_STAGE_BG3, "data/stg4bg3.anm", ANM_OFFSET_STAGE_BG3) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }

        if (g_AnmManager->LoadAnms(ANM_FILE_STAGE_BG4, "data/stg4bg4.anm", ANM_OFFSET_STAGE_BG4) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }

        if (g_AnmManager->LoadAnms(ANM_FILE_STAGE_BG5, "data/stg4bg5.anm", ANM_OFFSET_STAGE_BG5) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 5:
        if (g_AnmManager->LoadAnms(ANM_FILE_STAGE_BG1, "data/stg5bg.anm", ANM_OFFSET_STAGE_BG1) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 6:
        if (g_AnmManager->LoadAnms(ANM_FILE_STAGE_BG1, "data/stg6bg.anm", ANM_OFFSET_STAGE_BG1) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 7:
        if (g_AnmManager->LoadAnms(ANM_FILE_STAGE_BG1, "data/stg7bg.anm", ANM_OFFSET_STAGE_BG1) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 8:
        if (g_AnmManager->LoadAnms(ANM_FILE_STAGE_BG1, "data/stg8bg.anm", ANM_OFFSET_STAGE_BG1) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
    }
    if (arg->LoadStageData(g_StageFiles[g_GameManager.currentStage]) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }

    arg->skyFog.color.color = 0xff000000;
    arg->skyFog.nearPlane = 200.0f;
    arg->skyFog.farPlane = 500.0f;
    arg->cam.pos = ZunVec3(0.0f, 0.0f, 1000.0f);
    arg->cam.lookAt = ZunVec3(0.0f, 0.0f, 0.0f);
    arg->cam.up = ZunVec3(0.0f, 1.0f, 0.0f);
    arg->cam.fov = ZUN_PI / 6.0f;
    arg->camEnd = arg->cam;
    arg->camStart = arg->cam;
    for (i = 0; i < 4; i++)
    {
        arg->timersMax[i] = 0;
        arg->timers[i] = 0;
    }
    arg->scriptWaitTime = 0;
    return ZUN_SUCCESS;
}

ZunResult Stage::DeletedCallback(Stage *arg)
{
    g_AnmManager->ReleaseAnm(5);
    g_AnmManager->ReleaseAnm(6);
    g_AnmManager->ReleaseAnm(7);
    g_AnmManager->ReleaseAnm(8);
    g_AnmManager->ReleaseAnm(9);
#if defined(TH07_PSP)
    for (i32 z = 0; z < 4; ++z)
    {
        SAFE_FREE(arg->pspInstancesByZ[z]);
        arg->pspInstanceCounts[z] = 0;
    }
    SAFE_FREE(arg->pspObjectCullRadii);
    arg->pspInstanceBucketsReady = 0;
#endif
    SAFE_DELETE_ARRAY(arg->objects);
    SAFE_FREE(arg->quadVms);
    SAFE_FREE(arg->stdData);
    return ZUN_SUCCESS;
}

ZunResult Stage::RegisterChain(i32 stage)
{
    Stage *mgr = &g_Stage;
    memset(mgr, 0, sizeof(Stage));
    mgr->stdData = NULL;
    mgr->stageFrameCounter = 0;
    mgr->stage = stage;
    g_StageCalcChain.callback = (ChainCallback)OnUpdate;
    g_StageCalcChain.addedCallback = NULL;
    g_StageCalcChain.deletedCallback = NULL;
    g_StageCalcChain.addedCallback = (ChainLifecycleCallback)AddedCallback;
    g_StageCalcChain.deletedCallback = (ChainLifecycleCallback)DeletedCallback;
    g_StageCalcChain.arg = mgr;
    if (g_Chain.AddToCalcChain(&g_StageCalcChain, 7))
    {
        return ZUN_ERROR;
    }

    g_StageOnDrawHighPrioChain.callback = (ChainCallback)OnDrawHighPrio;
    g_StageOnDrawHighPrioChain.addedCallback = NULL;
    g_StageOnDrawHighPrioChain.deletedCallback = NULL;
    g_StageOnDrawHighPrioChain.arg = mgr;
    g_Chain.AddToDrawChain(&g_StageOnDrawHighPrioChain, 3);
    g_StageOnDrawLowPrioChain.callback = (ChainCallback)OnDrawLowPrio;
    g_StageOnDrawLowPrioChain.addedCallback = NULL;
    g_StageOnDrawLowPrioChain.deletedCallback = NULL;
    g_StageOnDrawLowPrioChain.arg = mgr;
    g_Chain.AddToDrawChain(&g_StageOnDrawLowPrioChain, 4);
    return ZUN_SUCCESS;
}

void Stage::CutChain()
{
    g_Chain.Cut(&g_StageCalcChain);
    g_Chain.Cut(&g_StageOnDrawHighPrioChain);
    g_Chain.Cut(&g_StageOnDrawLowPrioChain);
}

ZunResult Stage::LoadStageData(const char *stdPath)
{
    StdRawQuadBasic *quad;
    StdRawObject *obj;
    i32 i;
    i32 vmIdx;

    this->stdData = (StdRawHeader *)FileSystem::OpenFile(stdPath, 0);
    if (!this->stdData)
    {
        g_GameErrorContext.Log("ステージデータが見つかりません。データが壊れています\n");
        return ZUN_ERROR;
    }

    this->objectsCount = this->stdData->objectsCount;
    this->quadCount = this->stdData->quadCount;
    this->objectInstances = (StdRawInstance *)(this->stdData->facesOffset + (u8 *)this->stdData);
    this->beginningOfScript = (StdRawInstr *)(this->stdData->scriptOffset + (u8 *)this->stdData);

    u32 *offsets = (u32 *)(this->stdData + 1);
    this->objects = new StdRawObject *[this->objectsCount];
    for (i = 0; i < this->objectsCount; i++)
    {
        this->objects[i] = (StdRawObject *)((u8 *)this->stdData + offsets[i]);
    }
    this->quadVms = (AnmVm *)malloc(this->quadCount * sizeof(AnmVm));
    for (i = 0, vmIdx = 0; i < this->objectsCount; i++)
    {
        obj = this->objects[i];
        obj->flags = 1;
        quad = &obj->firstQuad;
        while (quad->type >= 0)
        {
            g_AnmManager->ExecuteAnmIdx(&this->quadVms[vmIdx],
                                        quad->anmScript + ANM_OFFSET_STAGE_BG1);
            quad->vmIndex = vmIdx++;
            quad = (StdRawQuadBasic *)((u8 *)quad + quad->byteSize);
        }
    }
#if defined(TH07_PSP)
    // The original renderer scans the complete instance stream four times,
    // once per Z layer.  Bucket immutable STD instances at load time, keeping
    // their original order so alpha/depth behaviour is unchanged.  Each
    // bucket carries the same negative-id sentinel as the source stream.
    i32 bucketCounts[4] = {};
    for (StdRawInstance *instance = this->objectInstances; instance->id >= 0; ++instance)
    {
        if (instance->id < this->objectsCount)
        {
            const i32 z = this->objects[instance->id]->zLevel;
            if (z >= 0 && z < 4)
            {
                ++bucketCounts[z];
            }
        }
    }

    bool bucketsReady = true;
    for (i32 z = 0; z < 4; ++z)
    {
        this->pspInstancesByZ[z] = static_cast<StdRawInstance *>(
            malloc(static_cast<size_t>(bucketCounts[z] + 1) * sizeof(StdRawInstance)));
        if (!this->pspInstancesByZ[z])
        {
            bucketsReady = false;
            break;
        }
        this->pspInstanceCounts[z] = 0;
    }
    if (bucketsReady)
    {
        for (StdRawInstance *instance = this->objectInstances; instance->id >= 0; ++instance)
        {
            if (instance->id >= this->objectsCount)
            {
                continue;
            }
            const i32 z = this->objects[instance->id]->zLevel;
            if (z >= 0 && z < 4)
            {
                this->pspInstancesByZ[z][this->pspInstanceCounts[z]++] = *instance;
            }
        }
        for (i32 z = 0; z < 4; ++z)
        {
            StdRawInstance &sentinel = this->pspInstancesByZ[z][this->pspInstanceCounts[z]];
            memset(&sentinel, 0, sizeof(sentinel));
            sentinel.id = -1;
        }
        this->pspInstanceBucketsReady = 1;
#if defined(TH07_PSP_PERF_DIAG)
        th07_psp_boot_notef("stage bg buckets %d/%d/%d/%d", this->pspInstanceCounts[0],
                            this->pspInstanceCounts[1], this->pspInstanceCounts[2],
                            this->pspInstanceCounts[3]);
#endif
    }
    else
    {
        for (i32 z = 0; z < 4; ++z)
        {
            SAFE_FREE(this->pspInstancesByZ[z]);
            this->pspInstanceCounts[z] = 0;
        }
        this->pspInstanceBucketsReady = 0;
    }

    // Object dimensions never change.  Avoid one sqrt per visible instance
    // per frame in the gate/stair backgrounds; fall back to the exact source
    // expression if this small optional cache cannot be allocated.
    this->pspObjectCullRadii =
        static_cast<f32 *>(malloc(static_cast<size_t>(this->objectsCount) * sizeof(f32)));
    if (this->pspObjectCullRadii)
    {
        for (i32 objectIdx = 0; objectIdx < this->objectsCount; ++objectIdx)
        {
            this->pspObjectCullRadii[objectIdx] =
                this->objects[objectIdx]->size.Length() * 0.5f + 880.0f;
        }
    }
#endif
    return ZUN_SUCCESS;
}

ZunResult Stage::UpdateObjects()
{
    StdRawQuadBasic *quad;
    StdRawObject *object;
    AnmVm *vm;
    i32 i;
    i32 vmCount;

    for (i = 0; i < this->objectsCount; i++)
    {
        object = this->objects[i];
        if ((object->flags & 1) != 0)
        {
            vmCount = 0;
            quad = &object->firstQuad;
            while (0 <= quad->type)
            {
                vm = &this->quadVms[quad->vmIndex];
                switch (quad->type)
                {
                case 0:
                    g_AnmManager->ExecuteScript(vm);
                    break;
                case 1:
                    g_AnmManager->ExecuteScript(vm);
                    break;
                }
                if (vm->currentInstruction)
                {
                    vmCount++;
                }
                quad = (StdRawQuadBasic *)((u8 *)quad + quad->byteSize);
            }
            if (vmCount == 0)
            {
                object->flags &= 0xfffffffe;
            }
        }
    }
    return ZUN_SUCCESS;
}

i32 Stage::RenderObjects(i32 zLevel)
{
#if defined(TH07_PSP_PERF_DETAIL)
    const unsigned long long pspStageStartUs = sceKernelGetSystemTimeWide();
#endif
    ZunColor origColor;
    f32 var_98;
    ZunVec3 projectSrc;
    f32 radius;
    StdRawQuadBasic *curQuad;
    ZunVec3 diffPos;
    ZunVec3 quadPos;
    ZunVec3 viewDir;
    f32 dotProd;
    StdRawObject *obj;
    ZunMatrix worldMatrix;
    i32 fogState;
    StdRawInstance *instance;
    AnmVm *curQuadVm;

#if defined(TH07_PSP)
    instance = this->pspInstanceBucketsReady && zLevel >= 0 && zLevel < 4
                   ? this->pspInstancesByZ[zLevel]
                   : this->objectInstances;
#else
    instance = this->objectInstances;
#endif
    projectSrc.x = 0.0f;
    projectSrc.y = 0.0f;
    projectSrc.z = 0.0f;
    fogState = 255;

    UpdateCamera();

    AnmManager::SetCameraModeStatic(g_AnmManager, 1);

    worldMatrix.Identity();

    while (instance->id >= 0)
    {
        obj = this->objects[instance->id];
        if (obj->zLevel == zLevel)
        {
            curQuad = &obj->firstQuad;

            quadPos.x = obj->pos.x + instance->pos.x - this->position.x + obj->size.x / 2.0f;
            quadPos.y = obj->pos.y + instance->pos.y - this->position.y + obj->size.y / 2.0f;
            quadPos.z = obj->pos.z + instance->pos.z - this->position.z + obj->size.z / 2.0f;

            quadPos = quadPos - this->cam.pos;

            if (quadPos.LengthSq() > 1690000.0f)
            {
                // empty branch
            }
            else
            {
                dotProd = quadPos.Dot(&this->cam.lookAtDir);
#if defined(TH07_PSP)
                radius = this->pspObjectCullRadii
                             ? this->pspObjectCullRadii[instance->id]
                             : obj->size.Length() * 0.5f + 880.0f;
#else
                radius = obj->size.Length() / 2.0f + 880.0f;
#endif

                if (dotProd > radius || dotProd < 60.0f)
                {
                    // empty branch
                }
                else
                {
                    obj->flags |= 2;

                    while (curQuad->type >= 0)
                    {
                        curQuadVm = &this->quadVms[curQuad->vmIndex];
                        switch (curQuad->type)
                        {
                        case 0:
                            curQuadVm->pos.x = curQuadVm->offset.x + curQuad->pos.x +
                                               instance->pos.x - this->position.x;
                            curQuadVm->pos.y = curQuadVm->offset.y + curQuad->pos.y +
                                               instance->pos.y - this->position.y;
                            curQuadVm->pos.z = curQuadVm->offset.z + curQuad->pos.z +
                                               instance->pos.z - this->position.z;

                            if (curQuad->size.x != 0.0f)
                            {
                                curQuadVm->scale.x = curQuad->size.x / curQuadVm->sprite->widthPx;
                            }
                            if (curQuad->size.y != 0.0f)
                            {
                                curQuadVm->scale.y = curQuad->size.y / curQuadVm->sprite->heightPx;
                            }

                            if (curQuadVm->autoRotate == 2)
                            {
                                worldMatrix.m[3][0] = curQuadVm->pos.x;
                                worldMatrix.m[3][1] = curQuadVm->pos.y;
                                worldMatrix.m[3][2] = curQuadVm->pos.z;

                                quadPos.Project(&projectSrc, &g_Supervisor.viewport,
                                                &g_Supervisor.projectionMatrix,
                                                &g_Supervisor.viewMatrix, &worldMatrix);

                                viewDir.x = g_Supervisor.viewMatrix.m[0][0];
                                viewDir.y = g_Supervisor.viewMatrix.m[0][1];
                                viewDir.z = g_Supervisor.viewMatrix.m[0][2];
                                viewDir.Normalize(&viewDir);

                                if (curQuad->size.x != 0.0f)
                                {
                                    var_98 = curQuad->size.x;
                                }
                                else
                                {
                                    var_98 = curQuadVm->sprite->widthPx;
                                }

                                worldMatrix.m[3][0] += viewDir.x * var_98 * curQuadVm->scale.x;
                                worldMatrix.m[3][1] += viewDir.y * var_98 * curQuadVm->scale.x;
                                worldMatrix.m[3][2] += viewDir.z * var_98 * curQuadVm->scale.x;

                                viewDir.Project(&projectSrc, &g_Supervisor.viewport,
                                                &g_Supervisor.projectionMatrix,
                                                &g_Supervisor.viewMatrix, &worldMatrix);

                                diffPos = viewDir - quadPos;

                                curQuadVm->scale.x = diffPos.Length() / var_98;
                                curQuadVm->scale.y = curQuadVm->scale.x;

                                diffPos = curQuadVm->pos - this->cam.pos;

                                origColor = curQuadVm->color;

                                var_98 = diffPos.Length();
                                if (this->skyFog.nearPlane < var_98)
                                {
                                    var_98 = (this->skyFog.nearPlane - var_98) /
                                             (this->skyFog.nearPlane - this->skyFog.farPlane);
                                    if (var_98 >= 1.0f)
                                    {
                                        goto skip_draw;
                                    }

                                    curQuadVm->color.bytes.b = curQuadVm->color.bytes.b -
                                                               (u8)((curQuadVm->color.bytes.b -
                                                                     this->skyFog.color.bytes.b) *
                                                                    var_98);
                                    curQuadVm->color.bytes.g = curQuadVm->color.bytes.g -
                                                               (u8)((curQuadVm->color.bytes.g -
                                                                     this->skyFog.color.bytes.g) *
                                                                    var_98);
                                    curQuadVm->color.bytes.r = curQuadVm->color.bytes.r -
                                                               (u8)((curQuadVm->color.bytes.r -
                                                                     this->skyFog.color.bytes.r) *
                                                                    var_98);
                                    curQuadVm->color.bytes.a =
                                        (u8)(curQuadVm->color.bytes.a * (1.0f - var_98));
                                }

                                curQuadVm->pos = quadPos;

                                if (curQuadVm->pos.z < 0.0f || curQuadVm->pos.z > 1.0f)
                                {
                                    // empty branch
                                }
                                else
                                {
                                    if (fogState != 0)
                                    {
                                        if (!g_Supervisor.cfg.disableFog)
                                        {
                                            g_Supervisor.DisableFog();
                                        }
                                        fogState = 0;
                                    }
                                    g_AnmManager->DrawFacingCamera(curQuadVm);
                                }
                                curQuadVm->color = origColor;
                            }
                            else
                            {
                                if (!g_Supervisor.cfg.disableFog && fogState != 1)
                                {
                                    if (!g_Supervisor.cfg.disableFog)
                                    {
                                        g_Supervisor.EnableFog();
                                    }
                                    fogState = 1;
                                }
                                g_AnmManager->Draw3(curQuadVm);
                            }
                            break;
                        }
                    skip_draw:
                        curQuad = (StdRawQuadBasic *)((u8 *)curQuad + curQuad->byteSize);
                    }
                }
            }
        }
        instance++;
    }
#if defined(TH07_PSP_PERF_DETAIL)
    Th07PspPerfAddStageTime(sceKernelGetSystemTimeWide() - pspStageStartUs);
#endif
    return 0;
}

void Stage::SetupCameraStageBackground()
{
    ZunVec3 eyeVec;
    ZunVec3 atVec;
    ZunVec3 upVec;
    f32 fov;
    f32 aspectRatio;
    f32 centerX;
    f32 centerY;
    f32 eyeZ;

    centerX = (f32)g_Supervisor.viewport.width / 2.0f;
    centerY = (f32)g_Supervisor.viewport.height / 2.0f;
    aspectRatio = (f32)g_Supervisor.viewport.width / (f32)g_Supervisor.viewport.height;
    fov = ZUN_PI / 10.0f;
    eyeZ = centerY / tanf(fov / 2.0f);

    upVec.x = 0.0f;
    upVec.y = -1.0f;
    upVec.z = 0.0f;

    atVec.x = centerX;
    atVec.y = centerY;
    atVec.z = 0.0f;

    eyeVec.x = centerX;
    eyeVec.y = centerY;
    eyeVec.z = eyeZ;

    g_Supervisor.viewMatrix.LookAtLH(&eyeVec, &atVec, &upVec);
    g_Supervisor.projectionMatrix.PerspectiveFovLH(fov, aspectRatio, 1.0f, 10000.0f);
    g_Supervisor.gfxDevice->SetTransformMatrix(MATRIX_VIEW, g_Supervisor.viewMatrix);
    g_Supervisor.gfxDevice->SetTransformMatrix(MATRIX_PROJECTION, g_Supervisor.projectionMatrix);
}

void Stage::UpdateCamera()
{
    ZunVec3 at = this->cam.lookAt + this->cam.pos;
    g_Supervisor.viewMatrix.LookAtLH(&this->cam.pos, &at, &this->cam.up);
    g_Supervisor.projectionMatrix.PerspectiveFovLH(
        this->cam.fov, (f32)g_Supervisor.viewport.width / (f32)g_Supervisor.viewport.height,
        30.0f, 1800.0f);
    g_Supervisor.gfxDevice->SetTransformMatrix(MATRIX_VIEW, g_Supervisor.viewMatrix);
    g_Supervisor.gfxDevice->SetTransformMatrix(MATRIX_PROJECTION, g_Supervisor.projectionMatrix);
    this->cam.right.Cross(&this->cam.lookAt, &this->cam.up);
    this->cam.right.Normalize(&this->cam.right);
}
