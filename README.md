# liaotian · 多房间聊天室

跑在悟空派 H3（512MB 内存）上的自建聊天服务，C++17 单二进制、零第三方依赖，
对外通过 Cloudflare Tunnel 提供 `https://ldsjypl.online`。

**房间模型**：任何人可以开一间房，系统分配一个随机 6 位房号；把房号发给朋友就能进。
房间空了 2 分钟后自动解散，聊天记录一并删除 —— 不占内存、不占磁盘、不留痕迹。

## 功能

| 功能 | 说明 |
|---|---|
| 新建房间 | 服务端随机分配 6 位房号，自动去重，当前上限 50 间 |
| 房间私密 | 房号不公开、无列表接口，只有拿到房号的人才能进 |
| 加入房间 | 输入房号即可，单房间上限 30 人 |
| 昵称 | 1~24 字节，同一房间内重名自动加 `#2` |
| 历史消息 | 进房补最近 100 条；房间销毁时清除 |
| 自动回收 | 房间空置 120 秒后销毁，删除 `<房号>.jsonl` |
| 限速 | 每连接 5 条 / 3 秒 |
| 心跳重连 | 服务端 60s ping，前端指数退避重连并自动回房 |

## 资源控制（512MB 板子的关键）

| 限制 | 默认值 | 参数 |
|---|---|---|
| 同时连接数 | 128 | `--max-conns` |
| 房间总数 | 50 | `--max-rooms` |
| 单房间人数 | 30 | `--max-users` |
| 房间空置回收 | 120 秒 | `--room-ttl` |
| 每房间保留历史 | 100 条（内存） | 代码内 `hub_` 构造参数 |
| 进程内存硬上限 | 96MB（systemd `MemoryMax`） | `deploy/liaotian.service` |

实测常驻内存 **约 225KB**（0 房间 0 用户时），50 间房各 100 条历史预估 < 3MB。

## 目录结构

```
liaotian/
├── Makefile
├── README.md
├── docs/design.md
├── deploy/                      systemd / nginx / cloudflared / 看门狗脚本
├── tools/wstest.py              单文件 websocket 冒烟客户端
├── web/                         前端（原生 JS，无构建）
│   ├── index.html               大厅 + 聊天两个界面
│   ├── app.js
│   └── style.css
└── src/
    ├── main.cpp                 参数解析、启动
    ├── util/                    无依赖小工具
    │   ├── time.h  text.h  json.h  sha1.h  base64.h
    ├── storage/
    │   └── message_log.h/.cpp   单房间的 JSONL 追加日志 + 内存环形缓冲
    ├── net/
    │   └── websocket.h/.cpp     RFC6455 握手与帧编解码
    ├── chat/
    │   ├── room.h/.cpp          房间：成员、历史、空置计时
    │   └── hub.h/.cpp           房间管理 + JSON 协议（不碰 socket）
    └── core/
        ├── http.h/.cpp          请求解析、响应、静态文件
        └── server.h/.cpp        epoll 事件循环、连接生命周期
```

分层原则：`core` 只懂 socket 和 HTTP，`chat` 只懂房间和协议，两者通过
`Hub::SendFn` 回调连接；`storage` 和 `util` 不依赖任何上层。

## 编译

```bash
make -j4          # 产出 bin/liaotian，约 106KB
```

## 参数

```
--port N          监听端口，默认 8081
--bind ADDR       绑定地址，默认 127.0.0.1
--web DIR         静态目录，默认 ./web
--data DIR        数据目录，默认 ./data（每个房间一个 <房号>.jsonl）
--room-ttl SEC    空房间保留秒数，默认 120
--max-rooms N     房间上限，默认 50
--max-users N     单房间人数上限，默认 30
--max-conns N     连接上限，默认 128
--key CODE        可选邀请码（/ws?k=CODE），默认开放
```

## 协议

客户端 → 服务端：

```json
{"type":"create","name":"小林"}
{"type":"join","room":"482913","name":"小林"}
{"type":"msg","text":"你好"}
{"type":"leave"}
{"type":"ping"}
```

服务端 → 客户端：

```json
{"type":"joined","room":"482913","name":"小林","created":true,"users":["小林"],"history":[]}
{"type":"msg","room":"482913","id":7,"from":"小林","text":"你好","ts":1761288000}
{"type":"sys","room":"482913","text":"小红 加入了聊天","ts":1761288000}
{"type":"users","room":"482913","list":["小林","小红"]}
{"type":"left"}
{"type":"error","text":"房间不存在，可能已经解散"}
```

## HTTP 接口

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/` | 页面 |
| GET | `/api/health` | `{"ok":true,"online":N,"rooms":N,"rooms_total":N,"uptime":S}` |
| GET | `/api/history?room=482913&limit=50` | 某房间最近消息（最大 200） |
| GET | `/ws` | WebSocket 升级 |

## 管理员命令（仅本机）

管理通道是 **Unix socket**，不监听任何网络端口：`/run/liaotian/admin.sock`，
目录 `0700`、socket 文件 `0600`，只有板子上的 `lin` 和 `root` 能连。公网无法访问。

```bash
sudo lt-admin                      # 直接回车看帮助（交互式，Ctrl-D 退出）
lt-admin rooms                     # 所有房间：房号 / 人数 / 消息数 / 成员
lt-admin users 482913              # 某房间的在线用户
lt-admin read  482913 50           # 看最近 50 条（不会通知用户，不算"加入"）
lt-admin kick  482913 小林         # 把某人移出房间
lt-admin close 482913              # 立即关闭房间 + 删除记录 + 断开所有人
```

示例输出：

```
$ lt-admin rooms
房号      人数  消息  成员
482913      2      7  小林, 阿伟
947695      0      4  -
共 2 个房间
```

`read` 是纯读取，房间里的用户看不到任何"管理员进入"的提示；`kick` 会给对方
发一条"你已被管理员移出房间"然后断开；`close` 会让所有人回到大厅并清空记录。
## 部署

```bash
make
sudo install -m 644 deploy/liaotian.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now liaotian
sudo install -m 644 deploy/nginx-ldsjypl.conf /etc/nginx/sites-available/ldsjypl
sudo nginx -t && sudo systemctl reload nginx
```

外层入口（当前线上）：`cloudflared` → `127.0.0.1:8082`（nginx 明文回源）→ `127.0.0.1:8081`。

## 运维

```bash
sudo journalctl -u liaotian -f          # 日志：建房/进房/离房/销毁
systemctl show liaotian -p MemoryCurrent
ls data/                                 # 当前存活房间的记录文件
```

房间记录是纯追加 JSONL，掉电最多丢最后一行（解析时跳过残行）。

## 排错

| 现象 | 排查 |
|---|---|
| 提示"房间不存在" | 房号错，或房间已因空置被销毁 |
| 提示"房间人数已满" | 单房上限 30，开新房间 |
| 页面能开但连不上 | `curl -sk https://域名/api/health`；看 `cloudflared` 是否 active |
| 局域网设备连不上域名 | ufw 拦 IPv6 邻居发现，见 `wukongpi-maintenance-log.md` |
