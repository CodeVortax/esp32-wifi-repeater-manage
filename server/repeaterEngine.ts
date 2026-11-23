import fs from 'fs';
import path from 'path';
import type {
  AppCategory,
  ChannelSpectrumMetric,
  DeviceRecord,
  GraceArchiveEntry,
  MonthlyPlanConfig,
  OptimizationFeatures,
  PermanentMonthlySummary,
  RepeaterNode,
  RepeaterSystemState,
  SystemErrorEntry,
  TelegramConfig,
  TimeSlicePoint,
} from '../src/types.ts';

const STORE_DIR = path.resolve(process.cwd(), 'data');
const STORE_FILE = path.join(STORE_DIR, 'repeater-store.json');

interface PersistedState extends RepeaterSystemState {
  connected: boolean;
  lastSyncedAt: string | null;
}

function defaultEmptyState(): PersistedState {
  const now = new Date();
  const cycleStart = new Date(now.getFullYear(), now.getMonth(), 1);
  const cycleEnd = new Date(now.getFullYear(), now.getMonth() + 1, 0, 23, 59, 59);

  return {
    connected: false,
    lastSyncedAt: null,
    firmwareVersion: '2.0.0-vortax-unified',
    hardwareTarget: 'ESP32 (4 MB Flash · factory @ 0x10000 + nvblock 512 KB)',
    liveBridgeUrl: '',
    nodes: [],
    spectrum: [],
    devices: [],
    plan: {
      planName: 'Monthly Plan',
      totalCapGB: 250,
      cycleStartDay: 1,
      defaultUserCapGB: 0,
      graceRetentionHours: 48,
      currentCycleStart: cycleStart.toISOString(),
      currentCycleEnd: cycleEnd.toISOString(),
      autoRolloverEnabled: true,
    },
    graceArchives: [],
    monthlySummaries: [],
    errorLogs: [],
    telegram: {
      configured: false,
      maskedToken: '',
      chatId: '',
      autoBackupOnRollover: true,
      scheduleHours: 24,
      encryptTelegramBackups: true,
      lastSentAt: null,
      lastStatus: 'Not configured.',
    },
    features: {
      autoChannelScan: true,
      qosLowLatencyLane: true,
      coalescedDashboardApi: true,
      staticBlockWearLeveling: true,
      brownoutTxStepDown: true,
      dnsResponseCache: true,
    },
    timeSlices: [],
  };
}

function validateBridgeUrl(raw: string): URL {
  let parsed: URL;
  try {
    parsed = new URL(raw.trim());
  } catch {
    throw new Error('Invalid ESP32 repeater URL. Example: http://192.168.4.1');
  }
  if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') {
    throw new Error('Only http:// or https:// URLs are supported.');
  }
  const host = parsed.hostname.toLowerCase();
  if (
    host === '169.254.169.254' ||
    host === 'metadata.google.internal' ||
    host === '127.0.0.1' ||
    host === 'localhost' ||
    host === '::1'
  ) {
    throw new Error('Loopback and cloud metadata addresses are blocked.');
  }
  return parsed;
}

class RepeaterEngine {
  private state: PersistedState;
  private telegramTokenSecret = '';
  private espSessionCookie = '';

  constructor() {
    this.state = this.loadFromDisk();
  }

  private loadFromDisk(): PersistedState {
    try {
      if (fs.existsSync(STORE_FILE)) {
        const raw = fs.readFileSync(STORE_FILE, 'utf-8');
        const parsed = JSON.parse(raw) as Partial<PersistedState>;
        return {
          ...defaultEmptyState(),
          ...parsed,
          connected: false,
        };
      }
    } catch {
      // Corrupt store falls back cleanly to empty state
    }
    return defaultEmptyState();
  }

  private saveToDisk() {
    try {
      fs.mkdirSync(STORE_DIR, { recursive: true });
      fs.writeFileSync(STORE_FILE, JSON.stringify(this.state, null, 2), 'utf-8');
    } catch (err) {
      this.pushErrorLog({
        subsystem: 'StaticBlock',
        severity: 'Warning',
        code: 'DISK_PERSIST_ERR',
        message: err instanceof Error ? err.message : 'Failed to persist local state',
        heapAtEventKB: 0,
        actionTaken: 'Kept active state in memory.',
      });
    }
  }

  getState(): PersistedState {
    return this.state;
  }

  async connectEsp32(urlInput: string, adminPassword?: string): Promise<PersistedState> {
    const parsed = validateBridgeUrl(urlInput);
    const base = `${parsed.protocol}//${parsed.host}`;

    // 1. Authenticate if password provided (matches CodeVortax /api/login)
    if (adminPassword && adminPassword.length > 0) {
      const loginResp = await fetch(`${base}/api/login`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded',
          'X-Requested-With': 'dashboard',
        },
        body: new URLSearchParams({ user: 'admin', pass: adminPassword }),
        signal: AbortSignal.timeout(5000),
      });
      if (!loginResp.ok) {
        throw new Error(`ESP32 login failed (HTTP ${loginResp.status})`);
      }
      const setCookie = loginResp.headers.get('set-cookie') || '';
      const sidMatch = setCookie.match(/sid=([^;]+)/);
      if (sidMatch) {
        this.espSessionCookie = `sid=${sidMatch[1]}`;
      }
    }

    this.state.liveBridgeUrl = base;
    await this.syncFromEsp32();
    return this.state;
  }

  disconnectEsp32(): PersistedState {
    this.espSessionCookie = '';
    this.state.connected = false;
    this.state.liveBridgeUrl = '';
    this.state.nodes = [];
    this.state.devices = [];
    this.state.spectrum = [];
    this.state.lastSyncedAt = null;
    this.saveToDisk();
    return this.state;
  }

  private espHeaders(isMutating = false): Record<string, string> {
    const headers: Record<string, string> = {};
    if (this.espSessionCookie) headers.Cookie = this.espSessionCookie;
    if (isMutating) headers['X-Requested-With'] = 'dashboard';
    return headers;
  }

  async syncFromEsp32(): Promise<PersistedState> {
    if (!this.state.liveBridgeUrl) {
      throw new Error('No ESP32 repeater URL configured.');
    }
    const base = this.state.liveBridgeUrl;

    // Prefer coalesced /api/snapshot (v2.0), fallback to /api/status + /api/sysinfo (v0.4.0)
    let statusJson: Record<string, unknown> | null = null;
    try {
      const snapResp = await fetch(`${base}/api/snapshot`, {
        headers: this.espHeaders(),
        signal: AbortSignal.timeout(5000),
      });
      if (snapResp.ok) {
        statusJson = (await snapResp.json()) as Record<string, unknown>;
      }
    } catch {
      // Fallback below
    }

    if (!statusJson) {
      const stResp = await fetch(`${base}/api/status`, {
        headers: this.espHeaders(),
        signal: AbortSignal.timeout(5000),
      });
      if (stResp.status === 401) {
        throw new Error('ESP32 authentication required. Enter dashboard password and reconnect.');
      }
      if (!stResp.ok) {
        throw new Error(`ESP32 returned HTTP ${stResp.status}`);
      }
      statusJson = (await stResp.json()) as Record<string, unknown>;
    }

    const sys = (statusJson.sys as Record<string, unknown>) || statusJson;
    const rawDevices = Array.isArray(statusJson.devices) ? statusJson.devices : [];

    const mappedDevices: DeviceRecord[] = rawDevices.map((raw, idx) => {
      const d = raw as Record<string, unknown>;
      const mac = String(d.mac || `00:00:00:00:00:${String(idx).padStart(2, '0')}`);
      const bytesToday = Number(d.bytesToday || 0);
      const mbToday = Number((bytesToday / (1024 * 1024)).toFixed(2));
      const mbMonth = Number(d.mb ?? (Number(d.bytesAllTime || bytesToday) / (1024 * 1024)).toFixed(2));

      return {
        id: mac,
        mac,
        name: String(d.name || `User ${d.userNo || idx + 1}`),
        ip: String(d.ip || ''),
        nodeId: 'esp32-primary',
        online: Boolean(d.online ?? true),
        whitelisted: Boolean(d.whitelisted),
        blocked: Boolean(d.blocked),
        latencyMs: Number(d.latencyMs || 0),
        signalDbm: Number(d.rssi || -60),
        monthlyUsedMB: mbMonth,
        monthlyCapMB: Number(d.cap ?? d.limitMB ?? 0),
        dailyUsedMB: mbToday,
        topApp: String(d.topApp || 'Unknown'),
        topCategory: 'Web & Search' as AppCategory,
        appBreakdownMB: {},
        speedDownKbps: Number(d.down ?? 0),
        speedUpKbps: Number(d.up ?? 0),
      };
    });

    const freeHeap = Number(sys.heap ?? 0);
    const minHeap = Number(sys.minHeap ?? freeHeap);
    const largestBlock = Number(sys.largestBlock ?? freeHeap);
    const ch = Number(sys.channel ?? 1);
    const rssi = Number(sys.rssi ?? 0);

    const rootNode: RepeaterNode = {
      id: 'esp32-primary',
      name: String(sys.apSsid || 'ESP32 Repeater'),
      role: 'Root Repeater',
      staSsid: String(sys.staSsid || ''),
      apSsid: String(sys.apSsid || ''),
      apIp: String(sys.apIp || base.replace(/^https?:\/\//, '')),
      staIp: String(sys.staIp || ''),
      channel: ch,
      bandwidthMHz: 20,
      txPowerDbm: [19.5, 17, 13, 8.5][Number(sys.txLevel ?? 0)] ?? 19.5,
      rssiDbm: rssi,
      latencyMs: Number(sys.latencyMs ?? 0),
      jitterMs: Number(sys.jitterMs ?? 0),
      packetLossPct: Number(sys.packetLossPct ?? 0),
      connectedClients: mappedDevices.filter((d) => d.online).length,
      freeHeapBytes: freeHeap,
      minFreeHeapBytes: minHeap,
      largestContigBlockBytes: largestBlock,
      staticBlockUsedBytes: Number(sys.staticUsed ?? 0),
      staticBlockTotalBytes: Number(sys.staticTotal ?? 524288),
      wdtResetsAvoided: Number(sys.faults ?? 0),
      uptimeSec: Number(sys.uptime ?? 0),
      qosFastPath: true,
      naptTurbo: true,
      psDisabled: true,
      status: rssi !== 0 && rssi <= -80 ? 'High Latency' : 'Optimal',
    };

    const rawSpectrum = Array.isArray(statusJson.spectrum)
      ? (statusJson.spectrum as ChannelSpectrumMetric[])
      : [];

    this.state.connected = true;
    this.state.lastSyncedAt = new Date().toISOString();
    this.state.firmwareVersion = String(sys.version || this.state.firmwareVersion);
    this.state.nodes = [rootNode];
    this.state.devices = mappedDevices;
    this.state.spectrum = rawSpectrum;

    // Record real 5-minute snapshot slice
    const now = Date.now();
    const bucket = Math.floor(now / 300000) * 300000;
    const totalMB = Number(mappedDevices.reduce((a, d) => a + d.dailyUsedMB, 0).toFixed(2));
    const existingIdx = this.state.timeSlices.findIndex((s) => s.timestamp === bucket);
    const slice: TimeSlicePoint = {
      timestamp: bucket,
      label: new Date(bucket).toISOString().slice(5, 16).replace('T', ' '),
      totalMB,
      byDeviceMB: Object.fromEntries(mappedDevices.map((d) => [d.id, d.dailyUsedMB])),
      byAppMB: {},
      avgLatencyMs: rootNode.latencyMs,
    };
    if (existingIdx >= 0) this.state.timeSlices[existingIdx] = slice;
    else this.state.timeSlices.push(slice);
    if (this.state.timeSlices.length > 2016) this.state.timeSlices.shift();

    this.saveToDisk();
    return this.state;
  }

  async optimizeChannels(): Promise<{ state: PersistedState; detail: string }> {
    if (!this.state.connected || !this.state.liveBridgeUrl) {
      throw new Error('Connect to a live ESP32 repeater first to run hardware RF channel scan.');
    }
    const resp = await fetch(`${this.state.liveBridgeUrl}/api/channel/optimize`, {
      method: 'POST',
      headers: this.espHeaders(true),
      signal: AbortSignal.timeout(8000),
    });
    if (!resp.ok) {
      throw new Error(`ESP32 channel optimizer returned HTTP ${resp.status}`);
    }
    await this.syncFromEsp32();
    return { state: this.state, detail: 'Channel optimization executed on ESP32.' };
  }

  async pingHost(host: string): Promise<string> {
    if (!this.state.connected || !this.state.liveBridgeUrl) {
      throw new Error('Connect to a live ESP32 repeater first to run a real ICMP ping.');
    }
    const resp = await fetch(
      `${this.state.liveBridgeUrl}/api/ping?host=${encodeURIComponent(host || '8.8.8.8')}`,
      {
        headers: this.espHeaders(),
        signal: AbortSignal.timeout(8000),
      }
    );
    if (!resp.ok) throw new Error(`ESP32 ping failed (HTTP ${resp.status})`);
    return resp.text();
  }

  queryTimeRange(fromMs: number, toMs: number, deviceId?: string) {
    const filtered = this.state.timeSlices.filter((s) => s.timestamp >= fromMs && s.timestamp <= toMs);
    let totalMB = 0;
    const deviceTotals: Record<string, number> = {};
    const appTotals: Record<string, number> = {};
    let latencySum = 0;

    for (const slice of filtered) {
      latencySum += slice.avgLatencyMs;
      if (deviceId && deviceId !== 'all') {
        const mb = slice.byDeviceMB[deviceId] || 0;
        totalMB += mb;
        deviceTotals[deviceId] = Number(((deviceTotals[deviceId] || 0) + mb).toFixed(2));
      } else {
        totalMB += slice.totalMB;
        for (const [dId, val] of Object.entries(slice.byDeviceMB)) {
          deviceTotals[dId] = Number(((deviceTotals[dId] || 0) + val).toFixed(2));
        }
        for (const [aName, val] of Object.entries(slice.byAppMB)) {
          appTotals[aName] = Number(((appTotals[aName] || 0) + val).toFixed(2));
        }
      }
    }

    const durationHours = Math.max(5 / 60, (toMs - fromMs) / 3600000);
    const avgMbps = Number(((totalMB * 8) / (durationHours * 3600)).toFixed(2));

    return {
      fromMs,
      toMs,
      sliceCount: filtered.length,
      totalMB: Number(totalMB.toFixed(2)),
      avgMbps,
      avgLatencyMs: filtered.length ? Number((latencySum / filtered.length).toFixed(1)) : 0,
      deviceBreakdown: this.state.devices
        .filter((d) => !deviceId || deviceId === 'all' || d.id === deviceId)
        .map((d) => ({
          id: d.id,
          name: d.name,
          mac: d.mac,
          usedMB: Number((deviceTotals[d.id] || 0).toFixed(2)),
          topApp: d.topApp,
          sharePct: totalMB > 0 ? Number((((deviceTotals[d.id] || 0) / totalMB) * 100).toFixed(1)) : 0,
        })),
      appBreakdown: Object.entries(appTotals).map(([name, usedMB]) => ({
        name,
        usedMB: Number(usedMB.toFixed(2)),
        sharePct: totalMB > 0 ? Number(((usedMB / totalMB) * 100).toFixed(1)) : 0,
      })),
      series: filtered.map((s) => ({
        timestamp: s.timestamp,
        label: s.label,
        mb: deviceId && deviceId !== 'all' ? Number((s.byDeviceMB[deviceId] || 0).toFixed(2)) : s.totalMB,
        avgLatencyMs: s.avgLatencyMs,
      })),
    };
  }

  updatePlan(patch: Partial<MonthlyPlanConfig>): MonthlyPlanConfig {
    this.state.plan = {
      ...this.state.plan,
      ...patch,
      graceRetentionHours: 48,
    };
    this.saveToDisk();
    return this.state.plan;
  }

  executeMonthlyRollover(): {
    archivedGrace: GraceArchiveEntry;
    summary: PermanentMonthlySummary;
    state: PersistedState;
  } {
    const now = new Date();
    const cycleLabel = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')} (${now.toISOString().slice(11, 16)})`;
    const totalUsedMB = this.state.devices.reduce((acc, d) => acc + d.monthlyUsedMB, 0);
    const totalUsedGB = Number((totalUsedMB / 1024).toFixed(2));

    const archivedGrace: GraceArchiveEntry = {
      archiveId: `grace-${Date.now()}`,
      cycleLabel,
      archivedAt: now.toISOString(),
      expiresAt: new Date(now.getTime() + 48 * 3600 * 1000).toISOString(),
      remainingHours: 48,
      rawSliceCount: this.state.timeSlices.length,
      totalPlanMB: Math.round(totalUsedMB),
      devices: this.state.devices.map((d) => ({
        mac: d.mac,
        name: d.name,
        usedMB: d.monthlyUsedMB,
        topApp: d.topApp,
        peakMbps: 0,
      })),
    };

    const summary: PermanentMonthlySummary = {
      cycleLabel,
      closedAt: now.toISOString(),
      planName: this.state.plan.planName,
      planCapGB: this.state.plan.totalCapGB,
      totalUsedGB,
      rawLogsPurged: false,
      userSummaries: this.state.devices.map((d) => ({
        mac: d.mac,
        name: d.name,
        totalUsedGB: Number((d.monthlyUsedMB / 1024).toFixed(2)),
        sharePct: totalUsedMB > 0 ? Number(((d.monthlyUsedMB / totalUsedMB) * 100).toFixed(1)) : 0,
        mostUsedApp: d.topApp,
        mostUsedAppGB: Number(((d.appBreakdownMB[d.topApp] || 0) / 1024).toFixed(2)),
      })),
    };

    this.state.graceArchives.unshift(archivedGrace);
    this.state.monthlySummaries.unshift(summary);
    this.state.devices = this.state.devices.map((d) => ({
      ...d,
      monthlyUsedMB: 0,
      dailyUsedMB: 0,
      blocked: false,
    }));

    this.saveToDisk();
    return { archivedGrace, summary, state: this.state };
  }

  pruneExpiredGraceArchives(): { purgedCount: number; state: PersistedState } {
    const purgedCount = this.state.graceArchives.length;
    if (purgedCount > 0) {
      const purgedLabels = new Set(this.state.graceArchives.map((g) => g.cycleLabel));
      this.state.graceArchives = [];
      this.state.monthlySummaries = this.state.monthlySummaries.map((s) =>
        purgedLabels.has(s.cycleLabel) ? { ...s, rawLogsPurged: true } : s
      );
      this.saveToDisk();
    }
    return { purgedCount, state: this.state };
  }

  toggleFeature(key: keyof OptimizationFeatures, value: boolean): OptimizationFeatures {
    this.state.features[key] = value;
    this.saveToDisk();
    return this.state.features;
  }

  async updateDeviceAction(
    deviceId: string,
    action: 'block' | 'unblock' | 'whitelist' | 'unwhitelist' | 'setCap',
    capMB?: number
  ) {
    const dev = this.state.devices.find((d) => d.id === deviceId);
    if (!dev) return null;

    if (this.state.connected && this.state.liveBridgeUrl) {
      const endpoint =
        action === 'block'
          ? '/api/block'
          : action === 'unblock'
          ? '/api/unblock'
          : action === 'setCap'
          ? '/api/setLimit'
          : '/api/whitelist';

      const params = new URLSearchParams({ mac: dev.mac });
      if (action === 'whitelist') params.set('enabled', '1');
      if (action === 'unwhitelist') params.set('enabled', '0');
      if (action === 'setCap') params.set('mb', String(capMB ?? 0));

      const resp = await fetch(`${this.state.liveBridgeUrl}${endpoint}`, {
        method: 'POST',
        headers: { ...this.espHeaders(true), 'Content-Type': 'application/x-www-form-urlencoded' },
        body: params,
        signal: AbortSignal.timeout(5000),
      });
      if (!resp.ok) {
        throw new Error(`ESP32 rejected action (HTTP ${resp.status})`);
      }
    }

    if (action === 'block') dev.blocked = true;
    if (action === 'unblock') dev.blocked = false;
    if (action === 'whitelist') dev.whitelisted = true;
    if (action === 'unwhitelist') dev.whitelisted = false;
    if (action === 'setCap' && typeof capMB === 'number' && capMB >= 0) {
      dev.monthlyCapMB = capMB;
    }
    this.saveToDisk();
    return dev;
  }

  configureTelegram(input: {
    botToken?: string;
    chatId: string;
    autoBackupOnRollover: boolean;
    scheduleHours: number;
    encryptTelegramBackups: boolean;
  }): TelegramConfig {
    if (input.botToken && input.botToken.trim().length > 10) {
      this.telegramTokenSecret = input.botToken.trim();
      this.state.telegram.maskedToken = `••••${this.telegramTokenSecret.slice(-6)}`;
    }
    this.state.telegram.chatId = input.chatId.trim();
    this.state.telegram.autoBackupOnRollover = input.autoBackupOnRollover;
    this.state.telegram.scheduleHours = input.scheduleHours;
    this.state.telegram.encryptTelegramBackups = input.encryptTelegramBackups;
    this.state.telegram.configured = Boolean(this.telegramTokenSecret && this.state.telegram.chatId);
    this.state.telegram.lastStatus = this.state.telegram.configured
      ? 'Telegram bot configured and ready.'
      : 'Bot Token and Chat ID required.';
    this.saveToDisk();
    return this.state.telegram;
  }

  async sendTelegramBackup(encryptedJsonPayload?: string): Promise<{ ok: boolean; detail: string; telegram: TelegramConfig }> {
    if (!this.telegramTokenSecret || !this.state.telegram.chatId) {
      const msg = 'Enter your Telegram Bot Token and Chat ID first.';
      this.state.telegram.lastStatus = msg;
      return { ok: false, detail: msg, telegram: this.state.telegram };
    }

    if (!/^\d{6,12}:[A-Za-z0-9_-]{20,64}$/.test(this.telegramTokenSecret)) {
      const msg = 'Invalid Telegram Bot Token format.';
      this.state.telegram.lastStatus = msg;
      return { ok: false, detail: msg, telegram: this.state.telegram };
    }

    const totalGB = (this.state.devices.reduce((a, d) => a + d.monthlyUsedMB, 0) / 1024).toFixed(2);
    const summaryText = [
      `ESP32 Repeater Backup Report`,
      `Plan: ${this.state.plan.planName}`,
      `Used: ${totalGB} / ${this.state.plan.totalCapGB} GB`,
      `Devices: ${this.state.devices.length}`,
      `Monthly Digests: ${this.state.monthlySummaries.length}`,
    ].join('\n');

    const msgResp = await fetch(`https://api.telegram.org/bot${this.telegramTokenSecret}/sendMessage`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ chat_id: this.state.telegram.chatId, text: summaryText }),
    });
    if (!msgResp.ok) {
      const errText = await msgResp.text();
      this.state.telegram.lastStatus = `Telegram HTTP ${msgResp.status}: ${errText.slice(0, 120)}`;
      this.saveToDisk();
      return { ok: false, detail: this.state.telegram.lastStatus, telegram: this.state.telegram };
    }

    if (encryptedJsonPayload) {
      const form = new FormData();
      form.append('chat_id', this.state.telegram.chatId);
      form.append('caption', 'ESP32 Repeater AES-256-GCM Encrypted Backup');
      form.append(
        'document',
        new Blob([encryptedJsonPayload], { type: 'application/json' }),
        `esp32-backup-${new Date().toISOString().slice(0, 10)}.vortaxbak`
      );
      const docResp = await fetch(`https://api.telegram.org/bot${this.telegramTokenSecret}/sendDocument`, {
        method: 'POST',
        body: form,
      });
      if (!docResp.ok) {
        const errText = await docResp.text();
        this.state.telegram.lastStatus = `Summary sent, attachment failed (${docResp.status}): ${errText.slice(0, 100)}`;
        this.saveToDisk();
        return { ok: false, detail: this.state.telegram.lastStatus, telegram: this.state.telegram };
      }
    }

    this.state.telegram.lastSentAt = new Date().toISOString();
    this.state.telegram.lastStatus = 'Delivered report & encrypted backup to Telegram.';
    this.saveToDisk();
    return { ok: true, detail: this.state.telegram.lastStatus, telegram: this.state.telegram };
  }

  exportBackupSnapshot() {
    return {
      exportedAt: new Date().toISOString(),
      firmwareVersion: this.state.firmwareVersion,
      plan: this.state.plan,
      devices: this.state.devices,
      monthlySummaries: this.state.monthlySummaries,
      graceArchives: this.state.graceArchives,
      features: this.state.features,
      timeSlices: this.state.timeSlices,
    };
  }

  restoreBackupSnapshot(snapshot: unknown): { ok: boolean; message: string } {
    if (!snapshot || typeof snapshot !== 'object') {
      return { ok: false, message: 'Invalid backup payload.' };
    }
    const s = snapshot as Record<string, unknown>;
    if (!Array.isArray(s.devices) || !s.plan || typeof s.plan !== 'object') {
      return { ok: false, message: 'Backup missing devices or plan.' };
    }

    this.state.devices = s.devices as DeviceRecord[];
    this.state.plan = s.plan as MonthlyPlanConfig;
    if (Array.isArray(s.monthlySummaries)) this.state.monthlySummaries = s.monthlySummaries as PermanentMonthlySummary[];
    if (Array.isArray(s.graceArchives)) this.state.graceArchives = s.graceArchives as GraceArchiveEntry[];
    if (Array.isArray(s.timeSlices)) this.state.timeSlices = s.timeSlices as TimeSlicePoint[];
    if (s.features && typeof s.features === 'object') this.state.features = s.features as OptimizationFeatures;

    this.saveToDisk();
    return { ok: true, message: 'Encrypted backup decrypted and restored.' };
  }

  pushErrorLog(entry: Omit<SystemErrorEntry, 'id' | 'timestamp'>) {
    this.state.errorLogs.unshift({
      id: `err-${Date.now()}`,
      timestamp: new Date().toISOString(),
      ...entry,
    });
    if (this.state.errorLogs.length > 40) this.state.errorLogs.length = 40;
    this.saveToDisk();
  }
}

export const repeaterEngine = new RepeaterEngine();
