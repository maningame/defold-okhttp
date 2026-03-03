#if defined(DM_PLATFORM_ANDROID)

#define LIB_NAME "OkHttp"
#define MODULE_NAME "okhttp"

#include <dmsdk/sdk.h>
#include <dmsdk/dlib/android.h>

#include "okhttp_private.h"

struct OkHttp
{
    OkHttp()
    {
        memset(this, 0, sizeof(*this));
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

    jclass mapClass = env->FindClass("java/util/Map");
    jmethodID putMethod = env->GetMethodID(hashMapClass, "put",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

    lua_pushnil(L);

    while (lua_next(L, index) != 0) {
        jstring jkey = env->NewStringUTF(lua_tostring(L, -2));
        jstring jvalue = env->NewStringUTF(lua_tostring(L, -1));

        env->CallObjectMethod(hashMap, putMethod, jkey, jvalue);

        env->DeleteLocalRef(jkey);
        env->DeleteLocalRef(jvalue);
        lua_pop(L, 1);
    }

    return hashMap;
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

    // Callback
    OkHttpCommand* cmd = new OkHttpCommand;
    cmd->m_Callback = dmScript::CreateCallback(L, 3);
    cmd->m_Command = OKHTTP_REQUEST_RESULT;

    // Call native request
    env->CallVoidMethod(g_OkHttp.m_OkHttp, g_OkHttp.m_HttpRequest, jurl, jmethod, jheaders, jbody, (jlong)cmd);

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

JNIEXPORT void JNICALL Java_com_defold_okhttp_OkHttp_RequestCallback(JNIEnv* env, jobject, jstring url, jstring headers, jstring body, jint code, jstring error, jlong cmdHandle)
{
    const char* c_url = env->GetStringUTFChars(url, 0);
    const char* c_headers = env->GetStringUTFChars(headers, 0);
    const char* c_body = env->GetStringUTFChars(body, 0);
    const char* c_error = env->GetStringUTFChars(error, 0);

    OkHttpCommand* cmd = (OkHttpCommand*)cmdHandle;

    cmd->m_ResponseCode = code;
    cmd->m_Url = strdup(c_url);
    cmd->m_Headers = strdup(c_headers);
    cmd->m_Response = strdup(c_body);
    cmd->m_Error = strdup(c_error);

    env->ReleaseStringUTFChars(url, c_url);
    env->ReleaseStringUTFChars(headers, c_headers);
    env->ReleaseStringUTFChars(body, c_body);
    env->ReleaseStringUTFChars(error, c_error);

    OkHttp_Queue_Push(&g_OkHttp.m_CommandQueue, cmd);
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

    lua_State* L = dmScript::GetCallbackLuaContext(cmd->m_Callback);
    int top = lua_gettop(L);

    if (!dmScript::SetupCallback(cmd->m_Callback))
    {
        dmLogWarning("Setup failed, stack now: %d", lua_gettop(L));
        assert(top == lua_gettop(L));
        return;
    }

    lua_newtable(L);

    lua_pushstring(L, "url");
    lua_pushstring(L, (const char*)cmd->m_Url);
    lua_rawset(L, -3);

    lua_pushstring(L, "headers");

    const char* headers_json = (const char*)cmd->m_Headers;
    if (strlen(headers_json) > 0) {
        dmScript::JsonToLua(L, headers_json, strlen(headers_json));
    } else {
        lua_newtable(L);
    }

    lua_rawset(L, -3);

    lua_pushstring(L, "response");
    lua_pushstring(L, (const char*)cmd->m_Response);
    lua_rawset(L, -3);

    lua_pushstring(L, "error");
    lua_pushstring(L, (const char*)cmd->m_Error);
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
}

static dmExtension::Result AppInitializeOkHttpExt(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result InitializeOkHttpExt(dmExtension::Params* params)
{
    OkHttp_Queue_Create(&g_OkHttp.m_CommandQueue);

    dmAndroid::ThreadAttacher threadAttacher;
    JNIEnv* env = threadAttacher.GetEnv();

    jclass okhttp_class = dmAndroid::LoadClass(env, "com.defold.okhttp.OkHttp");

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
    OkHttp_Queue_Destroy(&g_OkHttp.m_CommandQueue);

    dmAndroid::ThreadAttacher threadAttacher;
    JNIEnv* env = threadAttacher.GetEnv();

    env->DeleteGlobalRef(g_OkHttp.m_OkHttp);

    g_OkHttp.m_OkHttp = NULL;

    return dmExtension::RESULT_OK;
}

static dmExtension::Result OnUpdateOkHttpExt(dmExtension::Params* params)
{
    OkHttp_Queue_Flush(&g_OkHttp.m_CommandQueue, OkHttp_OnCommand, 0);
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(OkHttpExt, LIB_NAME, AppInitializeOkHttpExt, AppFinalizeOkHttpExt, InitializeOkHttpExt, OnUpdateOkHttpExt, NULL, FinalizeOkHttpExt)

#endif //DM_PLATFORM_ANDROID
