package com.defold.okhttp;

import android.util.Log;

import org.json.JSONObject;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Protocol;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import okhttp3.Headers;
import okhttp3.ConnectionPool;
import okhttp3.logging.HttpLoggingInterceptor;

import java.io.IOException;
import java.util.Arrays;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

class OkHttp {
    public static final String TAG = "extension_okhttp";

    // Передаём данные обратно в Дефолд
    public static native void RequestCallback(String url, String headers, String body, int code, String error, long requestId);

    // Ответ
    public static class HttpResponse {
        public int code = 0;
        public String body = "";
        public String headers = "";
        public String error = "";
    }

    private final OkHttpClient httpClient;

    private static String errorMessage(Throwable e) {
        String message = e.getMessage();

        return (message != null) ? message : e.getClass().getName();
    }

    // Результат запроса уходит в Дефолд ровно один раз: повторная доставка
    // обращалась бы к уже освобождённому коллбэку
    private static void deliver(HttpResponse result, String url, long requestId, AtomicBoolean isDelivered) {
        if (!isDelivered.compareAndSet(false, true)) {
            Log.w(TAG, "Duplicate result ignored: " + url);
            return;
        }

        RequestCallback(url, result.headers, result.body, result.code, result.error, requestId);
    }

    // Http-запрос
    public void HttpRequest(String url, String method, Map<String, String> headers, String body, final long requestId) {
        HttpResponse result = new HttpResponse();
        final AtomicBoolean isDelivered = new AtomicBoolean(false);
        Request.Builder requestBuilder = new Request.Builder()
            .url(url);

        if (headers != null) {
            for (Map.Entry<String, String> header : headers.entrySet()) {
                requestBuilder.addHeader(header.getKey(), header.getValue());
            }
        }

        if ("GET".equals(method)) {
            requestBuilder.get();
        } else {
            MediaType mediaType = MediaType.parse("application/x-www-form-urlencoded; charset=utf-8");

            if (body != null && !body.isEmpty()) {
                RequestBody requestBody = RequestBody.Companion.create(body, mediaType);
                requestBuilder.method(method, requestBody);
            } else {
                RequestBody emptyBody = RequestBody.Companion.create("", mediaType);
                requestBuilder.method(method, emptyBody);
            }
        }

        Request request = requestBuilder.build();

        httpClient.newCall(request).enqueue(new Callback() {
            @Override
            public void onFailure(Call call, IOException e) {
                Log.e(TAG, "HTTP request failed", e);
                result.error = errorMessage(e);
                deliver(result, url, requestId, isDelivered);
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                try {
                    result.body = (response.body() != null) ? response.body().string() : "";
                    result.code = response.code();

                    Headers responseHeaders = response.headers();
                    JSONObject headersJson = new JSONObject();

                    for (int i = 0; i < responseHeaders.size(); i++) {
                        headersJson.put(responseHeaders.name(i), responseHeaders.value(i));
                    }

                    result.headers = headersJson.toString();
                } catch (Exception e) {
                    Log.e(TAG, "HTTP response processing failed", e);
                    result.error = errorMessage(e);
                } finally {
                    deliver(result, url, requestId, isDelivered);
                    response.close();
                }
            }
        });
    }

    public OkHttp(long readTimeout,
                  long connectTimeout,
                  int maxIdleConnections,
                  long keepAliveDuration,
                  boolean isLog) {

        OkHttpClient.Builder builder = new OkHttpClient.Builder()
            // Arrays.asList, а не List.of: java.util.List.of есть только с API 30
            .protocols(Arrays.asList(Protocol.HTTP_2, Protocol.HTTP_1_1))
            .connectionPool(new ConnectionPool(maxIdleConnections,
                                             keepAliveDuration,
                                             TimeUnit.MINUTES))
            .connectTimeout(connectTimeout, TimeUnit.SECONDS)
            .readTimeout(readTimeout, TimeUnit.SECONDS);

        if (isLog) {
            HttpLoggingInterceptor loggingInterceptor = new HttpLoggingInterceptor();
            loggingInterceptor.setLevel(HttpLoggingInterceptor.Level.BODY);
            builder.addInterceptor(loggingInterceptor);
        }

        this.httpClient = builder.build();
    }
}
