import http from 'http';
import { spawn } from 'child_process';
import fs from 'fs';
import path from 'path';

const CHROME_PATH = fs.existsSync('C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe')
  ? 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe'
  : 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';

const CDP_PORT = 9222;
const TARGET_URL = 'http://localhost:8080/';

async function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function runBrowserVerification() {
  console.log('================================================================');
  console.log(' Cogito++ Antigravity Browser Verification Suite');
  console.log(' - G0-33: Strict CSP Verification (Zero Violations)');
  console.log(' - §12-8: Industrial HMI Layout & Touch Target Verification');
  console.log(' - Target Viewport: 1024 x 768 (No Scroll Authority Area)');
  console.log('================================================================\n');

  console.log(`[1/5] Launching Headless Chrome (${CHROME_PATH})...`);
  const chromeProcess = spawn(CHROME_PATH, [
    '--headless=new',
    `--remote-debugging-port=${CDP_PORT}`,
    '--window-size=1024,768',
    '--hide-scrollbars',
    '--disable-gpu',
    '--no-sandbox',
    '--user-data-dir=' + path.resolve('./.chrome_test_profile')
  ]);

  // Wait for CDP to be ready
  let wsUrl = null;
  for (let i = 0; i < 30; i++) {
    await sleep(300);
    try {
      const res = await fetch(`http://localhost:${CDP_PORT}/json`);
      const tabs = await res.json();
      if (tabs && tabs.length > 0 && tabs[0].webSocketDebuggerUrl) {
        wsUrl = tabs[0].webSocketDebuggerUrl;
        break;
      }
    } catch (e) {}
  }

  if (!wsUrl) {
    chromeProcess.kill();
    throw new Error('Failed to connect to Chrome DevTools Protocol port 9222');
  }

  console.log(`[2/5] Connected to CDP Target: ${wsUrl}`);

  const ws = new globalThis.WebSocket(wsUrl);
  let messageId = 1;
  const pendingRequests = new Map();
  const consoleMessages = [];
  const cspViolations = [];

  function sendCommand(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = messageId++;
      pendingRequests.set(id, { resolve, reject, method });
      ws.send(JSON.stringify({ id, method, params }));
    });
  }

  await new Promise((resolve, reject) => {
    ws.onopen = resolve;
    ws.onerror = reject;
  });

  ws.onmessage = (event) => {
    const rawStr = typeof event.data === 'string' ? event.data : event.data.toString();
    try {
      const msg = JSON.parse(rawStr);
      if (msg.id && pendingRequests.has(msg.id)) {
        const { resolve, reject } = pendingRequests.get(msg.id);
        pendingRequests.delete(msg.id);
        if (msg.error) reject(msg.error);
        else resolve(msg.result);
      }

      if (msg.method === 'Console.messageAdded') {
        const text = msg.params.message.text;
        consoleMessages.push(text);
        if (text.toLowerCase().includes('content security policy') || text.toLowerCase().includes('csp') || text.toLowerCase().includes('violat')) {
          cspViolations.push(text);
        }
      }

      if (msg.method === 'Log.entryAdded') {
        const entry = msg.params.entry;
        if (entry.source === 'security' || entry.text.includes('Content Security Policy')) {
          cspViolations.push(entry.text);
        }
      }
    } catch (err) {
      console.error('Error handling CDP message:', err);
    }
  };

  // Enable CDP domains
  await sendCommand('Page.enable');
  await sendCommand('Console.enable');
  await sendCommand('Log.enable');
  await sendCommand('Runtime.enable');
  await sendCommand('Emulation.setDeviceMetricsOverride', {
    width: 1024,
    height: 768,
    deviceScaleFactor: 1,
    mobile: false
  });

  console.log(`[3/5] Navigating to ${TARGET_URL} at 1024x768 viewport...`);
  await sendCommand('Page.navigate', { url: TARGET_URL });
  await sleep(1500);

  // Trigger Scenario A: Change inspection threshold (Ask)
  console.log('[4/5] Triggering Decision::Ask scenario (Inspection Threshold Change)...');
  await sendCommand('Runtime.evaluate', {
    expression: `(() => {
      const btn = document.getElementById('btn-scenario-ask');
      if (btn) btn.click();
    })()`
  });
  await sleep(2000); // Wait for WebSocket event & modal rendering

  // Measure §12-8 metrics in DOM
  const evalResult = await sendCommand('Runtime.evaluate', {
    expression: `(() => {
      const modal = document.getElementById('approval-modal');
      const card = document.getElementById('approval-authority-card');
      const btnApprove = document.getElementById('btn-modal-approve');
      const btnReject = document.getElementById('btn-modal-reject');

      const isModalVisible = modal ? !modal.classList.contains('hidden') : false;
      const cardRect = card ? card.getBoundingClientRect() : { width: 0, height: 0 };
      const approveRect = btnApprove ? btnApprove.getBoundingClientRect() : { width: 0, height: 0, left: 0 };
      const rejectRect = btnReject ? btnReject.getBoundingClientRect() : { width: 0, height: 0, right: 0 };

      const buttonGap = approveRect.left - rejectRect.right;
      const viewportHeight = window.innerHeight;
      const viewportWidth = window.innerWidth;
      const cardFitsInViewport = cardRect.height <= viewportHeight;

      return {
        isModalVisible,
        viewport: { width: viewportWidth, height: viewportHeight },
        card: {
          width: cardRect.width,
          height: cardRect.height,
          scrollHeight: card ? card.scrollHeight : 0,
          clientHeight: card ? card.clientHeight : 0,
          fitsInViewport: cardFitsInViewport
        },
        approveBtn: {
          width: approveRect.width,
          height: approveRect.height
        },
        rejectBtn: {
          width: rejectRect.width,
          height: rejectRect.height
        },
        buttonGap
      };
    })()`,
    returnByValue: true
  });

  const metrics = evalResult.result.value;

  // Capture full screenshot at 1024x768
  const screenshotData = await sendCommand('Page.captureScreenshot', { format: 'png' });
  const screenshotBuffer = Buffer.from(screenshotData.data, 'base64');
  const screenshotPath = path.resolve('tests/web/screenshot_1024x768.png');
  fs.writeFileSync(screenshotPath, screenshotBuffer);
  console.log(`[5/5] Full screenshot captured: ${screenshotPath}\n`);

  // Close browser
  ws.close();
  chromeProcess.kill();

  // Print Verification Report
  console.log('================================================================');
  console.log('                VERIFICATION REPORT & METRICS                   ');
  console.log('================================================================');

  let passed = true;

  // Metric 1: G0-33 CSP Violations
  console.log(`\n1. G0-33 Content Security Policy Compliance:`);
  console.log(`   - Policy: style-src 'self' (No unsafe-inline)`);
  console.log(`   - Console Log Count: ${consoleMessages.length}`);
  console.log(`   - CSP Violations Count: ${cspViolations.length}`);
  if (cspViolations.length === 0) {
    console.log(`   [PASS] Zero CSP Violations detected! (Pure SVG & Class-based styling verified)`);
  } else {
    passed = false;
    console.error(`   [FAIL] CSP Violations found:`, cspViolations);
  }

  // Metric 2: 1024x768 Viewport & Authority Area Layout
  console.log(`\n2. §12-8 Viewport & Authority Area (1024x768 No-Scroll):`);
  console.log(`   - Viewport: ${metrics.viewport.width} x ${metrics.viewport.height}`);
  console.log(`   - Modal Visible: ${metrics.isModalVisible}`);
  console.log(`   - Authority Card Dimensions: ${Math.round(metrics.card.width)}px (W) x ${Math.round(metrics.card.height)}px (H)`);
  console.log(`   - Card Fits in 768px Viewport: ${metrics.card.fitsInViewport}`);
  console.log(`   - Scroll Height vs Client Height: ${metrics.card.scrollHeight}px vs ${metrics.card.clientHeight}px`);
  if (metrics.isModalVisible && metrics.card.fitsInViewport && metrics.card.scrollHeight <= metrics.card.clientHeight) {
    console.log(`   [PASS] Authority Area fits completely within 1024x768 without vertical scroll!`);
  } else {
    passed = false;
    console.error(`   [FAIL] Authority Area exceeds 768px or requires vertical scrolling`);
  }

  // Metric 3: Touch Target Sizes (>= 56px)
  console.log(`\n3. §12-8 Touch Target Sizes (Requirement: >= 56px):`);
  console.log(`   - Approve Button: ${Math.round(metrics.approveBtn.width)}px (W) x ${Math.round(metrics.approveBtn.height)}px (H)`);
  console.log(`   - Reject Button:  ${Math.round(metrics.rejectBtn.width)}px (W) x ${Math.round(metrics.rejectBtn.height)}px (H)`);
  const touchTargetPass = metrics.approveBtn.height >= 56 && metrics.rejectBtn.height >= 56 &&
                          metrics.approveBtn.width >= 56 && metrics.rejectBtn.width >= 56;
  if (touchTargetPass) {
    console.log(`   [PASS] Both buttons exceed minimum 56px touch target requirement!`);
  } else {
    passed = false;
    console.error(`   [FAIL] Touch target is smaller than 56px`);
  }

  // Metric 4: Button Separation Gap (>= 24px)
  console.log(`\n4. §12-8 Button Separation (Requirement: >= 24px):`);
  console.log(`   - Measured Separation Gap: ${Math.round(metrics.buttonGap)}px`);
  if (metrics.buttonGap >= 24) {
    console.log(`   [PASS] Separation gap is >= 24px (Prevents operator mis-touch)!`);
  } else {
    passed = false;
    console.error(`   [FAIL] Separation gap is less than 24px`);
  }

  console.log('\n================================================================');
  if (passed) {
    console.log(' 🎉 ALL EXIT GATES PASSED (G0-33 & §12-8 VERIFIED) 🎉');
  } else {
    console.error(' ❌ VERIFICATION FAILED');
    process.exit(1);
  }
  console.log('================================================================\n');
}

runBrowserVerification().catch(err => {
  console.error('Test Execution Error:', err);
  process.exit(1);
});
