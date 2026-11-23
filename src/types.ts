export type AppCategory = 'Video' | 'Social' | 'Messaging' | 'Web & Search' | 'Gaming' | 'Updates & System';

export interface RepeaterNode {
  id: string;
  name: string;
  role: 'Root Repeater' | 'Edge Node' | 'Relay Node';
  staSsid: string;
  apSsid: string;
  apIp: string;
  staIp: string;
  channel: number;
  bandwidthMHz: 20 | 40;
  txPowerDbm: number;
  rssiDbm: number;
  latencyMs: number;
  jitterMs: number;
  packetLossPct: number;
  connectedClients: number;
  freeHeapBytes: number;
  minFreeHeapBytes: number;
  largestContigBlockBytes: number;
  staticBlockUsedBytes: number;
  staticBlockTotalBytes: number;
  wdtResetsAvoided: number;
  uptimeSec: number;
  qosFastPath: boolean;
  naptTurbo: boolean;
  psDisabled: boolean;
  status: 'Optimal' | 'Congested' | 'High Latency';
}

export interface ChannelSpectrumMetric {
  channel: number;
  overlappingAps: number;
  noiseFloorDbm: number;
  utilizationPct: number;
  interferenceScore: number;
  recommended: boolean;
}

export interface DeviceRecord {
  id: string;
  mac: string;
  name: string;
  ip: string;
  nodeId: string;
  online: boolean;
  whitelisted: boolean;
  blocked: boolean;
  latencyMs: number;
  signalDbm: number;
  monthlyUsedMB: number;
  monthlyCapMB: number; // 0 = unlimited
  dailyUsedMB: number;
  topApp: string;
  topCategory: AppCategory;
  appBreakdownMB: Record<string, number>;
  speedDownKbps: number;
  speedUpKbps: number;
}

export interface TimeSlicePoint {
  timestamp: number; // epoch ms (5-minute precision buckets)
  label: string;
  totalMB: number;
  byDeviceMB: Record<string, number>;
  byAppMB: Record<string, number>;
  avgLatencyMs: number;
}

export interface MonthlyPlanConfig {
  planName: string;
  totalCapGB: number;
  cycleStartDay: number; // 1..28
  defaultUserCapGB: number;
  graceRetentionHours: number; // 48 hours = 2 days
  currentCycleStart: string;
  currentCycleEnd: string;
  autoRolloverEnabled: boolean;
}

export interface GraceArchiveEntry {
  archiveId: string;
  cycleLabel: string;
  archivedAt: string;
  expiresAt: string;
  remainingHours: number;
  rawSliceCount: number;
  totalPlanMB: number;
  devices: Array<{
    mac: string;
    name: string;
    usedMB: number;
    topApp: string;
    peakMbps: number;
  }>;
}

export interface PermanentMonthlySummary {
  cycleLabel: string;
  closedAt: string;
  planName: string;
  planCapGB: number;
  totalUsedGB: number;
  rawLogsPurged: boolean;
  userSummaries: Array<{
    mac: string;
    name: string;
    totalUsedGB: number;
    sharePct: number;
    mostUsedApp: string;
    mostUsedAppGB: number;
  }>;
}

export interface SystemErrorEntry {
  id: string;
  timestamp: string;
  subsystem: 'HTTP/AsyncTCP' | 'NAPT/WiFi' | 'StaticBlock' | 'WatchdogGuard' | 'TelegramTLS';
  severity: 'Recovered' | 'Warning' | 'Handled Fault';
  code: string;
  message: string;
  heapAtEventKB: number;
  actionTaken: string;
}

export interface TelegramConfig {
  configured: boolean;
  maskedToken: string;
  chatId: string;
  autoBackupOnRollover: boolean;
  scheduleHours: number; // 0 = manual/rollover only, 24 = daily, 168 = weekly
  encryptTelegramBackups: boolean;
  lastSentAt: string | null;
  lastStatus: string;
}

export interface OptimizationFeatures {
  autoChannelScan: boolean;
  qosLowLatencyLane: boolean;
  coalescedDashboardApi: boolean;
  staticBlockWearLeveling: boolean;
  brownoutTxStepDown: boolean;
  dnsResponseCache: boolean;
}

export interface RepeaterSystemState {
  connected: boolean;
  lastSyncedAt: string | null;
  firmwareVersion: string;
  hardwareTarget: string;
  liveBridgeUrl: string;
  nodes: RepeaterNode[];
  spectrum: ChannelSpectrumMetric[];
  devices: DeviceRecord[];
  plan: MonthlyPlanConfig;
  graceArchives: GraceArchiveEntry[];
  monthlySummaries: PermanentMonthlySummary[];
  errorLogs: SystemErrorEntry[];
  telegram: TelegramConfig;
  features: OptimizationFeatures;
  timeSlices: TimeSlicePoint[];
}
