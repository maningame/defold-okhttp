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
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;

class OkHttp {
    public static final String TAG = "extension_okhttp";

    // Передаём данные обратно в Дефолд
    public static native void RequestCallback(String url, String headers, String body, int code, String error, long cmdHandle);

    // Ответ
    public static class HttpResponse {
        public int code = 0;
        public String body = "";
        public String headers = "";
        public String error = "";
    }

    private final OkHttpClient httpClient;

    // Http-запрос
    public void HttpRequest(String url, String method, Map<String, String> headers, String body, final long commandPtr) {
        HttpResponse result = new HttpResponse();
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
                result.error = e.getMessage();
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
                    result.error = e.getMessage();
                } finally {
                    RequestCallback(url, result.headers, result.body, result.code, result.error, commandPtr);
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
            .protocols(List.of(Protocol.HTTP_2, Protocol.HTTP_1_1))
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
