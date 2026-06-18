# UDX710 SMS Gateway

[![Release](https://img.shields.io/github/v/release/Hxyspace/udx710-sms-gateway)](https://github.com/Hxyspace/udx710-sms-gateway/releases)
![Platform](https://img.shields.io/badge/platform-UDX710-red.svg)
![Language](https://img.shields.io/badge/language-C-green.svg)
![Node](https://img.shields.io/badge/node-20%2B-purple.svg)
[![Build](https://github.com/Hxyspace/udx710-sms-gateway/actions/workflows/build.yml/badge.svg)](https://github.com/Hxyspace/udx710-sms-gateway/actions/workflows/build.yml)

面向华为通讯壳和 UDX710 方案随身 WiFi 的短信网关。设备通过 USB 网卡暴露 `192.168.66.1`，本程序运行在设备内，提供短信收发、本地 Web UI、设备端 API，并可把收到的短信推送到电脑端服务。

UDX710 是紫光展锐 / Unisoc 方案。当前已在数源科技华为通讯壳 `SY108-658`、`SY108-698`、`SY108-688` 上实测可用；理论上同类 UDX710 随身 WiFi 设备也可以复用这套方案。

项目目标很明确：设备端不激活 APN、不主动跑蜂窝数据流量，同时能稳定收发短信。

## 功能

- 设备端监听短信并保存到 `/home/root/sms-gateway/messages.db`
- 设备端提供轻量 Web UI，默认监听 `18080`
- 设备端提供短信查询、发送、删除、清空 API
- 设备端自动设置 `setenforce 0`
- 设备端设置 oFono `RoamingAllowed=true`
- 设备端启动 30 秒后设置 `80 -> 18080` 端口转发
- 设备端通过 `usb0` ARP 表识别电脑端 IP，并推送新短信
- 电脑端 TypeScript 服务接收通知、保存 JSONL、预留飞书 webhook 推送

设备端不会激活 APN，不调用 `ActivatePdp`，也不依赖防火墙 DROP 来阻断数据。

## 架构

```
┌────────────────────┐        USB RNDIS         ┌────────────────────┐
│  UDX710 device     │  192.168.66.1/usb0       │  Linux PC          │
│                    │ ───────────────────────> │                    │
│  udx710-sms-gateway│                          │  notify server     │
│  Web UI :18080     │                          │  :18080            │
│  SQLite messages   │                          │  JSONL + Feishu    │
└────────────────────┘                          └────────────────────┘
```

## 目录结构

```
.
├── src/                    # 设备端 C 守护进程
├── web/                    # 嵌入到设备端二进制的 Web UI
├── server/                 # 电脑端 TypeScript 通知服务
├── third_party/glib        # 设备端 GLib 运行/构建依赖
├── third_party/sqlite      # 交叉编译用 sqlite 头文件和链接库
├── docs/device-api.md      # 设备端 HTTP API
└── .github/workflows       # GitHub Actions 构建
```

## 构建

### 本地构建设备端

需要 `aarch64-linux-gnu-gcc` 在 `PATH` 中。当前使用的交叉编译链是：

```text
/home/yuan/tools/gcc-linaro/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu
```

```sh
export PATH=/home/yuan/tools/gcc-linaro/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/bin:$PATH
make
```

输出：

```text
build/udx710-sms-gateway
```

设备端短信数据库使用 SQLite C API。项目内携带交叉编译用 sqlite 头文件和链接库，但目标设备系统已提供 `libsqlite3.so.0`，部署时不需要额外复制项目内 sqlite 动态库。

### 构建电脑端服务

要求 Node.js 20 或更高版本。

```sh
cd server
npm install
npm run dev
```

生产运行：

```sh
npm run build
npm start
```

如果使用 GitHub Release 里的 `udx710-sms-notify-server.tar.gz`，解压后直接运行：

```sh
node index.js
```

默认监听：

```text
0.0.0.0:18080
```

## GitHub Actions

项目提供 `.github/workflows/build.yml`。push、PR、手动触发和 `v*` tag 都会构建设备端二进制，并构建电脑端 TypeScript 服务。

Actions 不把交叉编译链提交进仓库，而是从本仓库 GitHub Release 下载工具链包：

```text
Release tag: toolchain-gcc-linaro-7.5.0
Asset name : gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu.tar.xz
```

首次使用前，把本地工具链压缩包上传到该 release：

```sh
gh release create toolchain-gcc-linaro-7.5.0 \
  /home/yuan/tools/gcc-linaro/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu.tar.xz \
  --title "GCC Linaro 7.5.0 aarch64 toolchain" \
  --notes "Build toolchain for UDX710 communication case SMS gateway"
```

公开仓库中，不需要额外配置个人 token。workflow 使用 GitHub Actions 自动提供的 `GITHUB_TOKEN` 访问同仓库 release，并在 `v*` tag 时发布构建产物。只有工具链放在另一个私有仓库、或需要跨仓库读写 release 时，才需要额外配置 PAT。

推送 tag 会自动发布两个产物：

- `udx710-sms-gateway-aarch64.tar.gz`：设备端 aarch64 二进制
- `udx710-sms-notify-server.tar.gz`：电脑端 TypeScript 编译产物，入口为 `index.js`

```sh
git tag v1.0.0
git push origin v1.0.0
```

## 设备端运行

设备上以 root 运行：

```sh
./udx710-sms-gateway
```

访问：

```text
http://192.168.66.1:18080/
http://192.168.66.1/
```

其中 `80 -> 18080` 端口转发会在启动后 30 秒设置。

## 配置

### 设备端

配置文件：

```text
/home/root/sms-gateway/device.conf
```

首次运行会自动生成：

```ini
[device]
device_id=udx710
notify_enabled=true
# notify_host=192.168.66.6
# notify_port=18080
token=
```

通知目标选择顺序：

1. 读取 `/proc/net/arp`
2. 找 `Device=usb0` 且 `Flags=0x2` 的 IP
3. 找不到则使用配置文件里的 `notify_host`
4. 配置也没有则使用默认 `192.168.66.6`

端口选择顺序：

1. 配置文件 `notify_port`
2. 默认 `18080`

### 电脑端

配置文件：

```text
server/notify-server.json
```

默认：

```json
{
  "listenHost": "0.0.0.0",
  "listenPort": 18080,
  "token": "",
  "deviceBaseUrl": "http://192.168.66.1:18080",
  "dataFile": "notify-messages.jsonl",
  "feishuEnabled": false,
  "feishuWebhook": "",
  "feishuOpenId": ""
}
```

`token` 非空时，设备端请求必须带：

```http
X-Device-Token: <token>
```

## API 文档

- 设备端 API: [docs/device-api.md](docs/device-api.md)
- 电脑端通知服务:
  - `GET /sms/notify/health`
  - `POST /sms/notify`
  - `GET /api/messages`
  - `GET /api/device/messages`
  - `POST /api/sync/device`

## 设备端推送到电脑端

设备收到短信并保存数据库后，会先检查电脑端健康状态：

```http
GET /sms/notify/health
X-Device-Token: <token>
```

只有健康检查返回 `200` 后，设备才推送短信：

```http
POST /sms/notify
Content-Type: application/json
X-Device-Token: <token>

{
  "device_id": "udx710",
  "message_id": 12,
  "direction": "in",
  "phone": "10086",
  "content": "短信内容",
  "timestamp": 1710000000,
  "status": "received"
}
```

电脑端按 `device_id + message_id` 去重。`inserted=false` 表示电脑端已经收到过这条短信。

## 飞书推送

电脑端当前只实现最小 text webhook 推送，方便后续替换成卡片式交互。

```json
{
  "feishuEnabled": true,
  "feishuWebhook": "https://open.feishu.cn/open-apis/bot/v2/hook/xxx",
  "feishuOpenId": ""
}
```

当前推送内容：

```text
收到短信
号码：10086
时间：2024/3/9 16:00:00
内容：短信内容
```

## 调试

电脑端日志示例：

```text
notify server listening on 0.0.0.0:18080
device api: http://192.168.66.1:18080
[health] ok
[notify] received inserted=true device=udx710 message_id=12 phone=10086 content_len=20
[notify] duplicate device=udx710 message_id=12
[feishu] push failed: ...
```

设备端关键日志：

```text
set RoamingAllowed=true success.
HTTP server listening on 18080.
udx710-sms-gateway started.
detected USB notify host: 192.168.66.6
SMS notify target: http://192.168.66.6:18080/sms/notify
SMS pushed to notify server: id=12
```
