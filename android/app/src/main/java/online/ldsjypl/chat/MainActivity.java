package online.ldsjypl.chat;

import android.app.Activity;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class MainActivity extends Activity {

    private static final String HOST = "ldsjypl.online";
    private static final int PORT = 443;
    private static final String PATH = "/ws";

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ChatAdapter adapter = new ChatAdapter();
    private final SimpleDateFormat hhmm = new SimpleDateFormat("HH:mm", Locale.getDefault());

    /** 当前唯一的一条连接，任何时刻只允许存在一个。 */
    private volatile WsClient ws;
    private volatile boolean connecting = false;
    private volatile boolean manualClose = false;
    private volatile boolean joined = false;
    /** 连接代号：旧连接迟到的回调靠它丢弃。 */
    private volatile long connGen = 0;

    private int retry = 0;
    private Runnable pendingAction;
    private Runnable lastJoinAction;
    private Runnable joinRetryTask;
    private Runnable pingTask;
    private Runnable reconnectTask;
    private Runnable watchdogTask;

    private String myName = "";
    private String roomId = "";
    private String roomNick = "";

    private View lobbyRoot;
    private View chatRoot;
    private EditText nickInput;
    private EditText roomInput;
    private EditText inputBox;
    private TextView lobbyStatus;
    private TextView roomTitle;
    private TextView onlineText;
    private ListView list;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        lobbyRoot = findViewById(R.id.lobbyRoot);
        chatRoot = findViewById(R.id.chatRoot);
        nickInput = findViewById(R.id.nickInput);
        roomInput = findViewById(R.id.roomInput);
        inputBox = findViewById(R.id.inputBox);
        lobbyStatus = findViewById(R.id.lobbyStatus);
        roomTitle = findViewById(R.id.roomTitle);
        onlineText = findViewById(R.id.onlineText);
        list = findViewById(R.id.messageList);
        list.setAdapter(adapter);

        // Android 15 强制边到边：手动给状态栏和输入法让位
        final View rootView = findViewById(R.id.root);
        rootView.setOnApplyWindowInsetsListener((v, insets) -> {
            int top = insets.getSystemWindowInsetTop();
            int bottom = insets.getSystemWindowInsetBottom();
            v.setPadding(0, top, 0, 0);
            chatRoot.setPadding(0, 0, 0, bottom);
            return insets;
        });

        SharedPreferences sp = getSharedPreferences("lt", MODE_PRIVATE);
        nickInput.setText(sp.getString("name", ""));

        Button createBtn = findViewById(R.id.createBtn);
        Button joinBtn = findViewById(R.id.joinBtn);
        Button leaveBtn = findViewById(R.id.leaveBtn);
        Button sendBtn = findViewById(R.id.sendBtn);

        createBtn.setOnClickListener(v -> createRoom());
        joinBtn.setOnClickListener(v -> joinRoom());
        leaveBtn.setOnClickListener(v -> doLeave());
        sendBtn.setOnClickListener(v -> sendMessage());

        inputBox.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_SEND || actionId == EditorInfo.IME_ACTION_DONE) {
                sendMessage();
                return true;
            }
            return false;
        });
    }

    @Override
    public void onBackPressed() {
        if (chatRoot.getVisibility() == View.VISIBLE) {
            doLeave();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        manualClose = true;
        stopTimers();
        WsClient c = ws;
        ws = null;
        if (c != null) c.close();
        super.onDestroy();
    }

    // ------------------------------------------------------------ 大厅动作

    private String currentName() {
        String n = nickInput.getText().toString().trim();
        if (n.isEmpty()) {
            toast("先填个昵称");
            nickInput.requestFocus();
            return null;
        }
        if (n.length() > 24) n = n.substring(0, 24);
        myName = n;
        getSharedPreferences("lt", MODE_PRIVATE).edit().putString("name", n).apply();
        return n;
    }

    private void createRoom() {
        final String n = currentName();
        if (n == null) return;
        setLobbyStatus("正在连接服务器…（跨境线路可能要十几秒）");
        lastJoinAction = () -> {
            try {
                JSONObject o = new JSONObject();
                o.put("type", "create");
                o.put("name", n);
                sendJson(o);
            } catch (Exception ignored) {
            }
        };
        ensureConnected(lastJoinAction);
        armJoinRetry();
    }

    private void joinRoom() {
        final String n = currentName();
        if (n == null) return;
        final String r = roomInput.getText().toString().trim();
        if (!r.matches("\\d{6}")) {
            toast("房号是 6 位数字");
            roomInput.requestFocus();
            return;
        }
        setLobbyStatus("正在连接服务器…（跨境线路可能要十几秒）");
        lastJoinAction = () -> {
            try {
                JSONObject o = new JSONObject();
                o.put("type", "join");
                o.put("room", r);
                o.put("name", n);
                sendJson(o);
            } catch (Exception ignored) {
            }
        };
        ensureConnected(lastJoinAction);
        armJoinRetry();
    }

    private void doLeave() {
        if (joined) {
            try {
                JSONObject o = new JSONObject();
                o.put("type", "leave");
                sendJson(o);
            } catch (Exception ignored) {
            }
        }
        joined = false;
        roomId = "";
        cancelJoinRetry();
        showLobby();
    }

    private void sendMessage() {
        String text = inputBox.getText().toString().trim();
        if (text.isEmpty()) return;
        if (!joined) {
            toast("还没进房间");
            return;
        }
        try {
            JSONObject o = new JSONObject();
            o.put("type", "msg");
            o.put("text", text);
            if (sendJson(o)) inputBox.setText("");
        } catch (Exception ignored) {
        }
    }

    // ------------------------------------------------------------ 连接管理

    private void ensureConnected(Runnable action) {
        WsClient c = ws;
        if (c != null && c.isOpen()) {
            action.run();
            return;
        }
        pendingAction = action;
        connect();
    }

    /**
     * 只允许一条连接：
     * 1) 建连中直接返回，避免并发建第二条；
     * 2) 建新连接前先关闭旧的；
     * 3) 所有回调都用 client == ws 校验，旧连接迟到的消息直接丢弃
     *    （之前消息显示两遍就是因为没有这层校验）。
     */
    private void connect() {
        if (manualClose) return;
        if (connecting) return;
        connecting = true;
        final long gen = ++connGen;

        final WsClient old = ws;
        if (old != null) {
            ws = null;
            old.close();
        }

        final WsClient client = new WsClient(HOST, PORT, PATH, new WsClient.Listener() {
            @Override
            public void onOpen() {
                ui.post(() -> {
                    if (gen != connGen) return;
                    connecting = false;
                    cancelReconnect();
                    retry = 0;
                    setLobbyStatus("");
                    startPing();
                    Runnable a = pendingAction;
                    pendingAction = null;
                    if (a != null) {
                        ui.postDelayed(a, 400);
                    } else if (!roomId.isEmpty()) {
                        ui.postDelayed(() -> sendJoin(roomId, roomNick), 400);
                    }
                });
            }

            @Override
            public void onMessage(final String text) {
                ui.post(() -> {
                    if (gen != connGen) return;  // 旧连接的迟到消息
                    handleMessage(text);
                });
            }

            @Override
            public void onClosed(String reason) {
                ui.post(() -> {
                    if (gen != connGen) return;
                    connecting = false;
                    ws = null;
                    stopPing();
                    joined = false;
                    if (manualClose) return;
                    if (roomId.isEmpty()) {
                        setLobbyStatus("连不上服务器，正在重试…");
                    } else {
                        appendSys("连接断开，正在重连…");
                    }
                    scheduleReconnect();
                });
            }
        });

        ws = client;
        watchdogTask = () -> {
            if (client == ws && !client.isOpen()) {
                connecting = false;
                setLobbyStatus("连接超时，正在重试…");
                client.close();
            }
        };
        ui.postDelayed(watchdogTask, 40000);

        new Thread(() -> {
            try {
                client.connect();
            } catch (Exception e) {
                ui.post(() -> {
                    if (gen != connGen) return;
                    connecting = false;
                    ws = null;
                    Log.e("liaotian", "connect failed: " + e.getMessage());
                    setLobbyStatus("连接失败：" + (e.getMessage() == null ? "未知错误" : e.getMessage()));
                    scheduleReconnect();
                });
            }
        }, "ws-connect").start();
    }

    private void scheduleReconnect() {
        cancelReconnect();
        int delay = Math.min(1000 * (1 << Math.min(retry, 4)), 15000);
        retry++;
        reconnectTask = this::connect;
        ui.postDelayed(reconnectTask, delay);
    }

    private void cancelReconnect() {
        if (reconnectTask != null) {
            ui.removeCallbacks(reconnectTask);
            reconnectTask = null;
        }
    }

    private void sendJoin(String room, String name) {
        try {
            JSONObject o = new JSONObject();
            o.put("type", "join");
            o.put("room", room);
            o.put("name", name);
            sendJson(o);
        } catch (Exception ignored) {
        }
    }

    private boolean sendJson(JSONObject o) {
        WsClient c = ws;
        if (c == null || !c.isOpen()) {
            toast("还没连上，稍等一下");
            return false;
        }
        c.send(o.toString());
        return true;
    }

    private void armJoinRetry() {
        cancelJoinRetry();
        joinRetryTask = new Runnable() {
            @Override
            public void run() {
                if (!joined && lastJoinAction != null) {
                    Log.i("liaotian", "join not acked, retrying");
                    lastJoinAction.run();
                    ui.postDelayed(this, 6000);
                }
            }
        };
        ui.postDelayed(joinRetryTask, 6000);
    }

    private void cancelJoinRetry() {
        if (joinRetryTask != null) {
            ui.removeCallbacks(joinRetryTask);
            joinRetryTask = null;
        }
    }

    private void startPing() {
        stopPing();
        pingTask = new Runnable() {
            @Override
            public void run() {
                try {
                    JSONObject o = new JSONObject();
                    o.put("type", "ping");
                    sendJson(o);
                } catch (Exception ignored) {
                }
                ui.postDelayed(this, 45000);
            }
        };
        ui.postDelayed(pingTask, 45000);
    }

    private void stopPing() {
        if (pingTask != null) {
            ui.removeCallbacks(pingTask);
            pingTask = null;
        }
    }

    private void stopTimers() {
        stopPing();
        cancelJoinRetry();
        cancelReconnect();
        if (watchdogTask != null) {
            ui.removeCallbacks(watchdogTask);
            watchdogTask = null;
        }
    }

    // ------------------------------------------------------------ 消息处理

    private void handleMessage(String text) {
        try {
            JSONObject o = new JSONObject(text);
            String type = o.optString("type");

            if ("joined".equals(type)) {
                roomId = o.optString("room");
                roomNick = o.optString("name");
                joined = true;
                cancelJoinRetry();
                adapter.clear();
                showChat(roomId);
                JSONArray users = o.optJSONArray("users");
                updateOnline(users != null ? users.length() : 1);
                JSONArray history = o.optJSONArray("history");
                if (history != null) {
                    for (int i = 0; i < history.length(); i++) {
                        JSONObject m = history.getJSONObject(i);
                        addMessage(m.optString("from"), m.optString("text"), m.optLong("ts"));
                    }
                }
                if (o.optBoolean("created", false)) {
                    appendSys("房间已创建，房号 " + roomId + "。房号不公开，把数字发给朋友即可");
                } else {
                    appendSys("已进入 " + roomId);
                }
                scrollBottom();
            } else if ("msg".equals(type)) {
                if (roomId.equals(o.optString("room"))) {
                    addMessage(o.optString("from"), o.optString("text"), o.optLong("ts"));
                    scrollBottom();
                }
            } else if ("sys".equals(type)) {
                if (roomId.equals(o.optString("room"))) {
                    appendSys(o.optString("text"));
                    scrollBottom();
                }
            } else if ("users".equals(type)) {
                if (roomId.equals(o.optString("room"))) {
                    JSONArray list = o.optJSONArray("list");
                    updateOnline(list != null ? list.length() : 0);
                }
            } else if ("left".equals(type)) {
                joined = false;
                roomId = "";
                showLobby();
                toast("已离开房间");
            } else if ("closed".equals(type)) {
                joined = false;
                roomId = "";
                showLobby();
                toast("房间已被管理员关闭");
            } else if ("error".equals(type)) {
                String msg = o.optString("text");
                setLobbyStatus(msg);
                toast(msg);
                if (msg != null && (msg.contains("不存在") || msg.contains("已满")
                        || msg.contains("移出") || msg.contains("上限"))) {
                    joined = false;
                    roomId = "";
                    showLobby();
                }
            }
        } catch (Exception ignored) {
        }
    }

    private void addMessage(String from, String msg, long ts) {
        ChatAdapter.Item it = new ChatAdapter.Item();
        boolean me = from != null && from.equals(roomNick);
        it.type = me ? ChatAdapter.TYPE_ME : ChatAdapter.TYPE_THEM;
        String time = hhmm.format(new Date(ts > 0 ? ts * 1000 : System.currentTimeMillis()));
        it.head = (me ? "我" : from) + " · " + time;
        it.body = msg;
        adapter.add(it);
    }

    private void appendSys(String text) {
        ChatAdapter.Item it = new ChatAdapter.Item();
        it.type = ChatAdapter.TYPE_SYS;
        it.body = text;
        adapter.add(it);
    }

    private void updateOnline(int n) {
        onlineText.setText("在线 " + n);
    }

    private void scrollBottom() {
        if (adapter.getCount() > 0) list.setSelection(adapter.getCount() - 1);
    }

    // ------------------------------------------------------------ 视图切换

    private void showChat(String room) {
        lobbyRoot.setVisibility(View.GONE);
        chatRoot.setVisibility(View.VISIBLE);
        roomTitle.setText("聊天室 " + room);
        inputBox.requestFocus();
    }

    private void showLobby() {
        chatRoot.setVisibility(View.GONE);
        lobbyRoot.setVisibility(View.VISIBLE);
        setLobbyStatus("");
    }

    private void setLobbyStatus(String text) {
        if (lobbyStatus != null) lobbyStatus.setText(text == null ? "" : text);
    }

    private void toast(String text) {
        Toast.makeText(this, text, Toast.LENGTH_SHORT).show();
    }
}
