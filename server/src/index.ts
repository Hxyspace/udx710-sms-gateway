import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';
import { existsSync, mkdirSync, readFileSync, writeFileSync, appendFileSync } from 'node:fs';
import { dirname, join } from 'node:path';

type Config = {
  listenHost: string;
  listenPort: number;
  token: string;
  deviceBaseUrl: string;
  dataFile: string;
  feishuEnabled: boolean;
  feishuWebhook: string;
  feishuOpenId: string;
};

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

const cwd = process.cwd();
const configPath = join(cwd, 'notify-server.json');

function defaultConfig(): Config {
  return {
    listenHost: '0.0.0.0',
    listenPort: 18080,
    token: '',
    deviceBaseUrl: 'http://192.168.66.1:9527',
    dataFile: 'notify-messages.jsonl',
    feishuEnabled: false,
    feishuWebhook: '',
    feishuOpenId: ''
  };
}

function ensureConfig(): Config {
  if (!existsSync(configPath)) {
    const config = defaultConfig();
    writeFileSync(configPath, JSON.stringify(config, null, 2) + '\n');
    return config;
  }

  const loaded = JSON.parse(readFileSync(configPath, 'utf8')) as Partial<Config>;
  return { ...defaultConfig(), ...loaded };
}

const config = ensureConfig();
const dataPath = join(cwd, config.dataFile);
mkdirSync(dirname(dataPath), { recursive: true });

const messages = new Map<string, SmsMessage>();

function messageKey(message: SmsMessage): string {
  return `${message.device_id}:${message.message_id}`;
}

function loadMessages(): void {
  if (!existsSync(dataPath)) {
    return;
  }

  for (const line of readFileSync(dataPath, 'utf8').split('\n')) {
    if (!line.trim()) {
      continue;
    }
    try {
      const message = JSON.parse(line) as SmsMessage;
      messages.set(messageKey(message), message);
    } catch {
      // Ignore a broken line and keep the service available.
    }
  }
}

function saveMessage(message: SmsMessage): boolean {
  const key = messageKey(message);
  if (messages.has(key)) {
    console.log(`[notify] duplicate device=${message.device_id} message_id=${message.message_id}`);
    return false;
  }

  messages.set(key, message);
  appendFileSync(dataPath, JSON.stringify(message) + '\n');
  return true;
}

function sendJson(res: ServerResponse, statusCode: number, payload: unknown): void {
  const body = JSON.stringify(payload);
  res.writeHead(statusCode, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': Buffer.byteLength(body),
    'cache-control': 'no-store'
  });
  res.end(body);
}

function readBody(req: IncomingMessage): Promise<string> {
  return new Promise((resolve, reject) => {
    let body = '';
    req.setEncoding('utf8');
    req.on('data', chunk => {
      body += chunk;
      if (body.length > 1024 * 1024) {
        req.destroy(new Error('request body too large'));
      }
    });
    req.on('end', () => resolve(body));
    req.on('error', reject);
  });
}

function tokenAllowed(req: IncomingMessage): boolean {
  if (!config.token) {
    return true;
  }
  return req.headers['x-device-token'] === config.token;
}

function parsePage(url: URL): { page: number; pageSize: number; all: boolean } {
  const page = Math.max(1, Number.parseInt(url.searchParams.get('page') || '1', 10) || 1);
  const pageSizeRaw = Number.parseInt(url.searchParams.get('page_size') || '10', 10) || 10;
  const pageSize = Math.min(Math.max(pageSizeRaw, 1), 500);
  const all = url.searchParams.get('all') === '1' || url.searchParams.get('all') === 'true';
  return { page, pageSize, all };
}

function listMessages(url: URL): unknown {
  const { page, pageSize, all } = parsePage(url);
  const deviceId = url.searchParams.get('device_id') || '';
  const direction = url.searchParams.get('direction') || '';
  let items = Array.from(messages.values()).sort((a, b) => {
    if (b.timestamp !== a.timestamp) {
      return b.timestamp - a.timestamp;
    }
    return b.message_id - a.message_id;
  });

  if (deviceId) {
    items = items.filter(item => item.device_id === deviceId);
  }
  if (direction) {
    items = items.filter(item => item.direction === direction);
  }

  const total = items.length;
  const pageItems = all ? items : items.slice((page - 1) * pageSize, page * pageSize);
  return { items: pageItems, total, page, page_size: pageSize, all };
}

async function proxyDeviceMessages(url: URL): Promise<unknown> {
  const target = new URL('/api/messages', config.deviceBaseUrl);
  for (const key of ['direction', 'page', 'page_size', 'all']) {
    const value = url.searchParams.get(key);
    if (value !== null) {
      target.searchParams.set(key, value);
    }
  }

  const response = await fetch(target);
  if (!response.ok) {
    throw new Error(`device api failed: ${response.status}`);
  }
  return response.json();
}

async function syncDeviceMessages(url: URL): Promise<unknown> {
  const data = await proxyDeviceMessages(url);
  if (!data || typeof data !== 'object' || !Array.isArray((data as { items?: unknown }).items)) {
    throw new Error('device api response is invalid');
  }

  let inserted = 0;
  for (const item of (data as { items: Array<Record<string, unknown>> }).items) {
    const message: SmsMessage = {
      device_id: 'device',
      message_id: Number(item.id) || 0,
      direction: String(item.direction || ''),
      phone: String(item.phone || ''),
      content: String(item.content || ''),
      timestamp: Number(item.timestamp) || 0,
      status: String(item.status || ''),
      received_at: Math.floor(Date.now() / 1000)
    };
    if (message.message_id > 0 && saveMessage(message)) {
      inserted++;
    }
  }

  return { status: 'success', inserted, source: data };
}

async function sendFeishu(message: SmsMessage): Promise<void> {
  if (!config.feishuEnabled || !config.feishuWebhook) {
    return;
  }

  const at = config.feishuOpenId ? `<at user_id="${config.feishuOpenId}"></at>` : '';
  const text =
    `${at}收到短信\n` +
    `号码：${message.phone}\n` +
    `时间：${new Date(message.timestamp * 1000).toLocaleString('zh-CN', { hour12: false })}\n` +
    `内容：${message.content}`;

  const response = await fetch(config.feishuWebhook, {
    method: 'POST',
    headers: {
      'User-Agent': 'udx710-sms-gateway-notify-server',
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      msg_type: 'text',
      content: { text }
    })
  });

  if (!response.ok) {
    throw new Error(`feishu http failed: ${response.status}`);
  }

  const result = await response.json() as Record<string, unknown>;
  const code = result.code ?? result.StatusCode ?? 0;
  if (code !== 0) {
    throw new Error(`feishu api failed: ${JSON.stringify(result)}`);
  }
}

async function handleNotify(req: IncomingMessage, res: ServerResponse): Promise<void> {
  if (!tokenAllowed(req)) {
    console.warn('[notify] reject unauthorized message');
    sendJson(res, 401, { status: 'error', error: 'unauthorized' });
    return;
  }

  const raw = await readBody(req);
  const input = JSON.parse(raw) as Partial<SmsMessage>;
  const message: SmsMessage = {
    device_id: String(input.device_id || 'device'),
    message_id: Number(input.message_id) || 0,
    direction: String(input.direction || 'in'),
    phone: String(input.phone || ''),
    content: String(input.content || ''),
    timestamp: Number(input.timestamp) || Math.floor(Date.now() / 1000),
    status: String(input.status || 'received'),
    received_at: Math.floor(Date.now() / 1000)
  };

  if (!message.message_id || !message.content) {
    console.warn(`[notify] reject invalid message message_id=${message.message_id} content_len=${message.content.length}`);
    sendJson(res, 400, { status: 'error', error: 'message_id and content are required' });
    return;
  }

  const inserted = saveMessage(message);
  if (inserted) {
    sendFeishu(message).catch(error => {
      console.error(`[feishu] push failed: ${error instanceof Error ? error.message : String(error)}`);
    });
  }
  console.log(
    `[notify] received inserted=${inserted} device=${message.device_id} ` +
    `message_id=${message.message_id} phone=${message.phone} content_len=${message.content.length}`
  );
  sendJson(res, 200, { status: 'success', inserted });
}

async function route(req: IncomingMessage, res: ServerResponse): Promise<void> {
  const url = new URL(req.url || '/', `http://${req.headers.host || 'localhost'}`);

  if (req.method === 'GET' && url.pathname === '/sms/notify/health') {
    if (!tokenAllowed(req)) {
      console.warn('[health] reject unauthorized request');
      sendJson(res, 401, { status: 'error', error: 'unauthorized' });
      return;
    }
    console.log('[health] ok');
    sendJson(res, 200, { status: 'ok' });
    return;
  }

  if (req.method === 'POST' && url.pathname === '/sms/notify') {
    await handleNotify(req, res);
    return;
  }

  if (req.method === 'GET' && url.pathname === '/api/messages') {
    sendJson(res, 200, listMessages(url));
    return;
  }

  if (req.method === 'GET' && url.pathname === '/api/device/messages') {
    sendJson(res, 200, await proxyDeviceMessages(url));
    return;
  }

  if (req.method === 'POST' && url.pathname === '/api/sync/device') {
    sendJson(res, 200, await syncDeviceMessages(url));
    return;
  }

  sendJson(res, 404, { status: 'error', error: 'not found' });
}

loadMessages();

const server = createServer((req, res) => {
  route(req, res).catch(error => {
    sendJson(res, 500, { status: 'error', error: error instanceof Error ? error.message : String(error) });
  });
});

server.on('error', error => {
  console.error(`notify server listen failed: ${error instanceof Error ? error.message : String(error)}`);
  process.exit(1);
});

server.listen(config.listenPort, config.listenHost, () => {
  console.log(`notify server listening on ${config.listenHost}:${config.listenPort}`);
  console.log(`device api: ${config.deviceBaseUrl}`);
});
