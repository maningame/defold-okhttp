#if defined(DM_PLATFORM_ANDROID)

#include <dmsdk/sdk.h>

#include "okhttp_private.h"

static const uint32_t QUEUE_CAPACITY_STEP = 8;

static int32_t FindRequest(OkHttpCommandQueue* queue, uint64_t id)
{
    for(uint32_t i = 0; i != queue->m_Requests.Size(); ++i)
    {
        if (queue->m_Requests[i].m_Id == id)
        {
            return (int32_t)i;
        }
    }

    return -1;
}

// Expects queue->m_Mutex to be held
static dmScript::LuaCallbackInfo* TakeRequest(OkHttpCommandQueue* queue, uint64_t id)
{
    int32_t index = FindRequest(queue, id);

    if (index < 0)
    {
        return 0;
    }

    dmScript::LuaCallbackInfo* callback = queue->m_Requests[index].m_Callback;
    queue->m_Requests.EraseSwap(index);

    return callback;
}

void OkHttp_Queue_Create(OkHttpCommandQueue* queue)
{
    if (queue->m_Mutex == 0)
    {
        queue->m_Mutex = dmMutex::New();
    }

    DM_MUTEX_SCOPED_LOCK(queue->m_Mutex);

    queue->m_Commands.SetSize(0);
    queue->m_Requests.SetSize(0);
    queue->m_IsOpen = true;

    // m_NextRequestId is deliberately not reset: ids issued by a previous lua
    // state must never become resolvable again
}

void OkHttp_Queue_Destroy(OkHttpCommandQueue* queue, OkHttpCommandFn fn, void* ctx)
{
    assert(fn != 0);

    if (queue->m_Mutex == 0)
    {
        return;
    }

    dmArray<OkHttpCommand> commands;
    dmArray<OkHttpRequest> requests;
    {
        DM_MUTEX_SCOPED_LOCK(queue->m_Mutex);
        queue->m_IsOpen = false;
        commands.Swap(queue->m_Commands);
        requests.Swap(queue->m_Requests);
    }

    for(uint32_t i = 0; i != commands.Size(); ++i)
    {
        fn(&commands[i], ctx);
    }

    for(uint32_t i = 0; i != requests.Size(); ++i)
    {
        OkHttpCommand cmd;
        cmd.m_Command = OKHTTP_REQUEST_RESULT;
        cmd.m_Callback = requests[i].m_Callback;

        fn(&cmd, ctx);
    }
}

uint64_t OkHttp_Queue_AddRequest(OkHttpCommandQueue* queue, dmScript::LuaCallbackInfo* callback)
{
    if (queue->m_Mutex == 0)
    {
        return 0;
    }

    DM_MUTEX_SCOPED_LOCK(queue->m_Mutex);

    if (!queue->m_IsOpen)
    {
        return 0;
    }

    OkHttpRequest request;
    request.m_Id = ++queue->m_NextRequestId;
    request.m_Callback = callback;

    if(queue->m_Requests.Full())
    {
        queue->m_Requests.OffsetCapacity(QUEUE_CAPACITY_STEP);
    }
    queue->m_Requests.Push(request);

    return request.m_Id;
}

dmScript::LuaCallbackInfo* OkHttp_Queue_TakeRequest(OkHttpCommandQueue* queue, uint64_t id)
{
    if (queue->m_Mutex == 0)
    {
        return 0;
    }

    DM_MUTEX_SCOPED_LOCK(queue->m_Mutex);

    return TakeRequest(queue, id);
}

bool OkHttp_Queue_PushResult(OkHttpCommandQueue* queue, uint64_t id, OkHttpCommand* cmd)
{
    if (queue->m_Mutex == 0)
    {
        return false;
    }

    // Taking the callback and queueing the result happen under the same lock, so
    // a result can never end up owning a callback that nobody will release
    DM_MUTEX_SCOPED_LOCK(queue->m_Mutex);

    if (!queue->m_IsOpen)
    {
        return false;
    }

    dmScript::LuaCallbackInfo* callback = TakeRequest(queue, id);

    if (callback == 0)
    {
        return false;
    }

    cmd->m_Callback = callback;

    if(queue->m_Commands.Full())
    {
        queue->m_Commands.OffsetCapacity(QUEUE_CAPACITY_STEP);
    }
    queue->m_Commands.Push(*cmd);

    return true;
}

void OkHttp_Queue_Flush(OkHttpCommandQueue* queue, OkHttpCommandFn fn, void* ctx)
{
    assert(fn != 0);

    if (queue->m_Mutex == 0)
    {
        return;
    }

    dmArray<OkHttpCommand> tmp;
    {
        DM_MUTEX_SCOPED_LOCK(queue->m_Mutex);
        if (queue->m_Commands.Empty())
        {
            return;
        }
        tmp.Swap(queue->m_Commands);
    }

    for(uint32_t i = 0; i != tmp.Size(); ++i)
    {
        fn(&tmp[i], ctx);
    }
}

#endif // DM_PLATFORM_ANDROID
