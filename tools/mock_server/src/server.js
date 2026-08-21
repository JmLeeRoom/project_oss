import http from 'http';
import crypto from 'crypto';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const DASHBOARD_DIR = path.resolve(__dirname, '../../web_dashboard');

const PORT = 8080;

// State storage
let currentState = 'Idle';
let activeSessionId = crypto.randomUUID();
let currentPendingApproval = null;
let auditLogs = [
  {
    seq: 1,
    turn_id: 1,
    process_epoch_id: 'epoch_20260821_001',
    event_type: 'turn_begin',
    tool_name: null,
    verdict: 'allowed',
    reason_code: 'allowed',
    action_digest: '4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b',
    hash_chain_status: 'verified',
    latency_us: 1240,
    timestamp: new Date(Date.now() - 60000).toISOString()
  },
  {
    seq: 2,
    turn_id: 1,
    process_epoch_id: 'epoch_20260821_001',
    event_type: 'tool_execute',
    tool_name: 'opcua.read_defect_stats',
    verdict: 'allowed',
    reason_code: 'allowed',
    action_digest: '9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08',
    hash_chain_status: 'verified',
    latency_us: 4320,
    timestamp: new Date(Date.now() - 50000).toISOString()
  }
];

const toolsRegistry = [
  {
    name: 'opcua.read_defect_stats',
    description: '최근 라인 검사 통계 및 불량률 조회',
    effect: 'none',
    risk: 'low',
    idempotency: 'safe',
    approval_required: false,
    grammar_coverage: 'full',
    status: 'enabled'
  },
  {
    name: 'opcua.set_inspection_threshold',
    description: '비전 AI 검사 임계값(0.0~1.0) 변경',
    effect: 'write',
    risk: 'high',
    idempotency: 'conditional',
    approval_required: true,
    grammar_coverage: 'full',
    status: 'enabled'
  },
  {
    name: 'system.purge_audit_logs',
    description: '감사 로그 강제 초기화',
    effect: 'destructive',
    risk: 'critical',
    idempotency: 'unsafe',
    approval_required: true,
    grammar_coverage: 'none',
    status: 'forbidden',
    forbidden_reason: '보안 정책에 따라 런타임 호출 영구 금지'
  }
];

// Active WebSocket client sockets
const wsClients = new Set();

function transitionFSM(newState) {
  const from = currentState;
  currentState = newState;
  console.log(`[FSM Transition] ${from} -> ${newState}`);
  broadcast({
    type: 'fsm_state_change',
    data: { from, to: newState, timestamp: Date.now() }
  });
}

// HTTP Server
const server = http.createServer((req, res) => {
  // CORS & Security Headers
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization, X-Requested-With');
  
  // G0-33 Strict Content Security Policy (Zero unsafe-inline)
  res.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self' ws: http:; font-src 'self'; frame-ancestors 'none';");
  res.setHeader('X-Frame-Options', 'DENY');
  res.setHeader('X-Content-Type-Options', 'nosniff');

  if (req.method === 'OPTIONS') {
    res.writeHead(204);
    res.end();
    return;
  }

  const url = new URL(req.url, `http://${req.headers.host}`);

  // 1. Session start
  if (req.method === 'POST' && url.pathname === '/api/v1/session/start') {
    activeSessionId = crypto.randomUUID();
    currentState = 'Idle';
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      sessionId: activeSessionId,
      mode: 'Default',
      createdAt: new Date().toISOString()
    }));
    return;
  }

  // 2. Turn input
  if (req.method === 'POST' && url.pathname === '/api/v1/agent/turn') {
    let body = '';
    req.on('data', chunk => body += chunk);
    req.on('end', () => {
      const data = JSON.parse(body || '{}');
      const message = data.message || '';

      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ status: 'processing', sessionId: activeSessionId }));

      handleTurnScenario(message);
    });
    return;
  }

  // 3. Approval response
  if (req.method === 'POST' && url.pathname === '/api/v1/approval/respond') {
    let body = '';
    req.on('data', chunk => body += chunk);
    req.on('end', () => {
      const data = JSON.parse(body || '{}');
      const { approvalId, decision, approverId } = data;

      if (!currentPendingApproval || currentPendingApproval.approvalId !== approvalId) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'No matching pending approval found or expired' }));
        return;
      }

      console.log(`[Approval Response] approvalId=${approvalId}, decision=${decision}, approver=${approverId}`);
      
      const approved = (decision === 'Approved' || decision === 'allow');
      const resolvedDigest = currentPendingApproval.actionDigest;
      currentPendingApproval = null;

      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ status: 'ok', decision: approved ? 'Approved' : 'Rejected' }));

      // Resume FSM Loop
      if (approved) {
        transitionFSM('Executing');
        setTimeout(() => {
          auditLogs.push({
            seq: auditLogs.length + 1,
            turn_id: 2,
            process_epoch_id: 'epoch_20260821_001',
            event_type: 'tool_execute',
            tool_name: 'opcua.set_inspection_threshold',
            verdict: 'approved_and_executed',
            reason_code: 'allowed',
            action_digest: resolvedDigest,
            hash_chain_status: 'verified',
            latency_us: 15200,
            timestamp: new Date().toISOString()
          });
          broadcast({ type: 'audit_event', data: auditLogs[auditLogs.length - 1] });

          transitionFSM('Finalizing');
          setTimeout(() => {
            transitionFSM('Idle');
            broadcast({
              type: 'agent_message',
              data: {
                role: 'assistant',
                content: '작업자 승인 완료: 비전 검사 임계값이 0.80에서 0.95로 성공적으로 변경되었습니다. 설비 상태 정합성이 검증되었습니다.'
              }
            });
          }, 400);
        }, 800);
      } else {
        transitionFSM('Finalizing');
        setTimeout(() => {
          auditLogs.push({
            seq: auditLogs.length + 1,
            turn_id: 2,
            process_epoch_id: 'epoch_20260821_001',
            event_type: 'approval_rejected',
            tool_name: 'opcua.set_inspection_threshold',
            verdict: 'rejected',
            reason_code: 'approval_rejected',
            action_digest: resolvedDigest,
            hash_chain_status: 'verified',
            latency_us: 520,
            timestamp: new Date().toISOString()
          });
          broadcast({ type: 'audit_event', data: auditLogs[auditLogs.length - 1] });

          transitionFSM('Idle');
          broadcast({
            type: 'agent_message',
            data: {
              role: 'assistant',
              content: '작업자가 변경 요청을 거부(Reject)하여 설비 파라미터가 변경되지 않았습니다. 거부 사유가 감사 로그에 안전하게 기록되었습니다.'
            }
          });
        }, 400);
      }
    });
    return;
  }

  // 4. Audit logs
  if (req.method === 'GET' && url.pathname === '/api/v1/audit/logs') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      total: auditLogs.length,
      chainStatus: 'verified',
      logs: auditLogs
    }));
    return;
  }

  // 5. Tools registry
  if (req.method === 'GET' && url.pathname === '/api/v1/tools/registry') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      tools: toolsRegistry,
      registryDigest: 'f7c3bc1d808e04732adf679965ccc34ca7ae3441'
    }));
    return;
  }

  // 6. Current FSM state
  if (req.method === 'GET' && url.pathname === '/api/v1/fsm/state') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      state: currentState,
      sessionId: activeSessionId,
      pendingApproval: currentPendingApproval
    }));
    return;
  }

  // 7. Static Dashboard Serving
  if (req.method === 'GET') {
    let filePath = path.join(DASHBOARD_DIR, url.pathname === '/' ? 'index.html' : url.pathname);
    if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
      const ext = path.extname(filePath);
      const mimeTypes = {
        '.html': 'text/html; charset=utf-8',
        '.css': 'text/css; charset=utf-8',
        '.js': 'application/javascript; charset=utf-8',
        '.svg': 'image/svg+xml',
        '.json': 'application/json'
      };
      res.writeHead(200, { 'Content-Type': mimeTypes[ext] || 'application/octet-stream' });
      fs.createReadStream(filePath).pipe(res);
      return;
    }
  }

  res.writeHead(404, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify({ error: 'Not found' }));
});

// Scenario Handler
function handleTurnScenario(userMessage) {
  // 1. Start Thinking
  transitionFSM('Thinking');
  broadcast({
    type: 'agent_message',
    data: { role: 'user', content: userMessage }
  });

  setTimeout(() => {
    // 2. Evaluating Gate
    transitionFSM('GateEvaluating');

    setTimeout(() => {
      // Scenario A: Inspection Threshold Change (Write/High Risk -> Ask)
      if (userMessage.includes('임계값') || userMessage.includes('변경') || userMessage.includes('수정') || userMessage.includes('threshold')) {
        const approvalId = crypto.randomUUID();
        const actionDigest = crypto.createHash('sha256').update(`action:${Date.now()}:set_threshold`).digest('hex');

        currentPendingApproval = {
          approvalId,
          toolName: 'opcua.set_inspection_threshold',
          risk: 'high',
          effect: 'write',
          reasonCode: 'approval_required',
          reason: '고위험 설비 쓰기 작업(Write/High Risk)으로 작업자 명시적 승인 필수',
          before: { threshold: 0.80, line: 'Line-3A', camera_id: 'CAM-02' },
          requested: { threshold: 0.95, line: 'Line-3A', camera_id: 'CAM-02' },
          actionDigest,
          createdAt: new Date().toISOString(),
          expiresInSeconds: 300
        };

        transitionFSM('PendingApproval');
        broadcast({
          type: 'pending_approval',
          data: currentPendingApproval
        });
      }
      // Scenario B: Deny (Forbidden / Policy Deny)
      else if (userMessage.includes('삭제') || userMessage.includes('delete') || userMessage.includes('purge')) {
        transitionFSM('Finalizing');
        setTimeout(() => {
          auditLogs.push({
            seq: auditLogs.length + 1,
            turn_id: 3,
            process_epoch_id: 'epoch_20260821_001',
            event_type: 'policy_denied',
            tool_name: 'system.purge_audit_logs',
            verdict: 'denied',
            reason_code: 'tool_forbidden',
            action_digest: crypto.createHash('sha256').update('denied_purge').digest('hex'),
            hash_chain_status: 'verified',
            latency_us: 180,
            timestamp: new Date().toISOString()
          });
          broadcast({ type: 'audit_event', data: auditLogs[auditLogs.length - 1] });

          transitionFSM('Idle');
          broadcast({
            type: 'agent_message',
            data: {
              role: 'assistant',
              content: '[차단됨] PermissionPolicy 위반: 요청한 기능(system.purge_audit_logs)은 보안 불변식에 의해 영구 차단된 금지 도구(Forbidden)입니다. (사유: tool_forbidden)'
            }
          });
        }, 300);
      }
      // Scenario C: Read-only defect stats query (Allow)
      else {
        transitionFSM('Executing');
        setTimeout(() => {
          auditLogs.push({
            seq: auditLogs.length + 1,
            turn_id: 4,
            process_epoch_id: 'epoch_20260821_001',
            event_type: 'tool_execute',
            tool_name: 'opcua.read_defect_stats',
            verdict: 'allowed',
            reason_code: 'allowed',
            action_digest: crypto.createHash('sha256').update('read_stats').digest('hex'),
            hash_chain_status: 'verified',
            latency_us: 3820,
            timestamp: new Date().toISOString()
          });
          broadcast({ type: 'audit_event', data: auditLogs[auditLogs.length - 1] });

          transitionFSM('Finalizing');
          setTimeout(() => {
            transitionFSM('Idle');
            broadcast({
              type: 'agent_message',
              data: {
                role: 'assistant',
                content: '최근 1시간 검사 결과 요약:\n- 총 검사 수량: 1,420개\n- 양품: 1,398개 (98.45%)\n- 불량: 22개 (스크래치 14건, 치수오차 8건)\n- 현재 검사 임계값: 0.80'
              }
            });
          }, 300);
        }, 500);
      }
    }, 400);
  }, 400);
}

// Native RFC 6455 WebSocket Handshake & Frame Handling
server.on('upgrade', (req, socket, head) => {
  if (req.headers['upgrade'] !== 'websocket') {
    socket.end('HTTP/1.1 400 Bad Request');
    return;
  }

  const key = req.headers['sec-websocket-key'];
  const acceptKey = crypto
    .createHash('sha1')
    .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
    .digest('base64');

  const headers = [
    'HTTP/1.1 101 Switching Protocols',
    'Upgrade: websocket',
    'Connection: Upgrade',
    `Sec-WebSocket-Accept: ${acceptKey}`
  ];

  socket.write(headers.join('\r\n') + '\r\n\r\n');
  wsClients.add(socket);
  console.log('[Native WS] Client connected. Total:', wsClients.size);

  // Send initial state
  sendWsFrame(socket, {
    type: 'fsm_state_change',
    data: { from: currentState, to: currentState, timestamp: Date.now() }
  });
  if (currentPendingApproval) {
    sendWsFrame(socket, {
      type: 'pending_approval',
      data: currentPendingApproval
    });
  }

  socket.on('data', buffer => {
    const parsed = parseWsFrame(buffer);
    if (parsed && parsed.text) {
      try {
        const msg = JSON.parse(parsed.text);
        if (msg.type === 'trigger_turn') {
          handleTurnScenario(msg.message || '');
        }
      } catch (e) {
        console.error('WS parse error:', e);
      }
    }
  });

  socket.on('close', () => {
    wsClients.delete(socket);
    console.log('[Native WS] Client disconnected. Remaining:', wsClients.size);
  });

  socket.on('error', () => {
    wsClients.delete(socket);
  });
});

function sendWsFrame(socket, dataObj) {
  try {
    const payload = Buffer.from(JSON.stringify(dataObj), 'utf8');
    const length = payload.length;
    let header;

    if (length <= 125) {
      header = Buffer.from([0x81, length]);
    } else if (length <= 65535) {
      header = Buffer.alloc(4);
      header[0] = 0x81;
      header[1] = 126;
      header.writeUInt16BE(length, 2);
    } else {
      header = Buffer.alloc(10);
      header[0] = 0x81;
      header[1] = 127;
      header.writeBigUInt64BE(BigInt(length), 2);
    }

    socket.write(Buffer.concat([header, payload]));
  } catch (err) {
    console.error('sendWsFrame error:', err);
  }
}

function parseWsFrame(buffer) {
  if (buffer.length < 2) return null;
  const isMasked = (buffer[1] & 0x80) === 0x80;
  let length = buffer[1] & 0x7f;
  let offset = 2;

  if (length === 126) {
    if (buffer.length < 4) return null;
    length = buffer.readUInt16BE(2);
    offset = 4;
  } else if (length === 127) {
    if (buffer.length < 10) return null;
    length = Number(buffer.readBigUInt64BE(2));
    offset = 10;
  }

  if (isMasked) {
    const mask = buffer.slice(offset, offset + 4);
    offset += 4;
    const payload = buffer.slice(offset, offset + length);
    for (let i = 0; i < payload.length; i++) {
      payload[i] ^= mask[i % 4];
    }
    return { text: payload.toString('utf8') };
  } else {
    const payload = buffer.slice(offset, offset + length);
    return { text: payload.toString('utf8') };
  }
}

function broadcast(msgObj) {
  for (const client of wsClients) {
    if (!client.destroyed) {
      sendWsFrame(client, msgObj);
    }
  }
}

server.listen(PORT, () => {
  console.log(`[Cogito++ Mock Server] Running at http://localhost:${PORT}`);
  console.log(`[Cogito++ Mock Server] Serving Dashboard at http://localhost:${PORT}/`);
  console.log(`[Cogito++ Mock Server] WebSocket endpoint ws://localhost:${PORT}/ws`);
});
