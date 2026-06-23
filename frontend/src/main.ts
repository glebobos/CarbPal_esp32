interface DeviceInfo {
  ssid: string;
  ip: string;
  clients: number;
  heap_free: number;
  uptime_s: number;
  idf_version: string;
  chip: string;
}

const elements = {
  ssid: document.getElementById('val-ssid'),
  ip: document.getElementById('val-ip'),
  clients: document.getElementById('val-clients'),
  heap: document.getElementById('val-heap'),
  uptime: document.getElementById('val-uptime'),
  chip: document.getElementById('val-chip'),
  idf: document.getElementById('val-idf'),
  statusBadge: document.getElementById('status-badge'),
  statusText: document.getElementById('status-text'),
  btnRefresh: document.getElementById('btn-refresh')
};

function formatUptime(seconds: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  if (h > 0) return `${h}h ${m}m`;
  if (m > 0) return `${m}m ${s}s`;
  return `${s}s`;
}

function formatHeap(bytes: number): string {
  return `${(bytes / 1024).toFixed(1)} KB`;
}

async function fetchTelemetry() {
  // Update UI to connecting state
  if (elements.statusBadge && elements.statusText) {
    elements.statusBadge.className = 'status-badge connecting';
    elements.statusText.textContent = 'Refreshing...';
  }

  try {
    const response = await fetch('/api/info');
    if (!response.ok) {
      throw new Error(`HTTP error! status: ${response.status}`);
    }
    const data: DeviceInfo = await response.json();

    // Populate data
    if (elements.ssid) elements.ssid.textContent = data.ssid || 'N/A';
    if (elements.ip) elements.ip.textContent = data.ip || 'N/A';
    if (elements.clients) elements.clients.textContent = data.clients.toString();
    if (elements.heap) elements.heap.textContent = formatHeap(data.heap_free);
    if (elements.uptime) elements.uptime.textContent = formatUptime(data.uptime_s);
    if (elements.chip) elements.chip.textContent = data.chip || 'N/A';
    if (elements.idf) elements.idf.textContent = data.idf_version || 'N/A';

    // Set connected status
    if (elements.statusBadge && elements.statusText) {
      elements.statusBadge.className = 'status-badge connected';
      elements.statusText.textContent = 'Connected';
    }
  } catch (error) {
    console.error('Error fetching telemetry:', error);
    
    // Set fallback display
    if (elements.ssid) elements.ssid.textContent = 'ESP32-Portal';
    if (elements.ip) elements.ip.textContent = '192.168.4.1';
    if (elements.clients) elements.clients.textContent = '0';
    if (elements.heap) elements.heap.textContent = 'Offline';
    if (elements.uptime) elements.uptime.textContent = 'Offline';
    if (elements.chip) elements.chip.textContent = 'ESP32-C3';
    if (elements.idf) elements.idf.textContent = 'v5.4.4';

    // Set error status
    if (elements.statusBadge && elements.statusText) {
      elements.statusBadge.className = 'status-badge error';
      elements.statusText.textContent = 'Offline';
    }
  }
}

// Initial fetch
document.addEventListener('DOMContentLoaded', fetchTelemetry);

// Refresh button listener
if (elements.btnRefresh) {
  elements.btnRefresh.addEventListener('click', fetchTelemetry);
}
