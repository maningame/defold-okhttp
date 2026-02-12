#if defined(DM_PLATFORM_ANDROID)

#include <dmsdk/sdk.h>

#include "okhttp_private.h"

void OkHttp_Queue_Create(OkHttpCommandQueue* queue)
{
    queue->m_Mutex = dmMutex::New();
}

void OkHttp_Queue_Destroy(OkHttpCommandQueue* queue)
{
    dmMutex::Delete(queue->m_Mutex);
}

void OkHttp_Queue_Push(OkHttpCommandQueue* queue, OkHttpCommand* cmd)
{
    DM_MUTEX_SCOPED_LOCK(queue->m_Mutex);

    if(queue->m_Commands.Full())
    {
        queue->m_Commands.OffsetCapacity(2);
    }
    queue->m_Commands.Push(*cmd);
}

void OkHttp_Queue_Flush(OkHttpCommandQueue* queue, OkHttpCommandFn fn, void* ctx)
{
    assert(fn != 0);

    if (queue->m_Commands.Empty())
    {
        return;
    }

    dmArray<OkHttpCommand> tmp;
    {
        DM_MUTEX_SCOPED_LOCK(queue->m_Mutex);
        tmp.Swap(queue->m_Commands);
    }

    for(uint32_t i = 0; i != tmp.Size(); ++i)
    {
        fn(&tmp[i], ctx);
    }
}

#endif // DM_PLATFORM_ANDROID
