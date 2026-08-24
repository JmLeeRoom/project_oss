import http from 'http';
import crypto from 'crypto';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const DASHBOARD_DIR = path.resolve(__dirname, '../../web_dashboard');

const PORT = 8080;
const HOST = '127.0.0.1';

// Server & Epoch State
const PROCESS_EPOCH_ID = `epoch_${Date.now()}`;
let activeSessionId = crypto.randomUUID();
let currentState = 'Idle';
let activeTurnId = 1;
let lineWriteLockdown = false;
let executionMode = 'Default';
let writeCallCount = 0; // Invariant 8 & S10 exit criteria: track write tool invocations

// Pending approvals array (up to kMaxPending = 64)
const kMaxPending = 64;
let pendingApprovals = [];

// Command ID deduplication cache (up to 256)
const commandCache = new Map(); // command_id -> { status, body, timestamp }

// Turn outcomes store
const turnOutcomes = new Map(); // turn_id -> outcome

// Audit log journal (append-only mock SQLite store)
let auditLogs = [
  {
    seq: 1,
    event_id: crypto.randomUUID(),
    session_id: activeSessionId,
    turn_id: 1,
    process_epoch_id: PROCESS_EPOCH_ID,
    kind: 'turn_begin',
    event_type: 'turn_begin',
    tool_name: '',
    verdict: 'allowed',
    reason_code: 'allowed',
    action_digest: '4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b',
    actor_id: 'operator_station_01',
    hash_chain_status: 'verified',
    latency_us: 1240,
    timestamp: new Date(Date.now() - 60000).toISOString()
  },
  {
    seq: 2,
    event_id: crypto.randomUUID(),
    session_id: activeSessionId,
    turn_id: 1,
    process_epoch_id: PROCESS_EPOCH_ID,
    kind: 'tool_execute',
    event_type: 'tool_execute',
    tool_name: 'opcua.read_defect_stats',
    verdict: 'allowed',
    reason_code: 'allowed',
    action_digest: '9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08',
    actor_id: 'agent_core',
    hash_chain_status: 'verified',
    latency_us: 4320,
    timestamp: new Date(Date.now() - 50000).toISOString()
  }
];

// Tools Registry (§12-5 / §4-3)
const toolsRegistry = [
  {
    name: 'opcua.read_defect_stats',
    description: '최근 라인 검사 통계 및 불량률 조회',
    effect: 'none',
    risk: 'low',
    idempotency: 'safe',
    approval_required: false,
    grammar_coverage: 'full',
    status: 'enabled',
    provider_id: 'provider_opcua_01',
    invoker_id: 'invoker_sync'
  },
  {
    name: 'opcua.set_inspection_threshold',
    description: '비전 AI 검사 임계값(0.0~1.0) 변경',
    effect: 'write',
    risk: 'high',
    idempotency: 'conditional',
    approval_required: true,
    grammar_coverage: 'partial',
    status: 'enabled',
    provider_id: 'provider_opcua_01',
    invoker_id: 'invoker_sync'
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
    forbidden_reason: '보안 정책에 따라 런타임 호출 영구 금지',
    provider_id: 'provider_sys_01',
    invoker_id: 'invoker_sync'
  }
];

// Canonical FSM Transitions (19 Explicit + Universal Rules R0~R4) (§4-10, §5, §12-9)
const fsmTransitions = {
  states: ['Idle', 'Infer', 'Propose', 'Gate', 'AwaitApproval', 'Execute', 'Observe', 'Done', 'Failed', 'Cancelled'],
  explicit_transitions: [
    { from: 'Idle', event: 'TurnBegin', to: 'Infer' },
    { from: 'Infer', event: 'ProposalReady', to: 'Propose' },
    { from: 'Infer', event: 'InferenceFailed', to: 'Observe' },
    { from: 'Propose', event: 'ActionProposed', to: 'Gate' },
    { from: 'Propose', event: 'NoActionProposed', to: 'Observe' },
    { from: 'Gate', event: 'GateAllow', to: 'Execute' },
    { from: 'Gate', event: 'GateAsk', to: 'AwaitApproval' },
    { from: 'Gate', event: 'GateDeny', to: 'Observe' },
    { from: 'AwaitApproval', event: 'Approved', to: 'Gate' },
    { from: 'AwaitApproval', event: 'Rejected', to: 'Observe' },
    { from: 'AwaitApproval', event: 'ApprovalExpired', to: 'Observe' },
    { from: 'Execute', event: 'ExecSuccess', to: 'Observe' },
    { from: 'Execute', event: 'ExecErrorOrIndeterminate', to: 'Observe' },
    { from: 'Observe', event: 'TurnComplete', to: 'Done' },
    { from: 'Observe', event: 'TurnFailed', to: 'Failed' },
    { from: 'Done', event: 'StartNextTurn', to: 'Idle' },
    { from: 'Failed', event: 'StartNextTurn', to: 'Idle' },
    { from: 'Cancelled', event: 'StartNextTurn', to: 'Idle' }
  ],
  universal_rules: [
    { rule: 'R0', name: 'IdleCancelNoOp', description: 'Idle에서 Cancel은 no-op (전이/감사 없음)' },
    { rule: 'R1', name: 'AuditErrorFailed', description: '{Infer, Propose, Gate, AwaitApproval, Execute, Observe} -> Failed' },
    { rule: 'R2', name: 'CancelToCancelled', description: '{Infer, Propose, Gate, AwaitApproval, Observe} -> Cancelled' },
    { rule: 'R3', name: 'ExecuteCancelViaToken', description: 'Execute 중 취소는 CancelToken으로 전달되어 Indeterminate/Cancelled 후 Observe로 진입' },
    { rule: 'R4', name: 'TerminalNoOp', description: '종료 상태(Done, Failed, Cancelled)에서 AuditError/Cancel은 no-op' }
  ]
};

// SSE Client Connection Management
const sseClients = new Set();
const kMaxConcurrentStreams = 16;

function broadcastSSE(event, data, seq = null) {
  const eventId = seq || (auditLogs.length > 0 ? auditLogs[auditLogs.length - 1].seq : 1);
  const payload = `id: ${eventId}\nevent: ${event}\ndata: ${JSON.stringify(data)}\n\n`;

  for (const client of sseClients) {
    if (!client.res.writableEnded) {
      client.res.write(payload);
    }
  }
}

// 15-second keepalive timer
setInterval(() => {
  for (const client of sseClients) {
    if (!client.res.writableEnded) {
      client.res.write(': keepalive\n\n');
    }
  }
}, 15000);

function transitionFSM(newState, reason = '') {
  const from = currentState;
  currentState = newState;
  console.log(`[FSM Transition] ${from} -> ${newState} ${reason ? '(' + reason + ')' : ''}`);
  broadcastSSE('state_change', {
    from,
    to: newState,
    session_id: activeSessionId,
    turn_id: activeTurnId,
    monotonic_ns: process.hrtime.bigint().toString(),
    timestamp: new Date().toISOString()
  });
}

function appendAuditEntry(entry) {
  const seq = auditLogs.length + 1;
  const newEntry = {
    seq,
    event_id: crypto.randomUUID(),
    session_id: activeSessionId,
    turn_id: activeTurnId,
    process_epoch_id: PROCESS_EPOCH_ID,
    hash_chain_status: 'verified',
    timestamp: new Date().toISOString(),
    ...entry
  };
  auditLogs.push(newEntry);
  broadcastSSE('audit_event', newEntry, seq);
  return newEntry;
}

// HTTP Server
const server = http.createServer((req, res) => {
  const origin = req.headers.origin || '';
  const isAllowedOrigin = !origin || origin.includes('127.0.0.1') || origin.includes('localhost');

  // Security Headers (§12-6)
  res.setHeader('Content-Security-Policy', "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; connect-src 'self'; worker-src 'self'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'; upgrade-insecure-requests");
  res.setHeader('Strict-Transport-Security', 'max-age=31536000');
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('Referrer-Policy', 'no-referrer');
  res.setHeader('X-Frame-Options', 'DENY');

  if (origin && isAllowedOrigin) {
    res.setHeader('Access-Control-Allow-Origin', origin);
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type, X-Cogito-CSRF, Last-Event-ID, Authorization');
  }

  if (req.method === 'OPTIONS') {
    res.writeHead(204);
    res.end();
    return;
  }

  const url = new URL(req.url, `http://${req.headers.host || '127.0.0.1:8080'}`);

  // Cache-Control: no-store for all /api/*
  if (url.pathname.startsWith('/api/')) {
    res.setHeader('Cache-Control', 'no-store');
  }

  // --------------------------------------------------------------------------
  // 1. GET /api/events (SSE Stream — W6)
  // --------------------------------------------------------------------------
  if (req.method === 'GET' && url.pathname === '/api/events') {
    if (sseClients.size >= kMaxConcurrentStreams) {
      res.writeHead(503, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: 'Max concurrent streams limit reached (16)' }));
      return;
    }

    res.writeHead(200, {
      'Content-Type': 'text/event-stream; charset=utf-8',
      'Cache-Control': 'no-store',
      'Connection': 'keep-alive'
    });

    const client = { res, id: crypto.randomUUID() };
    sseClients.add(client);
    console.log(`[SSE] Client connected (${client.id}). Active streams: ${sseClients.size}`);

    // Initial handshake frame
    res.write(`: connected epoch=${PROCESS_EPOCH_ID}\n\n`);

    // Handle Last-Event-ID replay
    const lastEventIdHeader = req.headers['last-event-id'];
    if (lastEventIdHeader) {
      const fromSeq = parseInt(lastEventIdHeader, 10);
      if (!isNaN(fromSeq)) {
        const replayLogs = auditLogs.filter(l => l.seq > fromSeq);
        for (const l of replayLogs) {
          res.write(`id: ${l.seq}\nevent: audit_event\ndata: ${JSON.stringify(l)}\n\n`);
        }
      }
    }

    // Push current state snapshot
    res.write(`event: state_change\ndata: ${JSON.stringify({
      from: currentState,
      to: currentState,
      session_id: activeSessionId,
      turn_id: activeTurnId,
      process_epoch_id: PROCESS_EPOCH_ID,
      monotonic_ns: process.hrtime.bigint().toString(),
      timestamp: new Date().toISOString()
    })}\n\n`);

    req.on('close', () => {
      sseClients.delete(client);
      console.log(`[SSE] Client disconnected (${client.id}). Remaining: ${sseClients.size}`);
    });
    return;
  }

  // Helper to read JSON request body
  function readJsonBody(callback) {
    let body = '';
    req.on('data', chunk => body += chunk);
    req.on('end', () => {
      try {
        const parsed = JSON.parse(body || '{}');
        callback(null, parsed);
      } catch (err) {
        callback(err, null);
      }
    });
  }

  // --------------------------------------------------------------------------
  // 2. POST /api/turn (202 Accepted {command_id, state} — W13)
  // --------------------------------------------------------------------------
  if (req.method === 'POST' && url.pathname === '/api/turn') {
    readJsonBody((err, data) => {
      if (err) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON body' }));
        return;
      }

      const commandId = data.command_id || crypto.randomUUID();
      const userMessage = data.message || '';

      // Idempotency check: duplicate submissions return original without re-executing
      if (commandCache.has(commandId)) {
        const cached = commandCache.get(commandId);
        res.writeHead(202, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(cached.response));
        return;
      }

      if (currentState !== 'Idle') {
        res.writeHead(409, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: `FSM is busy (current: ${currentState})` }));
        return;
      }

      const responsePayload = {
        command_id: commandId,
        state: 'Infer',
        session_id: activeSessionId,
        turn_id: ++activeTurnId
      };

      commandCache.set(commandId, { response: responsePayload, timestamp: Date.now() });

      res.writeHead(202, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(responsePayload));

      // Asynchronously drive the FSM turn
      executeTurnPipeline(userMessage, commandId);
    });
    return;
  }

  // --------------------------------------------------------------------------
  // 3. POST /api/approve (202 Accepted {approval_id, state:"approved"} — W13, §8-4)
  // --------------------------------------------------------------------------
  if (req.method === 'POST' && url.pathname === '/api/approve') {
    readJsonBody((err, data) => {
      if (err) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
        return;
      }

      const { approval_id, action_digest_hex, nonce } = data;
      const targetIdx = pendingApprovals.findIndex(p => p.pending_approval_id === approval_id);

      if (targetIdx === -1) {
        res.writeHead(404, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Approval request not found or already consumed' }));
        return;
      }

      const pending = pendingApprovals[targetIdx];

      // §8-4 [S-2]: Verify approval_id + action_digest_hex + nonce ALL match
      if (pending.action_digest_hex !== action_digest_hex || pending.nonce !== nonce) {
        res.writeHead(403, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Approval verification failed (digest/nonce mismatch)' }));
        return;
      }

      // Check expiration
      if (Date.now() > pending.expires_at_timestamp) {
        pendingApprovals.splice(targetIdx, 1);
        res.writeHead(410, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Approval expired' }));
        return;
      }

      // Consume approval (single use)
      pendingApprovals.splice(targetIdx, 1);

      // Return 202 Accepted immediately (W13: NO turn result in response)
      res.writeHead(202, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ approval_id, state: 'approved' }));

      // Resume execution: Gate -> Execute -> Observe -> Done -> Idle
      resumeApprovedExecution(pending);
    });
    return;
  }

  // --------------------------------------------------------------------------
  // 4. POST /api/reject (202 Accepted — §8-4)
  // --------------------------------------------------------------------------
  if (req.method === 'POST' && url.pathname === '/api/reject') {
    readJsonBody((err, data) => {
      if (err) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
        return;
      }

      const { approval_id, action_digest_hex, nonce, reason } = data;
      const targetIdx = pendingApprovals.findIndex(p => p.pending_approval_id === approval_id);

      if (targetIdx === -1) {
        res.writeHead(404, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Approval request not found or expired' }));
        return;
      }

      const pending = pendingApprovals[targetIdx];
      if (pending.action_digest_hex !== action_digest_hex || pending.nonce !== nonce) {
        res.writeHead(403, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Rejection verification failed (digest/nonce mismatch)' }));
        return;
      }

      pendingApprovals.splice(targetIdx, 1);

      res.writeHead(202, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ approval_id, state: 'rejected' }));

      // Resume: Gate -> Observe -> Done -> Idle (Write Call Count = 0!)
      resumeRejectedExecution(pending, reason || 'Operator explicitly rejected');
    });
    return;
  }

  // --------------------------------------------------------------------------
  // 5. POST /api/cancel (202 Accepted — W14)
  // --------------------------------------------------------------------------
  if (req.method === 'POST' && url.pathname === '/api/cancel') {
    if (currentState === 'Idle') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ status: 'ok', note: 'R0: Idle cancel is no-op' }));
      return;
    }

    transitionFSM('Cancelled', 'Explicit operator cancel');
    appendAuditEntry({
      kind: 'turn_cancel',
      event_type: 'turn_cancel',
      tool_name: '',
      verdict: 'cancelled',
      reason_code: 'user_cancelled',
      action_digest: '',
      actor_id: 'operator_station_01',
      latency_us: 100
    });

    res.writeHead(202, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ state: 'Cancelled' }));

    setTimeout(() => {
      transitionFSM('Idle', 'Turn cleanup');
    }, 300);
    return;
  }

  // --------------------------------------------------------------------------
  // 6. POST /api/indeterminate/ack (202 Accepted — §6-4)
  // --------------------------------------------------------------------------
  if (req.method === 'POST' && url.pathname === '/api/indeterminate/ack') {
    readJsonBody((err, data) => {
      lineWriteLockdown = false;
      appendAuditEntry({
        kind: 'indeterminate_ack',
        event_type: 'indeterminate_ack',
        tool_name: '',
        verdict: 'acknowledged',
        reason_code: 'lockdown_cleared',
        action_digest: '',
        actor_id: 'operator_station_01',
        latency_us: 200,
        note: data.note || 'Operator cleared indeterminate lockdown'
      });

      res.writeHead(202, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ line_write_lockdown: false }));
    });
    return;
  }

  // --------------------------------------------------------------------------
  // 7. POST /api/finalize/retry & POST /api/session/seal
  // --------------------------------------------------------------------------
  if (req.method === 'POST' && url.pathname === '/api/finalize/retry') {
    res.writeHead(202, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ status: 'retry_initiated' }));
    return;
  }

  if (req.method === 'POST' && url.pathname === '/api/session/seal') {
    res.writeHead(202, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ status: 'session_sealed' }));
    return;
  }

  // --------------------------------------------------------------------------
  // 8. GET /api/state (§12-5)
  // --------------------------------------------------------------------------
  if (req.method === 'GET' && url.pathname === '/api/state') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      state: currentState,
      session_id: activeSessionId,
      turn_id: activeTurnId,
      process_epoch_id: PROCESS_EPOCH_ID,
      monotonic_ns: process.hrtime.bigint().toString(),
      assets_digest: '9e8c3b4a2f1e0d9c8b7a6f5e4d3c2b1a0f9e8d7c6b5a4f3e2d1c0b9a8f7e6d5c',
      line_write_lockdown: lineWriteLockdown,
      mode: executionMode,
      write_call_count: writeCallCount
    }));
    return;
  }

  // --------------------------------------------------------------------------
  // 9. GET /api/approvals/pending (§12-5, §12-7)
  // --------------------------------------------------------------------------
  if (req.method === 'GET' && url.pathname === '/api/approvals/pending') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      count: pendingApprovals.length,
      approvals: pendingApprovals
    }));
    return;
  }

  // --------------------------------------------------------------------------
  // 10. GET /api/tools (§12-5)
  // --------------------------------------------------------------------------
  if (req.method === 'GET' && url.pathname === '/api/tools') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      tools: toolsRegistry,
      registry_digest: 'f7c3bc1d808e04732adf679965ccc34ca7ae3441890123456789abcdef012345'
    }));
    return;
  }

  // --------------------------------------------------------------------------
  // 11. GET /api/budget (§12-5)
  // --------------------------------------------------------------------------
  if (req.method === 'GET' && url.pathname === '/api/budget') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      session_token_used: 4820,
      session_token_limit: 100000,
      turn_token_used: 350,
      turn_token_limit: 4096,
      wall_time_ms: 1820,
      deadline_ms: 30000
    }));
    return;
  }

  // --------------------------------------------------------------------------
  // 12. GET /api/transitions (§12-5, §12-9)
  // --------------------------------------------------------------------------
  if (req.method === 'GET' && url.pathname === '/api/transitions') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(fsmTransitions));
    return;
  }

  // --------------------------------------------------------------------------
  // 13. GET /api/audit (§12-5, §7-4)
  // --------------------------------------------------------------------------
  if (req.method === 'GET' && url.pathname === '/api/audit') {
    const fromSeq = parseInt(url.searchParams.get('from_seq') || '1', 10);
    const limit = Math.min(parseInt(url.searchParams.get('limit') || '50', 10), 200);

    const filtered = auditLogs
      .filter(l => l.seq >= fromSeq)
      .slice(0, limit)
      .map(l => ({
        seq: l.seq,
        event_id: l.event_id,
        turn_id: l.turn_id,
        process_epoch_id: l.process_epoch_id,
        event_type: l.event_type,
        tool_name: l.tool_name,
        verdict: l.verdict,
        reason_code: l.reason_code,
        action_digest: l.action_digest,
        actor_id: l.actor_id,
        hash_chain_status: l.hash_chain_status,
        latency_us: l.latency_us,
        timestamp: l.timestamp
      }));

    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      total: auditLogs.length,
      returned: filtered.length,
      chain_status: 'verified',
      logs: filtered
    }));
    return;
  }

  // --------------------------------------------------------------------------
  // 14. GET /api/turns/{turn_id} (§12-5)
  // --------------------------------------------------------------------------
  if (req.method === 'GET' && url.pathname.startsWith('/api/turns/')) {
    const requestedTurnId = parseInt(url.pathname.replace('/api/turns/', ''), 10);
    const outcome = turnOutcomes.get(requestedTurnId) || { turn_id: requestedTurnId, status: 'completed' };
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(outcome));
    return;
  }

  // --------------------------------------------------------------------------
  // 15. Static Dashboard Serving (`tools/web_dashboard`)
  // --------------------------------------------------------------------------
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
  res.end(JSON.stringify({ error: 'Endpoint not found' }));
});

// ----------------------------------------------------------------------------
// WebSocket Explicit Rejection (W6 Requirement)
// ----------------------------------------------------------------------------
server.on('upgrade', (req, socket) => {
  console.warn('[W6 Violation Blocked] Inbound WebSocket upgrade rejected with 400 Bad Request');
  socket.write('HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Type: text/plain\r\n\r\nWebSocket upgrade not supported. Use GET /api/events SSE.\r\n');
  socket.destroy();
});

// ----------------------------------------------------------------------------
// Scenario Execution Pipelines
// ----------------------------------------------------------------------------
function executeTurnPipeline(userMessage, commandId) {
  // 1. Idle -> Infer
  transitionFSM('Infer', 'turn_begin');
  appendAuditEntry({
    kind: 'turn_begin',
    event_type: 'turn_begin',
    tool_name: '',
    verdict: 'allowed',
    reason_code: 'allowed',
    action_digest: '',
    actor_id: 'operator_station_01',
    latency_us: 950
  });

  broadcastSSE('agent_message', { role: 'user', content: userMessage });

  setTimeout(() => {
    // 2. Infer -> Propose
    transitionFSM('Propose', 'llm_proposal_ready');

    setTimeout(() => {
      // 3. Propose -> Gate
      transitionFSM('Gate', 'evaluating_policy_and_mode');

      setTimeout(() => {
        // Scenario A: Inspection Threshold Change (Write/High Risk -> Ask)
        if (userMessage.includes('임계값') || userMessage.includes('변경') || userMessage.includes('수정') || userMessage.includes('threshold')) {
          const approvalId = crypto.randomUUID();
          const actionDigestHex = crypto.createHash('sha256').update(`action:${Date.now()}:set_threshold`).digest('hex');
          const nonce = crypto.randomBytes(16).toString('hex');
          const expiresInMs = 120000;

          const pendingItem = {
            pending_action_id: crypto.randomUUID(),
            pending_approval_id: approvalId,
            tool_name: 'opcua.set_inspection_threshold',
            effect: 'write',
            risk: 'high',
            grammar_coverage: 'partial',
            approval_required: true,
            canonical_arguments: { line: 'Line-3A', camera_id: 'CAM-02', threshold: 0.95 },
            before: { line: 'Line-3A', camera_id: 'CAM-02', threshold: 0.80 },
            action_digest_hex: actionDigestHex,
            nonce: nonce,
            policy_rule_id: 'rule_policy_opcua_write_01',
            expires_in_ms: expiresInMs,
            expires_at_timestamp: Date.now() + expiresInMs,
            requester_subject_id: 'agent_cogito_core',
            untrusted_context: {
              source: 'model_response_untrusted',
              provenance: 'llama.cpp:q4_k_m:temp0.2',
              raw_text: '설비 최적화를 위해 V1 비전 검사기 임계값을 0.95로 상향 조정하고자 합니다. 승인 후 즉시 반영됩니다.'
            }
          };

          if (pendingApprovals.length < kMaxPending) {
            pendingApprovals.push(pendingItem);
          }

          transitionFSM('AwaitApproval', 'gate_ask');
          broadcastSSE('pending_approval', pendingItem);
        }
        // Scenario B: Deny (Policy Forbidden)
        else if (userMessage.includes('삭제') || userMessage.includes('delete') || userMessage.includes('purge')) {
          const actionDigest = crypto.createHash('sha256').update('denied_purge').digest('hex');
          transitionFSM('Observe', 'gate_deny');

          appendAuditEntry({
            kind: 'policy_denied',
            event_type: 'policy_denied',
            tool_name: 'system.purge_audit_logs',
            verdict: 'denied',
            reason_code: 'tool_forbidden',
            action_digest: actionDigest,
            actor_id: 'agent_core',
            latency_us: 180
          });

          broadcastSSE('verdict', {
            verdict: 'denied',
            reason_code: 'tool_forbidden',
            tool_name: 'system.purge_audit_logs',
            message: '보안 정책 위반: 런타임 호출이 영구 금지된 도구입니다 (tool_forbidden).'
          });

          setTimeout(() => {
            transitionFSM('Done', 'turn_outcome_finalized');
            setTimeout(() => {
              transitionFSM('Idle', 'awaiting_next_turn');
              broadcastSSE('agent_message', {
                role: 'assistant',
                content: '[차단됨] PermissionGate 거부: 요청한 도구(system.purge_audit_logs)는 보안 정책에 의해 호출이 영구 금지(Forbidden)되어 있습니다. (write 호출 0회)'
              });
            }, 300);
          }, 300);
        }
        // Scenario C: Read-Only stats query (Allow)
        else {
          transitionFSM('Execute', 'gate_allow');

          setTimeout(() => {
            const actionDigest = crypto.createHash('sha256').update('read_stats').digest('hex');
            appendAuditEntry({
              kind: 'tool_execute',
              event_type: 'tool_execute',
              tool_name: 'opcua.read_defect_stats',
              verdict: 'allowed',
              reason_code: 'allowed',
              action_digest: actionDigest,
              actor_id: 'agent_core',
              latency_us: 3820
            });

            transitionFSM('Observe', 'exec_success');

            setTimeout(() => {
              transitionFSM('Done', 'turn_outcome_finalized');
              setTimeout(() => {
                transitionFSM('Idle', 'awaiting_next_turn');
                broadcastSSE('agent_message', {
                  role: 'assistant',
                  content: '최근 1시간 검사 결과 요약:\n- 총 검사 수량: 1,420개\n- 양품: 1,398개 (98.45%)\n- 불량: 22개 (스크래치 14건, 치수오차 8건)\n- 현재 비전 임계값: 0.80'
                });
              }, 300);
            }, 300);
          }, 400);
        }
      }, 300);
    }, 300);
  }, 300);
}

function resumeApprovedExecution(pending) {
  transitionFSM('Gate', 'approved_resumed');

  setTimeout(() => {
    transitionFSM('Execute', 'gate_allow_after_approval');
    writeCallCount++; // Verified valid execution

    setTimeout(() => {
      appendAuditEntry({
        kind: 'tool_execute',
        event_type: 'tool_execute',
        tool_name: pending.tool_name,
        verdict: 'approved_and_executed',
        reason_code: 'allowed',
        action_digest: pending.action_digest_hex,
        actor_id: 'operator_station_01',
        latency_us: 14200
      });

      broadcastSSE('verdict', {
        approval_id: pending.pending_approval_id,
        verdict: 'allowed',
        reason_code: 'allowed',
        state: 'executed',
        tool_name: pending.tool_name
      });

      transitionFSM('Observe', 'exec_success');

      setTimeout(() => {
        transitionFSM('Done', 'turn_outcome_finalized');
        setTimeout(() => {
          transitionFSM('Idle', 'awaiting_next_turn');
          broadcastSSE('agent_message', {
            role: 'assistant',
            content: '작업자 승인 완료: 비전 검사 임계값이 0.80에서 0.95로 성공적으로 변경되었습니다. (Write 실행 1회 커밋 완료)'
          });
        }, 300);
      }, 300);
    }, 400);
  }, 300);
}

function resumeRejectedExecution(pending, reason) {
  transitionFSM('Gate', 'rejected_resumed');

  setTimeout(() => {
    transitionFSM('Observe', 'gate_rejected');

    appendAuditEntry({
      kind: 'approval_rejected',
      event_type: 'approval_rejected',
      tool_name: pending.tool_name,
      verdict: 'rejected',
      reason_code: 'approval_rejected',
      action_digest: pending.action_digest_hex,
      actor_id: 'operator_station_01',
      latency_us: 450,
      note: reason
    });

    broadcastSSE('verdict', {
      approval_id: pending.pending_approval_id,
      verdict: 'rejected',
      reason_code: 'approval_rejected',
      state: 'rejected',
      tool_name: pending.tool_name,
      reason
    });

    setTimeout(() => {
      transitionFSM('Done', 'turn_outcome_finalized');
      setTimeout(() => {
        transitionFSM('Idle', 'awaiting_next_turn');
        broadcastSSE('agent_message', {
          role: 'assistant',
          content: `작업자 거부(Reject)로 인해 설비 파라미터가 변경되지 않았습니다 (write 0회 보장). 사유: ${reason}`
        });
      }, 300);
    }, 300);
  }, 300);
}

// ----------------------------------------------------------------------------
// Server Listen on 127.0.0.1 (W8)
// ----------------------------------------------------------------------------
server.listen(PORT, HOST, () => {
  console.log(`========================================================`);
  console.log(` Cogito++ Mock API Server (§12-5 SSE Engine)`);
  console.log(` - Bind Address: http://${HOST}:${PORT}`);
  console.log(` - Protocol: REST (15 Endpoints) + SSE (/api/events)`);
  console.log(` - WebSocket: EXPLICITLY DISABLED (W6 Compliance)`);
  console.log(` - Security Headers: Strict CSP, HSTS, No-Store`);
  console.log(`========================================================`);
});

