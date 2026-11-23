import express from 'express';
import path from 'path';
import { fileURLToPath } from 'url';
import { repeaterEngine } from './server/repeaterEngine.ts';
import type { OptimizationFeatures } from './src/types.ts';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

async function startServer() {
  const app = express();
  const PORT = Number(process.env.PORT) || 3000;

  app.use(express.json({ limit: '4mb' }));

  app.get('/api/overview', (_req, res) => {
    res.json(repeaterEngine.getState());
  });

  app.post('/api/bridge/connect', async (req, res) => {
    try {
      const url = typeof req.body?.url === 'string' ? req.body.url : '';
      const pass = typeof req.body?.password === 'string' ? req.body.password : undefined;
      const state = await repeaterEngine.connectEsp32(url, pass);
      res.json(state);
    } catch (err) {
      res.status(400).json({ error: err instanceof Error ? err.message : 'Connection failed' });
    }
  });

  app.post('/api/bridge/sync', async (_req, res) => {
    try {
      const state = await repeaterEngine.syncFromEsp32();
      res.json(state);
    } catch (err) {
      res.status(400).json({ error: err instanceof Error ? err.message : 'Sync failed' });
    }
  });

  app.post('/api/bridge/disconnect', (_req, res) => {
    res.json(repeaterEngine.disconnectEsp32());
  });

  app.post('/api/channel/optimize', async (_req, res) => {
    try {
      const result = await repeaterEngine.optimizeChannels();
      res.json(result);
    } catch (err) {
      res.status(400).json({ error: err instanceof Error ? err.message : 'Channel optimize failed' });
    }
  });

  app.post('/api/ping', async (req, res) => {
    try {
      const host = typeof req.body?.host === 'string' ? req.body.host : '8.8.8.8';
      const output = await repeaterEngine.pingHost(host);
      res.json({ output });
    } catch (err) {
      res.status(400).json({ error: err instanceof Error ? err.message : 'Ping failed' });
    }
  });

  app.get('/api/usage/range', (req, res) => {
    const now = Date.now();
    const fromMs = Number(req.query.from) || now - 6 * 3600 * 1000;
    const toMs = Number(req.query.to) || now;
    const deviceId = typeof req.query.deviceId === 'string' ? req.query.deviceId : 'all';

    if (!Number.isFinite(fromMs) || !Number.isFinite(toMs) || fromMs > toMs) {
      res.status(400).json({ error: 'Invalid time range parameters' });
      return;
    }
    res.json(repeaterEngine.queryTimeRange(fromMs, toMs, deviceId));
  });

  app.post('/api/plans', (req, res) => {
    const { planName, totalCapGB, cycleStartDay, defaultUserCapGB, autoRolloverEnabled } = req.body ?? {};
    const updated = repeaterEngine.updatePlan({
      ...(typeof planName === 'string' && planName.trim() ? { planName: planName.trim().slice(0, 64) } : {}),
      ...(typeof totalCapGB === 'number' && totalCapGB > 0 && totalCapGB <= 50000 ? { totalCapGB } : {}),
      ...(typeof cycleStartDay === 'number' && cycleStartDay >= 1 && cycleStartDay <= 28 ? { cycleStartDay } : {}),
      ...(typeof defaultUserCapGB === 'number' && defaultUserCapGB >= 0 && defaultUserCapGB <= 10000
        ? { defaultUserCapGB }
        : {}),
      ...(typeof autoRolloverEnabled === 'boolean' ? { autoRolloverEnabled } : {}),
    });
    res.json(updated);
  });

  app.post('/api/plans/rollover', (_req, res) => {
    res.json(repeaterEngine.executeMonthlyRollover());
  });

  app.post('/api/plans/prune-grace', (_req, res) => {
    res.json(repeaterEngine.pruneExpiredGraceArchives());
  });

  app.post('/api/features/toggle', (req, res) => {
    const key = req.body?.key as keyof OptimizationFeatures;
    const value = Boolean(req.body?.value);
    const allowed: Array<keyof OptimizationFeatures> = [
      'autoChannelScan',
      'qosLowLatencyLane',
      'coalescedDashboardApi',
      'staticBlockWearLeveling',
      'brownoutTxStepDown',
      'dnsResponseCache',
    ];
    if (!allowed.includes(key)) {
      res.status(400).json({ error: 'Unknown feature key' });
      return;
    }
    res.json(repeaterEngine.toggleFeature(key, value));
  });

  app.post('/api/devices/:id/action', async (req, res) => {
    const { action, capMB } = req.body ?? {};
    if (!['block', 'unblock', 'whitelist', 'unwhitelist', 'setCap'].includes(action)) {
      res.status(400).json({ error: 'Invalid device action' });
      return;
    }
    try {
      const updated = await repeaterEngine.updateDeviceAction(
        req.params.id,
        action,
        typeof capMB === 'number' ? capMB : undefined
      );
      if (!updated) {
        res.status(404).json({ error: 'Device not found' });
        return;
      }
      res.json(updated);
    } catch (err) {
      res.status(400).json({ error: err instanceof Error ? err.message : 'Device action failed' });
    }
  });

  app.get('/api/backup/payload', (_req, res) => {
    res.json(repeaterEngine.exportBackupSnapshot());
  });

  app.post('/api/backup/restore', (req, res) => {
    const result = repeaterEngine.restoreBackupSnapshot(req.body?.snapshot);
    if (!result.ok) {
      res.status(400).json(result);
      return;
    }
    res.json({ ...result, state: repeaterEngine.getState() });
  });

  app.post('/api/telegram/config', (req, res) => {
    const { botToken, chatId, autoBackupOnRollover, scheduleHours, encryptTelegramBackups } = req.body ?? {};
    const cfg = repeaterEngine.configureTelegram({
      botToken: typeof botToken === 'string' ? botToken : undefined,
      chatId: typeof chatId === 'string' ? chatId : '',
      autoBackupOnRollover: Boolean(autoBackupOnRollover),
      scheduleHours: Number(scheduleHours) || 0,
      encryptTelegramBackups: Boolean(encryptTelegramBackups),
    });
    res.json(cfg);
  });

  app.post('/api/telegram/send-backup', async (req, res) => {
    const encryptedPayload = typeof req.body?.encryptedEnvelope === 'string' ? req.body.encryptedEnvelope : undefined;
    const result = await repeaterEngine.sendTelegramBackup(encryptedPayload);
    res.status(result.ok ? 200 : 400).json(result);
  });

  if (process.env.NODE_ENV !== 'production') {
    const { createServer: createViteServer } = await import('vite');
    const vite = await createViteServer({
      server: { middlewareMode: true },
      appType: 'spa',
    });
    app.use(vite.middlewares);
  } else {
    const distDir = path.resolve(__dirname, 'dist');
    app.use(express.static(distDir));
    app.get('*', (_req, res) => {
      res.sendFile(path.join(distDir, 'index.html'));
    });
  }

  app.listen(PORT, '0.0.0.0', () => {
    console.log(`ESP32 Repeater Manager listening on http://0.0.0.0:${PORT}`);
  });
}

startServer();
