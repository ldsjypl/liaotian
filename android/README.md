# 聊天室 Android 客户端

`ldsjypl.online` 聊天室的安卓客户端，纯 Java + 原生控件，不依赖 AndroidX。

- 包名 `online.ldsjypl.chat`，minSdk 26，targetSdk 36
- WebSocket 用 OkHttp 4.12，UI 用 ListView + 自绘气泡
- 产物：`liaotian-1.0.apk`（约 2.2MB，debug 签名，可直接安装）

## 编译

```bash
# 需要 JDK 21 与 Android SDK（platform 36 / build-tools 36.0.0）
# local.properties 里的 sdk.dir 指向本机 SDK
cd android
gradlew.bat assembleDebug
# 产物：app/build/outputs/apk/debug/app-debug.apk
```

首次构建会从阿里云镜像下载 AGP 与 OkHttp，约 1–2 分钟。

## 安装

```bash
adb install -r liaotian-1.0.apk
```

或把 APK 传到手机直接安装（需允许"安装未知来源应用"）。

## 功能

| 页面 | 内容 |
|---|---|
| 大厅 | 昵称、新建房间、输入房号加入、违禁内容提示、使用说明 |
| 聊天 | 标题显示"聊天室 房号"与在线人数、消息气泡（自己蓝色/别人深色/系统居中）、离开、发送 |

行为与网页版一致：

- 房号由服务端随机分配且不公开，只有拿到房号的人能进
- 断线自动重连（指数退避），回到原房间
- 发送 `create`/`join` 后 6 秒没收到 `joined` 会自动重发（跨境线路丢包时很有用）
- 点系统返回键 = 离开房间

## 已知限制

- 服务器地址写死在 `MainActivity.HOST`（`ldsjypl.online:443`），暂不支持 App 内切换；
  想连局域网需要在源码里改 `HOST`/`PORT` 并重建
- 只支持文本，无图片/表情面板
- 依赖网络质量：跨境到 Cloudflare 的链路在校园网下握手可能要几秒

## 结构

```
android/
├── app/src/main/java/online/ldsjypl/chat/
│   ├── MainActivity.java   大厅/聊天两屏、连接管理、协议处理
│   ├── ChatAdapter.java    消息列表（三种样式）
│   └── WsClient.java       OkHttp WebSocket 封装
├── app/src/main/res/
│   ├── layout/             activity_main.xml、item_message.xml
│   ├── drawable/           输入框/按钮/气泡圆角背景
│   └── mipmap-*/           启动图标
└── build.gradle.kts        AGP 9.2.1、OkHttp 4.12
```
