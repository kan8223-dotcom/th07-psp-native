#include "Chain.hpp"

#include <new>

#include <stddef.h>

#if defined(TH07_PSP_PERF_DETAIL)
#include <pspkernel.h>

#include "graphics/PspGuGraphics.hpp"

namespace
{
class PspChainPerfScope
{
  public:
    explicit PspChainPerfScope(bool draw)
        : mStartUs(0), mCursorUs(0), mDraw(draw)
    {
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        if (!gTh07PspPerfDenseSliceActive)
        {
            return;
        }
#endif
        mStartUs = sceKernelGetSystemTimeWide();
        mCursorUs = mStartUs;
        mActive = true;
    }
    ~PspChainPerfScope()
    {
        if (!mActive)
        {
            return;
        }
        const unsigned long long endUs = sceKernelGetSystemTimeWide();
#if defined(TH07_PSP_PERF_ATTRIB) || defined(TH07_PSP_PERF_EMPTY_TIMERS)
        if (mDraw)
        {
            Th07PspPerfAddDrawChainOverheadTime(mOverheadUs + endUs - mCursorUs);
        }
#endif
        const unsigned long long elapsed = endUs - mStartUs;
        if (mDraw)
        {
            Th07PspPerfAddDrawTime(elapsed);
        }
        else
        {
            Th07PspPerfAddCalcTime(elapsed);
        }
    }

#if defined(TH07_PSP_PERF_ATTRIB) || defined(TH07_PSP_PERF_EMPTY_TIMERS)
    unsigned long long BeginDrawJob()
    {
        const unsigned long long startUs = sceKernelGetSystemTimeWide();
        mOverheadUs += startUs - mCursorUs;
        return startUs;
    }

    void EndDrawJob(int priority, ChainCallback callback, unsigned long long startUs)
    {
        const unsigned long long endUs = sceKernelGetSystemTimeWide();
        Th07PspPerfAddDrawJobTime(priority, reinterpret_cast<unsigned long>(callback),
                                 endUs - startUs);
        mCursorUs = endUs;
    }
#endif

  private:
    unsigned long long mStartUs;
    unsigned long long mCursorUs;
#if defined(TH07_PSP_PERF_ATTRIB) || defined(TH07_PSP_PERF_EMPTY_TIMERS)
    unsigned long long mOverheadUs = 0;
#endif
    bool mDraw;
    bool mActive = false;
};
} // namespace
#endif

Chain g_Chain;

Chain::~Chain()
{
}

ChainElem::ChainElem()
{
    this->prev = NULL;
    this->next = NULL;
    this->callback = NULL;
    this->unkPtr = this;
    this->addedCallback = NULL;
    this->deletedCallback = NULL;
    this->priority = 0;
    this->isAllocated = 0;
}

ChainElem::~ChainElem()
{
    if (this->deletedCallback)
    {
        (*this->deletedCallback)(this->arg);
    }
    this->prev = NULL;
    this->next = NULL;
    this->callback = NULL;
    this->addedCallback = NULL;
    this->deletedCallback = NULL;
}

Chain::Chain()
{
}

ZunResult Chain::AddToCalcChain(ChainElem *elem, i32 priority)
{
    ZunResult uVar1;
    ChainElem *curElem;

    curElem = &this->calcChain;
    elem->priority = priority;
    while (curElem->next)
    {
        if (curElem->priority > priority)
        {
            break;
        }
        curElem = curElem->next;
    }
    if (curElem->priority > priority)
    {
        elem->next = curElem;
        elem->prev = curElem->prev;
        if (elem->prev)
        {
            elem->prev->next = elem;
        }
        curElem->prev = elem;
    }
    else
    {
        elem->next = NULL;
        elem->prev = curElem;
        curElem->next = elem;
    }
    if (elem->addedCallback)
    {
        uVar1 = elem->addedCallback(elem->arg);
        elem->addedCallback = NULL;
        return uVar1;
    }
    else
    {
        return ZUN_SUCCESS;
    }
}

ZunResult Chain::AddToDrawChain(ChainElem *elem, i32 priority)
{
    ChainElem *curElem;

    curElem = &this->drawChain;
    elem->priority = priority;
    while (curElem->next)
    {
        if (curElem->priority > priority)
        {
            break;
        }
        curElem = curElem->next;
    }
    if (curElem->priority > priority)
    {
        elem->next = curElem;
        elem->prev = curElem->prev;
        if (elem->prev)
        {
            elem->prev->next = elem;
        }
        curElem->prev = elem;
    }
    else
    {
        elem->next = NULL;
        elem->prev = curElem;
        curElem->next = elem;
    }
    if (elem->addedCallback)
    {
        return elem->addedCallback(elem->arg);
    }
    else
    {
        return ZUN_SUCCESS;
    }
}

i32 Chain::RunCalcChain()
{
#if defined(TH07_PSP_PERF_DETAIL)
    PspChainPerfScope perfScope(false);
#endif
    ChainElem *next;
    ChainElem *current;
    i32 updateCount;
    i32 callbackResult;

restart_from_first_job:
    updateCount = 0;
    current = &this->calcChain;
    while (current)
    {
        if (current->callback)
        {
        execute_again:
#if defined(TH07_PSP_PERF_ATTRIB) || defined(TH07_PSP_PERF_EMPTY_TIMERS)
            const bool measureJob = current->priority == 3 || current->priority == 7 ||
                                    current->priority == 8 || current->priority == 10 ||
                                    current->priority == 11 || current->priority == 12;
            const unsigned long long jobStartUs =
                measureJob ? sceKernelGetSystemTimeWide() : 0;
#endif
            callbackResult = current->callback(current->arg);
#if defined(TH07_PSP_PERF_ATTRIB) || defined(TH07_PSP_PERF_EMPTY_TIMERS)
            if (measureJob)
            {
                Th07PspPerfAddCalcJobTime(current->priority,
                                         sceKernelGetSystemTimeWide() - jobStartUs);
            }
#endif
            switch (callbackResult)
            {
            case CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB:
                next = current;
                current = current->next;
                Cut(next);
                updateCount++;
                continue;
            case CHAIN_CALLBACK_RESULT_EXECUTE_AGAIN:
                goto execute_again;
            case CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS:
                return 0;
            case CHAIN_CALLBACK_RESULT_BREAK:
                return 1;
            case CHAIN_CALLBACK_RESULT_EXIT_GAME_ERROR:
                return -1;
            case CHAIN_CALLBACK_RESULT_RESTART_FROM_FIRST_JOB:
                goto restart_from_first_job;
            default:
                break;
            }
            updateCount++;
        }
        current = current->next;
    }
    return updateCount;
}

i32 Chain::RunDrawChain()
{
#if defined(TH07_PSP_PERF_DETAIL)
    PspChainPerfScope perfScope(true);
#endif
    ChainElem *next;
    ChainElem *current;
    i32 updateCount;

    updateCount = 0;
    current = &this->drawChain;
    while (current)
    {
        if (current->callback)
        {
        execute_again:
#if defined(TH07_PSP_PERF_ATTRIB) || defined(TH07_PSP_PERF_EMPTY_TIMERS)
            const unsigned long long jobStartUs = perfScope.BeginDrawJob();
#endif
#if defined(TH07_PSP_PERF_M2) || defined(TH07_PSP_PERF_M3)
            Th07PspPerfSetDrawOwner(current->priority,
                                    reinterpret_cast<unsigned long>(current->callback));
#endif
            const i32 callbackResult = current->callback(current->arg);
#if defined(TH07_PSP_PERF_M2) || defined(TH07_PSP_PERF_M3)
            Th07PspPerfSetDrawOwner(-1, 0);
#endif
#if defined(TH07_PSP_PERF_ATTRIB) || defined(TH07_PSP_PERF_EMPTY_TIMERS)
            perfScope.EndDrawJob(current->priority, current->callback, jobStartUs);
#endif
#if defined(TH07_PSP_PERF_ATTRIB) || defined(TH07_PSP_PERF_EMPTY_TIMERS)
#if defined(TH07_PSP_PERF_GPU_ATTRIB)
            // GPU attribution: drain the GE after each draw job so the sync
            // tail lands on the priority that submitted the work.  Serializes
            // the pipeline; enable only for attribution runs, never for FPS
            // comparisons.
            Th07PspPerfPhaseGpuSync(current->priority);
#endif
#endif
            switch (callbackResult)
            {
            case CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB:
                next = current;
                current = current->next;
                Cut(next);
                updateCount++;
                continue;
            case CHAIN_CALLBACK_RESULT_EXECUTE_AGAIN:
                goto execute_again;
            case CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS:
                return 0;
            case CHAIN_CALLBACK_RESULT_BREAK:
                return 1;
            case CHAIN_CALLBACK_RESULT_EXIT_GAME_ERROR:
                return -1;
            default:
                break;
            }
            updateCount++;
        }
        current = current->next;
    }
    return updateCount;
}

void Chain::ReleaseSingleChain(ChainElem *root)
{
    ChainElem nextRootElem;
    ChainElem *tmp2;
    ChainElem *tmp;
    ChainElem *curElem;

    tmp = new ChainElem;
    nextRootElem.next = tmp;
    curElem = root;
    while (curElem)
    {
        tmp->unkPtr = curElem;
        tmp->next = new ChainElem;
        tmp = tmp->next;
        curElem = curElem->next;
    }
    curElem = &nextRootElem;
    while (curElem)
    {
        Cut(curElem->unkPtr);
        curElem = curElem->next;
    }
    tmp = nextRootElem.next;
    while (tmp)
    {
        tmp2 = tmp->next;
        delete tmp;
        tmp = NULL;
        tmp = tmp2;
    }
}

void Chain::Release()
{
    ReleaseSingleChain(&this->calcChain);
    ReleaseSingleChain(&this->drawChain);
}

ChainElem *Chain::CreateElem(ChainCallback callback)
{
    ChainElem *elem = new (std::nothrow) ChainElem;
    if (!elem)
    {
        return NULL;
    }
    elem->callback = callback;
    elem->addedCallback = NULL;
    elem->deletedCallback = NULL;
    elem->isAllocated = 1;
    return elem;
}

void Chain::Cut(ChainElem *toRemove)
{
    ChainElem *curElem;

    if (!toRemove)
    {
        return;
    }

    curElem = &this->calcChain;
    while (curElem)
    {
        if (curElem == toRemove)
        {
            goto destroy_elem;
        }
        curElem = curElem->next;
    }
    curElem = &this->drawChain;
    while (curElem)
    {
        if (curElem == toRemove)
        {
            goto destroy_elem;
        }
        curElem = curElem->next;
    }

    return;

destroy_elem:
    if (toRemove->prev)
    {
        toRemove->callback = NULL;
        toRemove->prev->next = toRemove->next;
        if (toRemove->next)
        {
            toRemove->next->prev = toRemove->prev;
        }
        toRemove->prev = NULL;
        toRemove->next = NULL;

        if (toRemove->isAllocated)
        {
            delete toRemove;
            toRemove = NULL;
        }
        else
        {
            if (toRemove->deletedCallback)
            {
                ChainLifecycleCallback deletedCallback = toRemove->deletedCallback;
                toRemove->deletedCallback = NULL;
                deletedCallback(toRemove->arg);
            }
        }
    }
}
