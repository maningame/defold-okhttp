#if defined(DM_PLATFORM_ANDROID)

#ifndef OKHTTP_PRIVATE_H
#define OKHTTP_PRIVATE_H

#include <dmsdk/sdk.h>

enum EOkHttpCommand
{
    OKHTTP_REQUEST_RESULT,
};

struct DM_ALIGNED(16) OkHttpCommand
{
    OkHttpCommand()
    {
        memset(this, 0, sizeof(OkHttpCommand));
    }

    // Used for storing eventual callback info (if needed)
    dmScript::LuaCallbackInfo* m_Callback;

    // The actual command payload
    int32_t  	m_Command;
    int32_t  	m_ResponseCode;
    void*    	m_Data;
    void*    	m_Url;
    void*    	m_Headers;
    void*    	m_Response;
    void*    	m_Error;
};

// An in flight request. The queue owns the callback until the result is
// delivered, so java only ever holds an id and can never reach freed memory
struct OkHttpRequest
{
    uint64_t                   m_Id;
    dmScript::LuaCallbackInfo* m_Callback;
};

struct OkHttpCommandQueue
{
    OkHttpCommandQueue()
    : m_Mutex(0)
    , m_NextRequestId(0)
    , m_IsOpen(false)
    {
    }

    dmArray<OkHttpCommand>  m_Commands;
    dmArray<OkHttpRequest>  m_Requests;
    dmMutex::HMutex      m_Mutex;
    uint64_t                m_NextRequestId;
    bool                    m_IsOpen;
};

typedef void (*OkHttpCommandFn)(OkHttpCommand* cmd, void* ctx);

// Opens the queue. The mutex is created once and is never destroyed: java
// callbacks may still arrive after the extension has been finalized
void OkHttp_Queue_Create(OkHttpCommandQueue* queue);
// Closes the queue and hands every queued command and every in flight callback
// to fn, so the caller can release them while its lua state is still alive
void OkHttp_Queue_Destroy(OkHttpCommandQueue* queue, OkHttpCommandFn fn, void* ctx);
// Stores an in flight request and returns its id, 0 if the queue is closed
uint64_t OkHttp_Queue_AddRequest(OkHttpCommandQueue* queue, dmScript::LuaCallbackInfo* callback);
// Removes an in flight request and returns its callback, 0 if the id is unknown
dmScript::LuaCallbackInfo* OkHttp_Queue_TakeRequest(OkHttpCommandQueue* queue, uint64_t id);
// Resolves the request id and copies the command (by value) into the queue.
// Returns false if the id is unknown (already delivered, or the request belongs
// to a destroyed lua state) or if the queue is closed
bool OkHttp_Queue_PushResult(OkHttpCommandQueue* queue, uint64_t id, OkHttpCommand* cmd);
void OkHttp_Queue_Flush(OkHttpCommandQueue* queue, OkHttpCommandFn fn, void* ctx);

#endif

#endif // DM_PLATFORM_ANDROID
