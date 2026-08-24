// Cogito++ Web Dashboard Frontend Logic

let ws = null;
let currentPendingApprovalId = null;

// Initialize Dashboard
document.addEventListener('DOMContentLoaded', () => {
  initWebSocket();
  fetchInitialData();
  setupEventHandlers();
});

// 1. WebSocket Connection
function initWebSocket() {
  const wsUrl = `ws://${window.location.hostname || 'localhost'}:8080/ws`;
  console.log('[Dashboard] Connecting to WebSocket:', wsUrl);

  try {
    ws = new WebSocket(wsUrl);

    ws.onopen = () => {
      console.log('[Dashboard] WebSocket connected');
      updateWsIndicator(true);
    };

    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        handleWsMessage(msg);
      } catch (err) {
        console.error('[Dashboard] WS Message Parse Error:', err);
      }
    };

    ws.onclose = () => {
      console.warn('[Dashboard] WebSocket disconnected. Reconnecting in 2s...');
      updateWsIndicator(false);
      setTimeout(initWebSocket, 2000);
    };

    ws.onerror = (err) => {
      console.error('[Dashboard] WebSocket Error:', err);
      updateWsIndicator(false);
    };
  } catch (err) {
    console.error('[Dashboard] WebSocket Init Error:', err);
    updateWsIndicator(false);
  }
}

function updateWsIndicator(connected) {
  const indicator = document.getElementById('ws-indicator');
  if (!indicator) return;
  if (connected) {
    indicator.innerHTML = '<span class="w-2 h-2 rounded-full bg-emerald-400"></span> Live Connected';
    indicator.className = 'flex items-center gap-1.5 text-emerald-400 font-medium text-xs';
  } else {
    indicator.innerHTML = '<span class="w-2 h-2 rounded-full bg-rose-500"></span> Disconnected';
    indicator.className = 'flex items-center gap-1.5 text-rose-400 font-medium text-xs';
  }
}

// 2. Message Dispatcher
function handleWsMessage(msg) {
  console.log('[Dashboard] WS Event Received:', msg.type, msg.data);

  if (msg.type === 'fsm_state_change') {
    updateFSMState(msg.data.to);
  } else if (msg.type === 'pending_approval') {
    showApprovalModal(msg.data);
  } else if (msg.type === 'agent_message') {
    appendChatMessage(msg.data.role, msg.data.content);
  } else if (msg.type === 'audit_event') {
    prependAuditRow(msg.data);
  }
}

// 3. FSM Visualizer Updater
function updateFSMState(stateName) {
  const badge = document.getElementById('fsm-badge');
  if (badge) {
    badge.textContent = stateName;
    if (stateName === 'PendingApproval') {
      badge.className = 'px-2.5 py-0.5 rounded-full text-xs font-mono font-bold bg-amber-500/20 text-amber-300 border border-amber-500/40';
    } else if (stateName === 'Thinking' || stateName === 'GateEvaluating') {
      badge.className = 'px-2.5 py-0.5 rounded-full text-xs font-mono font-bold bg-blue-500/20 text-blue-300 border border-blue-500/40';
    } else if (stateName === 'Executing') {
      badge.className = 'px-2.5 py-0.5 rounded-full text-xs font-mono font-bold bg-purple-500/20 text-purple-300 border border-purple-500/40';
    } else {
      badge.className = 'px-2.5 py-0.5 rounded-full text-xs font-mono font-bold bg-emerald-500/20 text-emerald-300 border border-emerald-500/40';
    }
  }

  // Update SVG Nodes
  const nodes = ['Idle', 'Thinking', 'GateEvaluating', 'PendingApproval', 'Executing', 'Finalizing'];
  nodes.forEach(name => {
    const el = document.getElementById(`node-${name}`);
    if (el) {
      if (name === stateName) {
        el.classList.add('fsm-active');
        if (name === 'PendingApproval') el.classList.add('fsm-node-ask');
      } else {
        el.classList.remove('fsm-active');
        el.classList.remove('fsm-node-ask');
      }
    }
  });
}

// 4. Approval Modal Logic (§12-8 Compliant)
function showApprovalModal(data) {
  currentPendingApprovalId = data.approvalId;

  document.getElementById('modal-tool-name').textContent = data.toolName || 'opcua.write_parameter';
  document.getElementById('modal-reason-code').textContent = data.reasonCode || 'approval_required';
  document.getElementById('modal-action-digest').textContent = data.actionDigest || 'unknown_digest';

  document.getElementById('diff-before').textContent = JSON.stringify(data.before, null, 2);
  document.getElementById('diff-requested').textContent = JSON.stringify(data.requested, null, 2);

  const modal = document.getElementById('approval-modal');
  modal.classList.remove('hidden');
}

function hideApprovalModal() {
  const modal = document.getElementById('approval-modal');
  modal.classList.add('hidden');
  currentPendingApprovalId = null;
}

// 5. Send User Message
async function sendAgentTurn(message) {
  if (!message.trim()) return;
  appendChatMessage('user', message);

  try {
    const res = await fetch('http://localhost:8080/api/v1/agent/turn', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message })
    });
    const data = await res.json();
    console.log('[Dashboard] Turn response:', data);
  } catch (err) {
    console.error('[Dashboard] sendAgentTurn error:', err);
    appendChatMessage('assistant', `[통신 오류] 서버와 통신할 수 없습니다: ${err.message}`);
  }
}

// 6. Respond to Approval
async function respondApproval(decision) {
  if (!currentPendingApprovalId) return;
  const approvalId = currentPendingApprovalId;
  hideApprovalModal();

  try {
    const res = await fetch('http://localhost:8080/api/v1/approval/respond', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        approvalId,
        decision,
        approverId: 'operator_hmi_station_01'
      })
    });
    const data = await res.json();
    console.log('[Dashboard] Approval respond result:', data);
  } catch (err) {
    console.error('[Dashboard] respondApproval error:', err);
  }
}

// 7. UI Helpers
function appendChatMessage(role, text) {
  const container = document.getElementById('chat-messages');
  if (!container) return;

  const msgDiv = document.createElement('div');
  msgDiv.className = `chat-msg ${role === 'user' ? 'chat-user' : 'chat-assistant'}`;

  const senderSpan = document.createElement('span');
  senderSpan.className = `chat-sender ${role === 'user' ? 'text-blue-400' : 'text-emerald-400'} font-semibold`;
  senderSpan.textContent = role === 'user' ? '작업자 (Operator)' : 'Cogito++ Agent';

  const contentP = document.createElement('p');
  contentP.textContent = text;

  msgDiv.appendChild(senderSpan);
  msgDiv.appendChild(contentP);
  container.appendChild(msgDiv);

  container.scrollTop = container.scrollHeight;
}

function prependAuditRow(log) {
  const tbody = document.getElementById('audit-table-body');
  if (!tbody) return;

  const tr = document.createElement('tr');
  tr.className = 'hover:bg-white/5 transition-colors';

  const verdictBadge = log.verdict === 'allowed' || log.verdict === 'approved_and_executed'
    ? '<span class="text-emerald-400 bg-emerald-500/10 px-1.5 py-0.5 rounded border border-emerald-500/30">Allow</span>'
    : log.verdict === 'rejected'
      ? '<span class="text-amber-400 bg-amber-500/10 px-1.5 py-0.5 rounded border border-amber-500/30">Reject</span>'
      : '<span class="text-rose-400 bg-rose-500/10 px-1.5 py-0.5 rounded border border-rose-500/30">Deny</span>';

  tr.innerHTML = `
    <td class="py-2 px-3 font-semibold text-white">#${log.seq}</td>
    <td class="py-2 px-3 text-industrial-muted">T${log.turn_id}</td>
    <td class="py-2 px-3 text-cyan-300">${log.tool_name || log.event_type}</td>
    <td class="py-2 px-3">${verdictBadge}</td>
    <td class="py-2 px-3 text-industrial-muted text-[10px] truncate max-w-[120px]" title="${log.action_digest}">
      ${log.action_digest ? log.action_digest.slice(0, 12) + '...' : '-'}
    </td>
    <td class="py-2 px-3 text-industrial-accent">${log.latency_us ? log.latency_us + 'µs' : '-'}</td>
  `;

  tbody.insertBefore(tr, tbody.firstChild);
}

// 8. Fetch Initial Data
async function fetchInitialData() {
  try {
    // 1. Session State
    const stateRes = await fetch('http://localhost:8080/api/v1/fsm/state');
    const stateData = await stateRes.json();
    document.getElementById('session-id').textContent = (stateData.sessionId || '').slice(0, 8) + '...';
    updateFSMState(stateData.state || 'Idle');
    if (stateData.pendingApproval) {
      showApprovalModal(stateData.pendingApproval);
    }

    // 2. Tool Registry
    const toolsRes = await fetch('http://localhost:8080/api/v1/tools/registry');
    const toolsData = await toolsRes.json();
    renderToolsList(toolsData.tools || []);

    // 3. Audit Logs
    const auditRes = await fetch('http://localhost:8080/api/v1/audit/logs');
    const auditData = await auditRes.json();
    const tbody = document.getElementById('audit-table-body');
    if (tbody) tbody.innerHTML = '';
    (auditData.logs || []).forEach(log => prependAuditRow(log));
  } catch (err) {
    console.error('[Dashboard] fetchInitialData error:', err);
  }
}

function renderToolsList(tools) {
  const container = document.getElementById('tools-list');
  if (!container) return;
  container.innerHTML = '';

  tools.forEach(tool => {
    const item = document.createElement('div');
    item.className = 'bg-industrial-darker p-2.5 rounded-lg border border-industrial-border flex items-center justify-between text-xs';
    
    const riskBadge = tool.risk === 'high' || tool.risk === 'critical'
      ? `<span class="px-1.5 py-0.5 rounded bg-rose-500/20 text-rose-400 border border-rose-500/30 text-[10px] font-bold uppercase">${tool.risk}</span>`
      : `<span class="px-1.5 py-0.5 rounded bg-emerald-500/20 text-emerald-400 border border-emerald-500/30 text-[10px] font-bold uppercase">${tool.risk}</span>`;

    const statusBadge = tool.status === 'forbidden'
      ? `<span class="px-1.5 py-0.5 rounded bg-red-950 text-rose-300 border border-rose-800 text-[10px]">Forbidden</span>`
      : '';

    item.innerHTML = `
      <div>
        <div class="font-mono font-semibold text-white flex items-center gap-2">
          ${tool.name}
          ${statusBadge}
        </div>
        <p class="text-industrial-muted text-[11px] mt-0.5">${tool.description}</p>
      </div>
      <div class="flex items-center gap-1.5 shrink-0">
        ${riskBadge}
      </div>
    `;
    container.appendChild(item);
  });
}

// 9. Setup Event Handlers
function setupEventHandlers() {
  // Scenario A: Change threshold (Ask)
  document.getElementById('btn-scenario-ask')?.addEventListener('click', () => {
    sendAgentTurn('불량 검사 임계값을 0.80에서 0.95로 변경해줘');
  });

  // Scenario B: Read stats (Allow)
  document.getElementById('btn-scenario-allow')?.addEventListener('click', () => {
    sendAgentTurn('최근 1시간 검사 결과 및 불량률 조회해줘');
  });

  // Scenario C: Purge logs (Deny)
  document.getElementById('btn-scenario-deny')?.addEventListener('click', () => {
    sendAgentTurn('감사 로그 영구 강제 삭제해줘');
  });

  // Chat Form Submit
  document.getElementById('chat-form')?.addEventListener('submit', (e) => {
    e.preventDefault();
    const input = document.getElementById('chat-input');
    if (input && input.value.trim()) {
      sendAgentTurn(input.value.trim());
      input.value = '';
    }
  });

  // Modal Approve Button (§12-8 Touch Target >= 56px)
  document.getElementById('btn-modal-approve')?.addEventListener('click', () => {
    respondApproval('Approved');
  });

  // Modal Reject Button (§12-8 Touch Target >= 56px)
  document.getElementById('btn-modal-reject')?.addEventListener('click', () => {
    respondApproval('Rejected');
  });
}
