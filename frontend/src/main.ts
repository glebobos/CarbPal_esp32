// TypeScript Controller for Carb Sync

interface CarbData {
  v1: number;
  v2: number;
  v3: number;
  v4: number;
}

// State
let currentView: '3-lines' | 'gauges' = '3-lines';
let activeSyncTargets = { left: false, right: false, mid: false };
let isConnected = false;
let isCalModalOpen = false;

// Calibration Offsets (v_calibrated = v_raw - offset)
let offsets = [0.0, 0.0, 0.0];

// Raw and Calibrated values
let rawValues: CarbData = { v1: 0.0, v2: 0.0, v3: 0.0, v4: 0.0 };
let calValues: CarbData = { v1: 0.0, v2: 0.0, v3: 0.0, v4: 0.0 };

// Oscilloscope History buffers (up to 200 samples)
const BUFFER_SIZE = 200;
const historyCh1: number[] = Array(BUFFER_SIZE).fill(0); // Diff 1-2
const historyCh2: number[] = Array(BUFFER_SIZE).fill(0); // Diff 3-4
const historyCh3: number[] = Array(BUFFER_SIZE).fill(0); // Diff (1+2)/2 - (3+4)/2



// Load calibration from localStorage
const storedOffsets = localStorage.getItem('carb_offsets');
if (storedOffsets) {
  try {
    offsets = JSON.parse(storedOffsets);
  } catch (e) {
    console.error('Error loading calibration offsets:', e);
  }
}

// DOM Elements
const elements = {
  statusBadge: document.getElementById('status-badge'),
  statusText: document.getElementById('status-text'),
  calBtn: document.getElementById('cal-btn'),
  
  // View Modes tabs
  tab3Lines: document.getElementById('tab-3-lines'),
  tabGauges: document.getElementById('tab-gauges'),
  
  // Sync Selection
  syncSection: document.getElementById('sync-section'),
  syncLeft: document.getElementById('sync-left'),
  syncRight: document.getElementById('sync-right'),
  syncMid: document.getElementById('sync-mid'),
  
  // Visualizer Card Containers
  visualizerCard: document.getElementById('visualizer-card'),
  chartsStacked: document.getElementById('charts-stacked-container'),
  gaugesSection: document.getElementById('gauges-section'),
  
  // Canvases
  canvasCh1: document.getElementById('canvas-ch1') as HTMLCanvasElement | null,
  canvasCh2: document.getElementById('canvas-ch2') as HTMLCanvasElement | null,
  canvasCh3: document.getElementById('canvas-ch3') as HTMLCanvasElement | null,
  wrapperCh1: document.getElementById('wrapper-ch1'),
  wrapperCh2: document.getElementById('wrapper-ch2'),
  wrapperCh3: document.getElementById('wrapper-ch3'),
  
  // Gauges
  dialVal1: document.getElementById('dial-val-1'),
  dialVal2: document.getElementById('dial-val-2'),
  dialVal3: document.getElementById('dial-val-3'),
  dialVal4: document.getElementById('dial-val-4'),
  dialFill1: document.getElementById('dial-fill-1'),
  dialFill2: document.getElementById('dial-fill-2'),
  dialFill3: document.getElementById('dial-fill-3'),
  dialFill4: document.getElementById('dial-fill-4'),
  needle1: document.getElementById('needle-1'),
  needle2: document.getElementById('needle-2'),
  needle3: document.getElementById('needle-3'),
  needle4: document.getElementById('needle-4'),
  
  // Interactive Manifold bodies
  sensorRing1: document.getElementById('sensor-ring-1'),
  sensorRing2: document.getElementById('sensor-ring-2'),
  sensorRing3: document.getElementById('sensor-ring-3'),
  sensorRing4: document.getElementById('sensor-ring-4'),
  
  // Modal Elements
  calModal: document.getElementById('cal-modal'),
  calClose: document.getElementById('cal-close'),
  calRaw1: document.getElementById('cal-raw-1'),
  calRaw2: document.getElementById('cal-raw-2'),
  calRaw3: document.getElementById('cal-raw-3'),
  calOffset1: document.getElementById('cal-offset-1'),
  calOffset2: document.getElementById('cal-offset-2'),
  calOffset3: document.getElementById('cal-offset-3'),
  calResetDefaults: document.getElementById('cal-reset-defaults'),
  calZeroBtn: document.getElementById('cal-zero-btn'),
  helpBtn: document.getElementById('help-btn'),
  helpModal: document.getElementById('help-modal'),
  helpClose: document.getElementById('help-close'),
  helpOkBtn: document.getElementById('help-ok-btn'),
};

// Canvas context references
let ctxCh1: CanvasRenderingContext2D | null = null;
let ctxCh2: CanvasRenderingContext2D | null = null;
let ctxCh3: CanvasRenderingContext2D | null = null;

// Initialize Canvases
function initCanvases() {
  const resizeCanvas = (canvas: HTMLCanvasElement | null) => {
    if (!canvas) return null;
    const rect = canvas.getBoundingClientRect();
    canvas.width = rect.width * window.devicePixelRatio;
    canvas.height = rect.height * window.devicePixelRatio;
    const ctx = canvas.getContext('2d');
    if (ctx) {
      ctx.scale(window.devicePixelRatio, window.devicePixelRatio);
    }
    return ctx;
  };

  if (elements.canvasCh1) ctxCh1 = resizeCanvas(elements.canvasCh1);
  if (elements.canvasCh2) ctxCh2 = resizeCanvas(elements.canvasCh2);
  if (elements.canvasCh3) ctxCh3 = resizeCanvas(elements.canvasCh3);
}

// Window resize listener
window.addEventListener('resize', () => {
  initCanvases();
});

// Update individual chart wrappers visibility
function updateChartVisibility() {
  const anySelected = activeSyncTargets.left || activeSyncTargets.right || activeSyncTargets.mid;
  
  const showCh1 = !anySelected || activeSyncTargets.left;
  const showCh2 = !anySelected || activeSyncTargets.right;
  const showCh3 = !anySelected || activeSyncTargets.mid;
  
  if (elements.wrapperCh1) {
    if (showCh1) elements.wrapperCh1.classList.remove('hidden');
    else elements.wrapperCh1.classList.add('hidden');
  }
  if (elements.wrapperCh2) {
    if (showCh2) elements.wrapperCh2.classList.remove('hidden');
    else elements.wrapperCh2.classList.add('hidden');
  }
  if (elements.wrapperCh3) {
    if (showCh3) elements.wrapperCh3.classList.remove('hidden');
    else elements.wrapperCh3.classList.add('hidden');
  }
}

// View Toggle Handler
function setView(view: '3-lines' | 'gauges') {
  currentView = view;
  
  // Update Tab States
  [elements.tab3Lines, elements.tabGauges].forEach(tab => {
    if (tab) tab.classList.remove('active');
  });
  
  if (view === '3-lines' && elements.tab3Lines) elements.tab3Lines.classList.add('active');
  if (view === 'gauges' && elements.tabGauges) elements.tabGauges.classList.add('active');
  
  // Show/Hide Containers
  if (view === '3-lines') {
    if (elements.syncSection) elements.syncSection.classList.remove('hidden');
    if (elements.visualizerCard) elements.visualizerCard.classList.remove('hidden');
    if (elements.chartsStacked) elements.chartsStacked.classList.remove('hidden');
    if (elements.gaugesSection) elements.gaugesSection.classList.add('hidden');
    updateChartVisibility();
  } else {
    // gauges mode
    if (elements.syncSection) elements.syncSection.classList.add('hidden');
    if (elements.visualizerCard) elements.visualizerCard.classList.add('hidden');
    if (elements.gaugesSection) elements.gaugesSection.classList.remove('hidden');
  }
  
  // Re-init sizes just in case container visibility changed
  setTimeout(initCanvases, 50);
}

// Sync mode toggle handler (For single-select Tuner focus, toggles off if clicked active)
function toggleSyncTarget(target: 'left' | 'right' | 'mid') {
  const wasActive = activeSyncTargets[target];
  
  // Reset all targets
  activeSyncTargets = { left: false, right: false, mid: false };
  
  // Toggle the clicked target if it wasn't already active
  if (!wasActive) {
    activeSyncTargets[target] = true;
  }
  
  const updateBtnActive = (btn: HTMLElement | null, active: boolean) => {
    if (!btn) return;
    if (active) btn.classList.add('active');
    else btn.classList.remove('active');
  };
  
  updateBtnActive(elements.syncLeft, activeSyncTargets.left);
  updateBtnActive(elements.syncRight, activeSyncTargets.right);
  updateBtnActive(elements.syncMid, activeSyncTargets.mid);
  
  updateChartVisibility();
  updateEngineHighlights();
  
  // Re-initialize canvases to handle viewport layout updates
  setTimeout(initCanvases, 50);
}

// Event Listeners for tabs
elements.tab3Lines?.addEventListener('click', () => setView('3-lines'));
elements.tabGauges?.addEventListener('click', () => setView('gauges'));

elements.syncLeft?.addEventListener('click', () => toggleSyncTarget('left'));
elements.syncRight?.addEventListener('click', () => toggleSyncTarget('right'));
elements.syncMid?.addEventListener('click', () => toggleSyncTarget('mid'));



// Modal Actions
elements.calBtn?.addEventListener('click', () => {
  isCalModalOpen = true;
  elements.calModal?.classList.remove('hidden');
});

const closeModal = () => {
  isCalModalOpen = false;
  elements.calModal?.classList.add('hidden');
};

elements.calClose?.addEventListener('click', closeModal);
elements.calModal?.addEventListener('click', (e) => {
  if (e.target === elements.calModal) closeModal();
});

elements.calZeroBtn?.addEventListener('click', () => {
  // Capture current raw values as new calibration offsets
  offsets = [rawValues.v1, rawValues.v2, rawValues.v3];
  localStorage.setItem('carb_offsets', JSON.stringify(offsets));
  closeModal();
});

elements.calResetDefaults?.addEventListener('click', () => {
  offsets = [0.0, 0.0, 0.0];
  localStorage.setItem('carb_offsets', JSON.stringify(offsets));
  closeModal();
});

// Help Modal Actions
elements.helpBtn?.addEventListener('click', () => {
  elements.helpModal?.classList.remove('hidden');
});

const closeHelpModal = () => {
  elements.helpModal?.classList.add('hidden');
};

elements.helpClose?.addEventListener('click', closeHelpModal);
elements.helpOkBtn?.addEventListener('click', closeHelpModal);
elements.helpModal?.addEventListener('click', (e) => {
  if (e.target === elements.helpModal) closeHelpModal();
});

// Update indicators on the Carburetor SVG block
function updateEngineHighlights() {
  const setIndicatorState = (el: HTMLElement | null, state: string) => {
    if (!el) return;
    el.removeAttribute('class');
    el.classList.add('sensor-indicator');
    if (state) {
      el.classList.add(state);
    }
  };

  if (!isConnected) {
    setIndicatorState(elements.sensorRing1, 'inactive');
    setIndicatorState(elements.sensorRing2, 'inactive');
    setIndicatorState(elements.sensorRing3, 'inactive');
    setIndicatorState(elements.sensorRing4, 'inactive');
    return;
  }

  const diffLeft = Math.abs(calValues.v1);
  const diffRight = Math.abs(calValues.v2);
  const diffMid = Math.abs(calValues.v3);

  const getStatusClass = (diff: number) => {
    if (diff < 1.0) return 'balanced';
    if (diff < 2.5) return 'warning';
    return 'error';
  };

  const statusL = getStatusClass(diffLeft);
  const statusR = getStatusClass(diffRight);
  const statusM = getStatusClass(diffMid);

  const anySelected = activeSyncTargets.left || activeSyncTargets.right || activeSyncTargets.mid;

  // Left Pair (Cylinders 1 & 2)
  if (!anySelected) {
    setIndicatorState(elements.sensorRing1, statusL);
    setIndicatorState(elements.sensorRing2, statusL);
  } else if (activeSyncTargets.left) {
    setIndicatorState(elements.sensorRing1, statusL);
    setIndicatorState(elements.sensorRing2, statusL);
  } else if (activeSyncTargets.mid) {
    setIndicatorState(elements.sensorRing1, statusM);
    setIndicatorState(elements.sensorRing2, statusM);
  } else {
    setIndicatorState(elements.sensorRing1, 'inactive');
    setIndicatorState(elements.sensorRing2, 'inactive');
  }

  // Right Pair (Cylinders 3 & 4)
  if (!anySelected) {
    setIndicatorState(elements.sensorRing3, statusR);
    setIndicatorState(elements.sensorRing4, statusR);
  } else if (activeSyncTargets.right) {
    setIndicatorState(elements.sensorRing3, statusR);
    setIndicatorState(elements.sensorRing4, statusR);
  } else if (activeSyncTargets.mid) {
    setIndicatorState(elements.sensorRing3, statusM);
    setIndicatorState(elements.sensorRing4, statusM);
  } else {
    setIndicatorState(elements.sensorRing3, 'inactive');
    setIndicatorState(elements.sensorRing4, 'inactive');
  }
}

// Fetch Telemetry from ESP32
async function fetchTelemetry() {
  try {
    const res = await fetch('/api/data');
    if (!res.ok) throw new Error('API Offline');
    const data: CarbData = await res.json();
    
    rawValues = data;
    isConnected = true;
    
    if (elements.statusBadge && elements.statusText) {
      elements.statusBadge.className = 'status-badge connected';
      elements.statusText.textContent = 'Подключено';
    }
  } catch (e) {
    isConnected = false;
    rawValues = { v1: 0, v2: 0, v3: 0, v4: 0 };
    if (elements.statusBadge && elements.statusText) {
      elements.statusBadge.className = 'status-badge disconnected';
      elements.statusText.textContent = 'Отсутствует подключение';
    }
  }
}

// Calculate differences and update history buffers
function updateHistoryAndTelemetry() {
  if (isConnected) {
    // Compute calibrated values
    calValues = {
      v1: rawValues.v1 - offsets[0],
      v2: rawValues.v2 - offsets[1],
      v3: rawValues.v3 - offsets[2],
      v4: 0,
    };
  } else {
    // Flat-line at center/0
    calValues = { v1: 0, v2: 0, v3: 0, v4: 0 };
  }



  // In differential mode, the channels are already the calibrated sensor values
  const ch1Diff = calValues.v1;
  const ch2Diff = calValues.v2;
  const ch3Diff = calValues.v3;

  historyCh1.push(ch1Diff);
  historyCh1.shift();
  historyCh2.push(ch2Diff);
  historyCh2.shift();
  historyCh3.push(ch3Diff);
  historyCh3.shift();

  // Update text elements on UI
  const updateValueEl = (valEl: HTMLElement | null, fillEl: HTMLElement | null, needleEl: HTMLElement | null, val: number) => {
    if (valEl) valEl.textContent = val.toFixed(1);
    
    // Constrain val for visual gauges representation (map -50..+50 to 0..100)
    const percentage = (val + 50);
    const visualVal = Math.max(0, Math.min(100, percentage));
    
    // Dial progress bar (SVG circumference is approx 188.5)
    if (fillEl) {
      const strokeOffset = 188.5 - (visualVal / 100) * 188.5;
      fillEl.setAttribute('stroke-dashoffset', strokeOffset.toString());
    }

    // Dial Needle angle: -120deg to 120deg
    if (needleEl) {
      const angle = -120 + (visualVal / 100) * 240;
      needleEl.style.transform = `rotate(${angle}deg)`;
    }
  };

  // Reconstruct fictitious individual cylinder pressures from the 3 differential values
  const dial1Val = (calValues.v3 + calValues.v1) / 2;
  const dial2Val = (calValues.v3 - calValues.v1) / 2;
  const dial3Val = -(calValues.v3 - calValues.v2) / 2;
  const dial4Val = -(calValues.v3 + calValues.v2) / 2;

  updateValueEl(elements.dialVal1, elements.dialFill1, elements.needle1, dial1Val);
  updateValueEl(elements.dialVal2, elements.dialFill2, elements.needle2, dial2Val);
  updateValueEl(elements.dialVal3, elements.dialFill3, elements.needle3, dial3Val);
  updateValueEl(elements.dialVal4, elements.dialFill4, elements.needle4, dial4Val);

  // Update calibration modal texts if open
  if (isCalModalOpen) {
    if (elements.calRaw1) elements.calRaw1.textContent = `${rawValues.v1.toFixed(2)} kPa`;
    if (elements.calRaw2) elements.calRaw2.textContent = `${rawValues.v2.toFixed(2)} kPa`;
    if (elements.calRaw3) elements.calRaw3.textContent = `${rawValues.v3.toFixed(2)} kPa`;
    
    if (elements.calOffset1) elements.calOffset1.textContent = offsets[0].toFixed(2);
    if (elements.calOffset2) elements.calOffset2.textContent = offsets[1].toFixed(2);
    if (elements.calOffset3) elements.calOffset3.textContent = offsets[2].toFixed(2);
  }

  updateEngineHighlights();
}

// Plotting waveform on a Canvas context
function drawWaveform(
  ctx: CanvasRenderingContext2D | null, 
  canvas: HTMLCanvasElement | null, 
  history: number[], 
  title: string, 
  yRange: number, 
  color = '#39ff14'
) {
  if (!ctx || !canvas) return;
  
  const w = canvas.width / window.devicePixelRatio;
  const h = canvas.height / window.devicePixelRatio;
  
  // Clear and fill dark translucent background
  ctx.clearRect(0, 0, w, h);
  
  // Draw grid lines (10 horizontal lines)
  ctx.strokeStyle = 'rgba(57, 255, 20, 0.03)';
  ctx.lineWidth = 1;
  const gridLines = 8;
  for (let i = 1; i < gridLines; i++) {
    const y = (h / gridLines) * i;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }
  
  // Draw vertical division lines
  const gridCols = 10;
  for (let i = 1; i < gridCols; i++) {
    const x = (w / gridCols) * i;
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, h);
    ctx.stroke();
  }
  
  // Draw Center Zero reference line
  ctx.strokeStyle = 'rgba(57, 255, 20, 0.15)';
  ctx.lineWidth = 1;
  ctx.setLineDash([4, 4]);
  ctx.beginPath();
  ctx.moveTo(w / 2, 0);
  ctx.lineTo(w / 2, h);
  ctx.stroke();
  ctx.setLineDash([]);
  
  // Draw Oscilloscope waveform line
  ctx.beginPath();
  ctx.strokeStyle = color;
  ctx.lineWidth = 2.5;
  ctx.shadowColor = color;
  ctx.shadowBlur = 8;
  
  const len = history.length;
  for (let i = 0; i < len; i++) {
    const val = history[i];
    // Map value: center is w/2. Range is from -yRange to +yRange on X axis.
    const x = w / 2 + (val / yRange) * (w / 2);
    // Map time: newest value (i = len - 1) at y = 0, oldest (i = 0) at y = h.
    const y = (h / (len - 1)) * (len - 1 - i);
    
    if (i === 0) {
      ctx.moveTo(x, y);
    } else {
      ctx.lineTo(x, y);
    }
  }
  ctx.stroke();
  ctx.shadowBlur = 0; // reset shadow
  
  // Draw title in corner
  ctx.fillStyle = 'rgba(255, 255, 255, 0.35)';
  ctx.font = '500 9px Inter, sans-serif';
  ctx.fillText(title, 8, 16);
}



// 60FPS Render Loop
function renderLoop() {
  if (currentView === '3-lines') {
    drawWaveform(ctxCh1, elements.canvasCh1, historyCh1, '', 20, '#39ff14');
    drawWaveform(ctxCh2, elements.canvasCh2, historyCh2, '', 20, '#39ff14');
    drawWaveform(ctxCh3, elements.canvasCh3, historyCh3, '', 20, '#39ff14');
  }

  requestAnimationFrame(renderLoop);
}

// Setup background poller & local simulation update speed
setInterval(async () => {
  await fetchTelemetry();
  updateHistoryAndTelemetry();
}, 100); // 10Hz updates matching professional scopes

// Startup initialization
document.addEventListener('DOMContentLoaded', () => {
  setView('3-lines');
  activeSyncTargets = { left: false, right: false, mid: false };
  updateChartVisibility();
  updateEngineHighlights();
  initCanvases();
  renderLoop();
});
