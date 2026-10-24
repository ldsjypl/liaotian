# 设计文档 · 多房间聊天室（v2）

## 1. 目标与约束

| 项 | 内容 |
|---|---|
| 目标 | 任何人可开房、用房号邀请、房间空了自动消失的聊天室 |
| 语言 | C++17，零第三方依赖 |
| 硬件 | 悟空派 H3，4×A7 1GHz，**512MB 内存**，TF 卡，2.4G WiFi |
| 对外 | Cloudflare Tunnel → nginx(8082) → liaotian(8081) |
| 体量 | 50 间房、每间 30 人、总连接 128 |

## 2. 分层

```
main.cpp
  └── core/server       epoll 循环、连接生命周期、HTTP 路由
        ├── core/http       请求解析 / 响应 / 静态文件
        ├── net/websocket   RFC6455 握手与帧
        └── chat/hub        房间管理与 JSON 协议（通过 SendFn 回调发消息）
              ├── chat/room          成员、历史、空置计时
              └── storage/message_log 每房间一个 JSONL 日志
util/*  纯头文件：时间、字符串、JSON、SHA1、Base64
```

依赖方向严格单向：`core → chat → storage → util`，`chat` 不知道 socket 的存在。
这样协议逻辑可以脱离网络单独测试，换 IO 模型（比如换 libuv）也不用动业务代码。

## 3. 房间生命周期

```
create ──▶ Room(id, dir) ──▶ 打开 <id>.jsonl，载入历史
   │
   ├── 有成员：user_count > 0，empty_since = 0
   │
   └── 最后一人离开 ──▶ note_empty(now)
                          │
        每 5 秒 Hub::tick 扫描
                          │
        now - empty_since > ttl(120s) ──▶ destroy()：close + unlink 文件，从 map 移除
```

要点：

- **房号唯一性**：6 位十进制随机数，生成时检查 map 冲突，最多重试 200 次。
- **空置不是立刻删**：给 120 秒宽限，避免"刷新页面 / 网络闪断"把房间弄丢。
- **进程重启不销毁房间**：`shutdown()` 只关闭文件句柄（flush），文件保留；
  只有 reaper 会删文件。重启后房间按需从磁盘重新载入。
- **列表只显示有人在的房间**：空置等待回收的房间不对外可见。

## 4. 内存预算

| 项 | 估算 |
|---|---|
| 二进制 mmap 常驻 | ~200KB |
| 每连接（读写缓冲 + 解析器） | ~10KB |
| 每个房间（100 条历史 × ~120B + 成员表） | ~15KB |
| 50 房间 × 30 人满配 | 15KB×50 + 10KB×128 ≈ 2.0MB |

因此 `MemoryMax=96M` 是极宽松的上限，只用于兜底（比如某天出现消息风暴）。
真正的约束是文件描述符：50 个房间文件 + 128 个连接 ≈ 180，`LimitNOFILE=4096` 足够。

## 5. 协议

WebSocket 文本帧承载 JSON，一条连接可先后经历"大厅 → 房间 → 大厅"。

| 方向 | type | 字段 |
|---|---|---|
| C→S | `create` | `name` |
| C→S | `join` | `room`,`name` |
| C→S | `msg` | `text` |
| C→S | `leave` | — |
| C→S | `ping` | — |
| S→C | `joined` | `room,name,created,users,history` |
| S→C | `msg` | `room,id,from,text,ts` |
| S→C | `sys` | `room,text,ts` |
| S→C | `users` | `room,list` |
| S→C | `left` | — |
| S→C | `error` | `text` |

所有下行消息都带 `room`，前端据此过滤（切房时旧房间的迟到消息不会串台）。

## 6. 限制与防滥用

| 项 | 值 | 目的 |
|---|---|---|
| 连接数 | 128 | 保护 fd 和内存 |
| 房间数 | 50 | 保护内存与磁盘写入 |
| 单房人数 | 30 | 控制广播扇出 |
| 消息长度 | 2000 字节 | 防大包 |
| 发言频率 | 5 条 / 3 秒 / 连接 | 防刷屏 |
| 未入房发言 | 拒绝 | 防匿名广播 |
| 静态路径 | 白名单字符 + 禁 `..` | 防穿越 |
| 请求头 | 16KB 上限 | 防慢速攻击 |
| 空闲 | 60s ping / 180s 断开 | 回收死连接 |
| 进程内存 | 96MB 硬上限 | 兜底 |

## 7. 存储

`data/<房号>.jsonl`，一行一条：

```json
{"id":3,"from":"小林","text":"你好","ts":1761288000}
```

- 启动时读入内存环形缓冲（每房间 100 条），文件本身不截断；
- 每条 `write()` 追加，每 50 条或 10 秒 `fdatasync` 一次；
- 房间销毁时 `unlink`，不留下垃圾；
- 掉电最多丢最后半条，读取时跳过解析不完整的行。

## 7.5 管理通道

管理员功能走 **Unix domain socket**（`/run/liaotian/admin.sock`），由同一个 epoll
循环轮询，不占用网络端口、不经过 Cloudflare：

- 目录 `RuntimeDirectory=liaotian`（0700），socket 文件 `chmod 0600`，属主 `lin`
- 一次连接一条命令，服务端回文本后关闭，CLI 就是 `nc -U` 的一行包装
- `read` 直接读内存里的消息环形缓冲，不 join 房间，所以用户无感知
- `kick` / `close` 通过 `Hub` 返回待关闭的 fd，由 `Server::close_conn()` 统一收尾，
  避免在协议层直接 close 造成事件循环里的悬空引用

命令解析、HTTP 和聊天协议分属 `core/admin.cpp`、`core/http.cpp`、`chat/hub.cpp`，
三者互不包含对方的头文件。
## 8. 前端结构

单页两屏：`#lobby`（昵称 / 新建 / 房号加入 / 房间列表 / 使用说明）与 `#chat`。
同一 WebSocket 连接贯穿始终，切屏只切 DOM。断线重连后自动用原房号回房；
`localStorage` 记住昵称。所有消息用 `textContent` 渲染，避免 XSS。

## 9. 后续可扩展

- 房间可选名字 / 密码
- 私聊、@提醒、图片（需对象存储或分片上传）
- 消息已读、撤回
- 把 JSONL 换成 SQLite（需要 `libsqlite3-dev`，常驻 +500KB）
- 多实例部署（房间按 hash 分片，或换成 Redis pub/sub）
