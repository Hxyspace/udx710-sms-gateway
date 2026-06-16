# network-online

UDX710 类随身 WiFi 设备的短信服务。设备通过 USB 网卡暴露 `192.168.66.1`，本程序运行在设备内，负责接收/发送短信、提供本地 Web UI，并把收到的短信通过 USB 网卡推送到电脑端服务。

目标是：设备端不激活 APN、不跑蜂窝数据流量，但能稳定接收短信。

## 组件

### 设备端：`network-daemon`

运行在嵌入式设备上。

职责：

- 设置 `setenforce 0`
- 设置 oFono `RoamingAllowed=true`
- 启动短信监听
- 保存短信到 `/home/root/network/messages.db`
- 启动 Web/API 服务，监听 `9527`
- 30 秒后设置 `80 -> 9527` 转发
- 30 秒后识别 USB 对端电脑 IP，并初始化短信通知推送

设备端不会：

- 激活 APN
- 调用 `ActivatePdp`
- 设置数据链路 DROP 防火墙
- 主动访问公网

### 电脑端：`server/`

运行在 Linux 电脑上，用 TypeScript/Node 实现。

职责：

- 接收设备端推送的短信
- 本地保存通知记录到 JSONL
- 提供查看通知记录的 API
- 代理访问设备端短信 API
- 预留并实现了最简单的飞书 webhook text 推送

## 构建和运行

### 设备端构建

```sh
make
```

输出：

```text
build/network-daemon
```

设备端短信数据库使用 SQLite C API。项目内携带交叉编译用的 sqlite 头文件和链接库：

```text
third_party/sqlite/include
third_party/sqlite/lib
```

当前设备系统已提供 `libsqlite3.so.0`，部署时不需要额外把项目内 sqlite 动态库放到设备上。

### 设备端运行

设备上以 root 运行：

```sh
./network-daemon
```

访问：

```text
http://192.168.66.1:9527/
http://192.168.66.1/
```

其中 `80 -> 9527` 端口转发会在启动后 30 秒设置。

### 电脑端运行

要求 Node.js 24 或更高版本。

```sh
cd server
npm start
```

默认监听：

```text
0.0.0.0:18080
```

健康检查：

```sh
curl http://127.0.0.1:18080/sms/notify/health
```

## 配置

### 设备端配置

路径：

```text
/home/root/network/device.conf
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

### 电脑端配置

路径：

```text
server/notify-server.json
```

默认：

```json
{
  "listenHost": "0.0.0.0",
  "listenPort": 18080,
  "token": "",
  "deviceBaseUrl": "http://192.168.66.1:9527",
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

当前设备端会从 `/home/root/network/device.conf` 的 `token` 字段读取并发送。

## 设备端 API

Base URL：

```text
http://192.168.66.1:9527
```

### 查询短信列表

```http
GET /api/messages?direction=in&page=1&page_size=10
```

参数：

- `direction`: `in` 或 `out`，可省略表示全部
- `page`: 第几页，从 `1` 开始
- `page_size`: 每页数量，最大 `500`
- `all`: `1` 或 `true` 时返回全部，忽略分页切片

响应：

```json
{
  "items": [
    {
      "id": 12,
      "direction": "in",
      "phone": "10086",
      "content": "短信内容",
      "timestamp": 1710000000,
      "status": "received"
    }
  ],
  "total": 1,
  "page": 1,
  "page_size": 10,
  "all": false
}
```

### 查询单条短信

```http
GET /api/message?id=12
```

响应：

```json
{
  "id": 12,
  "direction": "in",
  "phone": "10086",
  "content": "短信内容",
  "timestamp": 1710000000,
  "status": "received"
}
```

### 发送短信

```http
POST /api/send
Content-Type: application/x-www-form-urlencoded

recipient=13800138000&content=hello
```

响应：

```json
{
  "status": "success",
  "message": "sent",
  "path": "/ril_0/message..."
}
```

### 删除短信

```http
POST /api/delete
Content-Type: application/x-www-form-urlencoded

ids=12,13,14
```

响应：

```json
{"status":"success"}
```

### 清空短信

```http
POST /api/clear
Content-Type: application/x-www-form-urlencoded

direction=in
```

`direction` 只能是 `in` 或 `out`。

响应：

```json
{"status":"success"}
```

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

电脑端响应：

```json
{
  "status": "success",
  "inserted": true
}
```

去重规则：

```text
device_id + message_id
```

电脑端同步设备历史短信时，如果设备 API 返回的是 `id`，会映射为 `message_id`。`inserted=false` 表示电脑端已经收到过这条短信。

## 电脑端 API

Base URL：

```text
http://<电脑USB网卡IP>:18080
```

### 健康检查

```http
GET /sms/notify/health
```

响应：

```json
{"status":"ok"}
```

### 接收设备短信通知

```http
POST /sms/notify
Content-Type: application/json

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

### 查询电脑端已接收通知

```http
GET /api/messages?page=1&page_size=10
```

参数：

- `device_id`: 可选
- `direction`: 可选
- `page`: 第几页
- `page_size`: 每页数量，最大 `500`
- `all`: `1` 或 `true` 返回全部

响应：

```json
{
  "items": [
    {
      "device_id": "udx710",
      "message_id": 12,
      "direction": "in",
      "phone": "10086",
      "content": "短信内容",
      "timestamp": 1710000000,
      "status": "received",
      "received_at": 1710000001
    }
  ],
  "total": 1,
  "page": 1,
  "page_size": 10,
  "all": false
}
```

### 代理查询设备端短信

电脑端会访问 `deviceBaseUrl`，默认是 `http://192.168.66.1:9527`。

```http
GET /api/device/messages?direction=in&page=1&page_size=10
```

响应格式与设备端 `/api/messages` 一致。

### 从设备端同步短信到电脑端

```http
POST /api/sync/device?direction=in&page=1&page_size=50
```

响应：

```json
{
  "status": "success",
  "inserted": 3,
  "source": {
    "items": [],
    "total": 0,
    "page": 1,
    "page_size": 50,
    "all": false
  }
}
```

## 飞书推送

当前电脑端只实现最小 text webhook 推送，方便后续替换成卡片式交互。

启用：

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

后续设计飞书卡片时，建议以电脑端收到的短信 JSON 为输入。当前电脑端保存固定字段，并补充 `received_at`：

```ts
type SmsMessage = {
  device_id: string;
  message_id: number;
  direction: string;
  phone: string;
  content: string;
  timestamp: number;
  status?: string;
  received_at?: number;
};
```

建议卡片操作可以围绕这些 API 设计：

- 查看单条短信详情：电脑端本地 `device_id + message_id`
- 拉取设备端最新短信：`GET /api/device/messages`
- 同步设备端历史短信：`POST /api/sync/device`
- 后续如果要从飞书回复短信，可调用设备端 `POST /api/send`

## 调试

电脑端日志示例：

```text
notify server listening on 0.0.0.0:18080
device api: http://192.168.66.1:9527
[health] ok
[notify] received inserted=true device=udx710 message_id=12 phone=10086 content_len=20
[notify] duplicate device=udx710 message_id=12
[feishu] push failed: ...
```

设备端关键日志：

```text
set RoamingAllowed=true success.
HTTP server listening on 9527.
network-daemon started.
detected USB notify host: 192.168.66.6
SMS notify target: http://192.168.66.6:18080/sms/notify
SMS pushed to notify server: id=12
```
