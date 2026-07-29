#if defined(DM_PLATFORM_ANDROID)

#define LIB_NAME "OkHttp"
#define MODULE_NAME "okhttp"

#include <dmsdk/sdk.h>
#include <dmsdk/dlib/android.h>

#include "okhttp_private.h"

struct OkHttp
{
    OkHttp()
    : m_OkHttp(0)
    , m_HttpRequest(0)
    {
    }

    jobject m_OkHttp;

    jmethodID m_HttpRequest;

    OkHttpCommandQueue m_CommandQueue;
};

static OkHttp g_OkHttp;

static jobject LuaTableToHashMap(JNIEnv* env, lua_State* L, int index)
{
    jclass hashMapClass = env->FindClass("java/util/HashMap");
    jmethodID hashMapInit = env->GetMethodID(hashMapClass, "<init>", "()V");
    jobject hashMap = env->NewObject(hashMapClass, hashMapInit);

    jmethodID putMethod = env->GetMethodID(hashMapClass, "put",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

    env->DeleteLocalRef(hashMapClass);

    lua_pushnil(L);

    while (lua_next(L, index) != 0) {
        // lua_tostring returns NULL for non-string/non-number types; skip those entries
        const char* key = lua_tostring(L, -2);
        const char* value = lua_tostring(L, -1);

        if (key != NULL && value != NULL) {
            jstring jkey = env->NewStringUTF(key);
            jstring jvalue = env->NewStringUTF(value);

            env->CallObjectMethod(hashMap, putMethod, jkey, jvalue);

            env->DeleteLocalRef(jkey);
            env->DeleteLocalRef(jvalue);
        }

        lua_pop(L, 1);
    }

    return hashMap;
}

static const char* SafeString(void* str)
{
    return str != 0 ? (const char*)str : "";
}

static char* CopyJavaString(JNIEnv* env, jstring str)
{
    if (str == 0)
    {
        return strdup("");
    }

    const char* c_str = env->GetStringUTFChars(str, 0);

    if (c_str == 0)
    {
        return strdup("");
    }

    char* result = strdup(c_str);
    env->ReleaseStringUTFChars(str, c_str);

    return result;
}

static void FreeCommandData(OkHttpCommand* cmd)
{
    if (cmd->m_Data) {
        free(cmd->m_Data);
    }

    if (cmd->m_Url) {
        free(cmd->m_Url);
    }

    if (cmd->m_Headers) {
        free(cmd->m_Headers);
    }

    if (cmd->m_Response) {
        free(cmd->m_Response);
    }

    if (cmd->m_Error) {
        free(cmd->m_Error);
    }
}

static int OkHttp_Request(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);

    int top = lua_gettop(L);

    if (top < 3) {
        luaL_error(L, "request(url, method, callback, [headers], [body]) expected 3-5 arguments");
        return 0;
    }

    // URL
    if (lua_type(L, 1) != LUA_TSTRING) {
        return luaL_error(L, "Expected string for url, got %s", luaL_typename(L, 1));
    }

    // Method
    if (lua_type(L, 2) != LUA_TSTRING) {
        return luaL_error(L, "Expected string for method, got %s", luaL_typename(L, 2));
    }

    // Callback
    if (lua_type(L, 3) != LUA_TFUNCTION) {
        return luaL_error(L, "Expected function for callback, got %s", luaL_typename(L, 3));
    }

    if (g_OkHttp.m_OkHttp == 0)
    {
        return luaL_error(L, "The okhttp extension is not initialized");
    }

    dmAndroid::ThreadAttacher threadAttacher;
    JNIEnv* env = threadAttacher.GetEnv();

    const char* url = luaL_checkstring(L, 1);
    const char* method = luaL_checkstring(L, 2);

    jstring jurl = env->NewStringUTF(url);
    jstring jmethod = env->NewStringUTF(method);

    // Headers (optional)
    jobject jheaders = NULL;

    if (top >= 4 && !lua_isnil(L, 4) && lua_istable(L, 4)) {
        jheaders = LuaTableToHashMap(env, L, 4);
    }

    // Body (optional)
    jstring jbody = NULL;

    if (top >= 5 && !lua_isnil(L, 5) && lua_isstring(L, 5)) {
        jbody = env->NewStringUTF(luaL_checkstring(L, 5));
    }

    // Callback. The queue owns it and java only gets an id, so a duplicated or a
    // late result can never reach a freed callback
    dmScript::LuaCallbackInfo* callback = dmScript::CreateCallback(L, 3);
    uint64_t request_id = OkHttp_Queue_AddRequest(&g_OkHttp.m_CommandQueue, callback);

    if (request_id == 0)
    {
        dmScript::DestroyCallback(callback);

        env->DeleteLocalRef(jurl);
        env->DeleteLocalRef(jmethod);

        if (jheaders) env->DeleteLocalRef(jheaders);
        if (jbody) env->DeleteLocalRef(jbody);

        dmLogWarning("The okhttp queue is closed, the request was not sent: %s", url);

        return 0;
    }

    // Call native request
    env->CallVoidMethod(g_OkHttp.m_OkHttp, g_OkHttp.m_HttpRequest, jurl, jmethod, jheaders, jbody, (jlong)request_id);

    if (env->ExceptionCheck())
    {
        env->ExceptionDescribe();
        env->ExceptionClear();

        dmScript::LuaCallbackInfo* pending = OkHttp_Queue_TakeRequest(&g_OkHttp.m_CommandQueue, request_id);

        if (pending)
        {
            dmScript::DestroyCallback(pending);
        }

        dmLogError("Failed to send the request, the callback will not be called: %s", url);
    }

    // Cleanup
    env->DeleteLocalRef(jurl);
    env->DeleteLocalRef(jmethod);

    if (jheaders) env->DeleteLocalRef(jheaders);
    if (jbody) env->DeleteLocalRef(jbody);

    return 0;
}

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT void JNICALL Java_com_defold_okhttp_OkHttp_RequestCallback(JNIEnv* env, jclass, jstring url, jstring headers, jstring body, jint code, jstring error, jlong requestId)
{
    OkHttpCommand cmd;

    cmd.m_Command = OKHTTP_REQUEST_RESULT;
    cmd.m_ResponseCode = code;
    cmd.m_Url = CopyJavaString(env, url);
    cmd.m_Headers = CopyJavaString(env, headers);
    cmd.m_Response = CopyJavaString(env, body);
    cmd.m_Error = CopyJavaString(env, error);

    // The queue resolves the id into the stored callback. An unknown id means the
    // result was already delivered, or the lua state that owned the callback is
    // gone (sys.reboot, shutdown), so the result is dropped
    if (!OkHttp_Queue_PushResult(&g_OkHttp.m_CommandQueue, (uint64_t)requestId, &cmd))
    {
        dmLogWarning("Dropped a result of an unknown request: %s", SafeString(cmd.m_Url));
        FreeCommandData(&cmd);
    }
}

#ifdef __cplusplus
}
#endif

static void HandleRequestResult(const OkHttpCommand* cmd)
{
    if (cmd->m_Callback == 0)
    {
        dmLogWarning("Received request result but no listener was set!");
        return;
    }

    if (!dmScript::IsCallbackValid(cmd->m_Callback))
    {
        dmLogWarning("Received request result but the callback is no longer valid");
        dmScript::DestroyCallback(cmd->m_Callback);
        return;
    }

    lua_State* L = dmScript::GetCallbackLuaContext(cmd->m_Callback);
    int top = lua_gettop(L);

    if (!dmScript::SetupCallback(cmd->m_Callback))
    {
        dmLogWarning("Setup failed, stack now: %d", lua_gettop(L));
        dmScript::DestroyCallback(cmd->m_Callback);
        assert(top == lua_gettop(L));
        return;
    }

    lua_newtable(L);

    lua_pushstring(L, "url");
    lua_pushstring(L, SafeString(cmd->m_Url));
    lua_rawset(L, -3);

    lua_pushstring(L, "headers");

    const char* headers_json = SafeString(cmd->m_Headers);
    if (strlen(headers_json) > 0) {
        dmScript::JsonToLua(L, headers_json, strlen(headers_json));
    } else {
        lua_newtable(L);
    }

    lua_rawset(L, -3);

    lua_pushstring(L, "response");
    lua_pushstring(L, SafeString(cmd->m_Response));
    lua_rawset(L, -3);

    lua_pushstring(L, "error");
    lua_pushstring(L, SafeString(cmd->m_Error));
    lua_rawset(L, -3);

    lua_pushstring(L, "status");
    lua_pushnumber(L, cmd->m_ResponseCode);
    lua_rawset(L, -3);

    dmScript::PCall(L, 2, 0);

    dmScript::TeardownCallback(cmd->m_Callback);
    dmScript::DestroyCallback(cmd->m_Callback);

    assert(top == lua_gettop(L));
}

static const luaL_reg Module_methods[] =
{
    {"request", OkHttp_Request},
    {0, 0}
};

static void LuaInit(lua_State* L)
{
    int top = lua_gettop(L);

    luaL_register(L, MODULE_NAME, Module_methods);

    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

static void OkHttp_OnCommand(OkHttpCommand* cmd, void*)
{
    switch (cmd->m_Command)
    {
    case OKHTTP_REQUEST_RESULT:
        HandleRequestResult(cmd);
        break;

    default:
        assert(false);
    }

    FreeCommandData(cmd);
}

// Used while the lua state is still alive (extension finalize)
static void OkHttp_OnDiscard(OkHttpCommand* cmd, void*)
{
    if (cmd->m_Callback)
    {
        dmScript::DestroyCallback(cmd->m_Callback);
    }

    FreeCommandData(cmd);
}

// Used when the owning lua state is already gone (extension initialize after a
// previous instance died): the callback cannot be unregistered anymore, only the
// payload is released
static void OkHttp_OnDiscardStale(OkHttpCommand* cmd, void*)
{
    FreeCommandData(cmd);
}

static dmExtension::Result AppInitializeOkHttpExt(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result InitializeOkHttpExt(dmExtension::Params* params)
{
    // The queue lives for the whole process, so anything left over from a previous
    // lua state (sys.reboot, engine restart) is dropped before it can be flushed
    // into the new one
    OkHttp_Queue_Destroy(&g_OkHttp.m_CommandQueue, OkHttp_OnDiscardStale, 0);
    OkHttp_Queue_Create(&g_OkHttp.m_CommandQueue);

    dmAndroid::ThreadAttacher threadAttacher;
    JNIEnv* env = threadAttacher.GetEnv();

    jclass okhttp_class = dmAndroid::LoadClass(env, "com.defold.okhttp.OkHttp");

    if (okhttp_class == 0)
    {
        dmLogError("Failed to load the com.defold.okhttp.OkHttp class");
        return dmExtension::RESULT_INIT_ERROR;
    }

    g_OkHttp.m_HttpRequest = env->GetMethodID(okhttp_class, "HttpRequest", "(Ljava/lang/String;Ljava/lang/String;Ljava/util/Map;Ljava/lang/String;J)V");

    jmethodID jni_constructor = env->GetMethodID(okhttp_class, "<init>", "(JJIJZ)V");

    jint maxIdleConnections = dmConfigFile::GetInt(params->m_ConfigFile, "okhttp.max_idle_connections", 5);
    jlong keepAliveDuration = dmConfigFile::GetInt(params->m_ConfigFile, "okhttp.keep_alive_duration", 5);
    jlong connectTimeout = dmConfigFile::GetInt(params->m_ConfigFile, "okhttp.connect_timeout", 10);
    jlong readTimeout = dmConfigFile::GetInt(params->m_ConfigFile, "okhttp.read_timeout", 10);
    jboolean isLog = dmConfigFile::GetInt(params->m_ConfigFile, "okhttp.log", 1) == 1;

    g_OkHttp.m_OkHttp = env->NewGlobalRef(env->NewObject(okhttp_class, jni_constructor,
        readTimeout,
        connectTimeout,
        maxIdleConnections,
        keepAliveDuration,
        isLog
    ));

    LuaInit(params->m_L);
    dmLogInfo("Registered %s Extension", MODULE_NAME);

    return dmExtension::RESULT_OK;
}

static dmExtension::Result AppFinalizeOkHttpExt(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result FinalizeOkHttpExt(dmExtension::Params* params)
{
    // Closes the queue and releases every callback that will never be delivered,
    // while the lua state they belong to is still alive
    OkHttp_Queue_Destroy(&g_OkHttp.m_CommandQueue, OkHttp_OnDiscard, 0);

    dmAndroid::ThreadAttacher threadAttacher;
    JNIEnv* env = threadAttacher.GetEnv();

    if (g_OkHttp.m_OkHttp)
    {
        env->DeleteGlobalRef(g_OkHttp.m_OkHttp);
    }

    g_OkHttp.m_OkHttp = NULL;
    g_OkHttp.m_HttpRequest = NULL;

    return dmExtension::RESULT_OK;
}

static dmExtension::Result OnUpdateOkHttpExt(dmExtension::Params* params)
{
    OkHttp_Queue_Flush(&g_OkHttp.m_CommandQueue, OkHttp_OnCommand, 0);
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(OkHttpExt, LIB_NAME, AppInitializeOkHttpExt, AppFinalizeOkHttpExt, InitializeOkHttpExt, OnUpdateOkHttpExt, NULL, FinalizeOkHttpExt)

#endif //DM_PLATFORM_ANDROID
