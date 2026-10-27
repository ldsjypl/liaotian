package online.ldsjypl.chat;

import android.util.Log;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.WebSocket;
import okhttp3.WebSocketListener;

/**
 * WebSocket 客户端，基于 OkHttp（成熟实现，避免手写 TLS/帧在厂商 ROM 上的兼容问题）。
 * connect() 会阻塞直到握手完成，最多等 30 秒。
 */
public class WsClient {

    public interface Listener {
        void onOpen();

        void onMessage(String text);

        void onClosed(String reason);
    }

    private final String url;
    private final Listener listener;
    private final OkHttpClient http;
    private volatile WebSocket socket;
    private volatile boolean open = false;

    public WsClient(String host, int port, String path, Listener listener) {
        this.url = "wss://" + host + ":" + port + path;
        this.listener = listener;
        this.http = new OkHttpClient.Builder()
                .connectTimeout(15, TimeUnit.SECONDS)
                .readTimeout(0, TimeUnit.MILLISECONDS)   // 长连接，读不超时
                .pingInterval(30, TimeUnit.SECONDS)      // 自动心跳
                .retryOnConnectionFailure(true)
                .build();
    }

    public boolean isOpen() {
        return open;
    }

    /** 阻塞直到握手完成（或超时/失败）。 */
    public void connect() throws Exception {
        final CountDownLatch latch = new CountDownLatch(1);
        final String[] error = {null};

        Request req = new Request.Builder().url(url).build();
        socket = http.newWebSocket(req, new WebSocketListener() {
            @Override
            public void onOpen(WebSocket webSocket, Response response) {
                open = true;
                Log.i("liaotian", "ws open: " + response.code());
                latch.countDown();
                listener.onOpen();
            }

            @Override
            public void onMessage(WebSocket webSocket, String text) {
                listener.onMessage(text);
            }

            @Override
            public void onFailure(WebSocket webSocket, Throwable t, Response response) {
                open = false;
                error[0] = t == null ? "failure" : t.getMessage();
                Log.e("liaotian", "ws failure: " + error[0], t);
                latch.countDown();
                listener.onClosed(error[0]);
            }

            @Override
            public void onClosed(WebSocket webSocket, int code, String reason) {
                open = false;
                latch.countDown();
                listener.onClosed(reason);
            }
        });

        if (!latch.await(30, TimeUnit.SECONDS)) {
            throw new IllegalStateException("handshake timeout");
        }
        if (!open) {
            throw new IllegalStateException(error[0] == null ? "connect failed" : error[0]);
        }
    }

    public void send(String text) {
        WebSocket s = socket;
        if (s != null && open) {
            boolean queued = s.send(text);
            Log.i("liaotian", "frame queued=" + queued + " " + text);
        } else {
            Log.w("liaotian", "send skipped: not open");
        }
    }

    public void close() {
        open = false;
        WebSocket s = socket;
        if (s != null) {
            s.close(1000, "bye");
            s.cancel();
        }
        http.dispatcher().executorService().shutdown();
    }
}
