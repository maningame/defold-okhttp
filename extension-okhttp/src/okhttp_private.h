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
};

struct OkHttpCommandQueue
{
    dmArray<OkHttpCommand>  m_Commands;
    dmMutex::HMutex      m_Mutex;
};

typedef void (*OkHttpCommandFn)(OkHttpCommand* cmd, void* ctx);

void OkHttp_Queue_Create(OkHttpCommandQueue* queue);
void OkHttp_Queue_Destroy(OkHttpCommandQueue* queue);
// The command is copied by value into the queue
void OkHttp_Queue_Push(OkHttpCommandQueue* queue, OkHttpCommand* cmd);
void OkHttp_Queue_Flush(OkHttpCommandQueue* queue, OkHttpCommandFn fn, void* ctx);

#endif

#endif // DM_PLATFORM_ANDROID