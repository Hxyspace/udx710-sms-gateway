# Device API

本文档描述设备端 `udx710-sms-gateway` 暴露的 HTTP API，面向华为通讯壳和同类 UDX710 随身 WiFi 设备。

设备端 HTTP 服务默认监听：

```text
http://192.168.66.1:18080
```

`80 -> 18080` 端口转发会在 `udx710-sms-gateway` 启动后 30 秒设置，因此通常也可以通过：

```text
http://192.168.66.1/
```

访问 Web UI 和 API。

## 查询短信列表

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

## 查询单条短信

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

不存在时返回：

```http
404 Not Found
```

## 发送短信

```http
POST /api/send
Content-Type: application/x-www-form-urlencoded

recipient=13800138000&content=hello
```

成功响应：

```json
{
  "status": "success",
  "message": "sent",
  "path": "/ril_0/message..."
}
```

失败时返回 `400` 或 `500`，响应里包含 `status=error`。

## 删除短信

```http
POST /api/delete
Content-Type: application/x-www-form-urlencoded

ids=12,13,14
```

响应：

```json
{"status":"success"}
```

## 清空短信

```http
POST /api/clear
Content-Type: application/x-www-form-urlencoded

direction=in
```

参数：

- `direction`: 只能是 `in` 或 `out`

响应：

```json
{"status":"success"}
```

## Web 资源

设备端 Web UI 资源直接嵌入 `udx710-sms-gateway` 二进制。常用路径：

```text
/
/index.html
/styles.css
/app.js
/layui.js
/vendor/css/layui.css
/vendor/font/iconfont.woff2
```

官方 layui 目录结构保留在源码里，但当前二进制只嵌入现代浏览器实际使用的 `iconfont.woff2`。
