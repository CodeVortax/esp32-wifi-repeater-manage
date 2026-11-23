import React, { useCallback, useEffect, useState } from 'react';
import { I18N, type Lang } from './i18n';
import type { MonthlyPlanConfig, OptimizationFeatures, RepeaterSystemState, TelegramConfig } from './types';
import { OverviewTab } from './components/OverviewTab';
import { UsersTab } from './components/UsersTab';
import { AppsTab } from './components/AppsTab';
import { BlockTab } from './components/BlockTab';
import { SpeedTab } from './components/SpeedTab';
import { SettingsTab } from './components/SettingsTab';
import { SystemTab } from './components/SystemTab';

type PageKey = 'overview' | 'users' | 'apps' | 'block' | 'speed' | 'settings' | 'system';

const NAV_PATHS: Array<{ key: PageKey; d: string }> = [
  { key: 'overview', d: 'M3 10.5 12 3l9 7.5V20a1 1 0 0 1-1 1h-5v-6H9v6H4a1 1 0 0 1-1-1z' },
  { key: 'users', d: 'M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2 M9 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8 M22 21v-2a4 4 0 0 0-3-3.87 M16 3.13a4 4 0 0 1 0 7.75' },
  { key: 'apps', d: 'M3 3h7v7H3z M14 3h7v7h-7z M14 14h7v7h-7z M3 14h7v7H3z' },
  { key: 'block', d: 'M12 2 4 5v6.1c0 5.1 3.4 9.7 8 10.9 4.6-1.2 8-5.8 8-10.9V5l-8-3z M9 12l2 2 4-4' },
  { key: 'speed', d: 'M12 14l4-4 M3.34 19a10 10 0 1 1 17.32 0' },
  { key: 'settings', d: 'M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z' },
  { key: 'system', d: 'M12 2v4 M12 18v4 M4.93 4.93l2.83 2.83 M16.24 16.24l2.83 2.83 M2 12h4 M18 12h4 M4.93 19.07l2.83-2.83 M16.24 7.76l2.83-2.83' },
];

function getInitialLang(): Lang {
  const q = new URLSearchParams(window.location.search).get('lang');
  return q === 'ar' || q === 'fr' || q === 'en' ? q : 'en';
}

function getInitialPage(): PageKey {
  const q = new URLSearchParams(window.location.search).get('tab');
  const valid: PageKey[] = ['overview', 'users', 'apps', 'block', 'speed', 'settings', 'system'];
  return valid.includes(q as PageKey) ? (q as PageKey) : 'overview';
}

export default function App() {
  const [page, setPage] = useState<PageKey>(getInitialPage);
  const [lang, setLang] = useState<Lang>(getInitialLang);
  const [theme, setTheme] = useState<'dark' | 'light'>('dark');
  const [state, setState] = useState<RepeaterSystemState | null>(null);
  const [internetOff, setInternetOff] = useState(false);
  const [optimizing, setOptimizing] = useState(false);
  const [toastMsg, setToastMsg] = useState('');
  const [toastVisible, setToastVisible] = useState(false);

  const t = (k: string) => I18N[lang][k] || k;

  useEffect(() => {
    document.documentElement.lang = lang;
    document.documentElement.dir = lang === 'ar' ? 'rtl' : 'ltr';
  }, [lang]);

  useEffect(() => {
    document.documentElement.setAttribute('data-theme', theme);
  }, [theme]);

  const showToast = useCallback((msg: string) => {
    setToastMsg(msg);
    setToastVisible(true);
    setTimeout(() => setToastVisible(false), 2600);
  }, []);

  const loadOverview = useCallback(async () => {
    const resp = await fetch('/api/overview');
    if (resp.ok) {
      setState(await resp.json());
    }
  }, []);

  useEffect(() => {
    loadOverview();
  }, [loadOverview]);

  const handleConnectBridge = async (url: string, password: string) => {
    const resp = await fetch('/api/bridge/connect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ url, password }),
    });
    const data = await resp.json();
    if (!resp.ok) throw new Error(data.error || t('failed'));
    setState(data);
    showToast(t('connected'));
  };

  const handleSyncBridge = async () => {
    const resp = await fetch('/api/bridge/sync', { method: 'POST' });
    const data = await resp.json();
    if (!resp.ok) {
      showToast(data.error || t('failed'));
      return;
    }
    setState(data);
    showToast(t('saved'));
  };

  const handleDisconnectBridge = async () => {
    const resp = await fetch('/api/bridge/disconnect', { method: 'POST' });
    if (resp.ok) {
      setState(await resp.json());
      showToast(t('disconnectedToast'));
    }
  };

  const handleOptimizeChannels = async () => {
    setOptimizing(true);
    try {
      const resp = await fetch('/api/channel/optimize', { method: 'POST' });
      const data = await resp.json();
      if (!resp.ok) {
        showToast(data.error || t('failed'));
        return;
      }
      setState(data.state);
      showToast(data.detail || t('saved'));
    } finally {
      setOptimizing(false);
    }
  };

  const handlePingNode = async () => {
    try {
      const resp = await fetch('/api/ping', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ host: '8.8.8.8' }),
      });
      const data = await resp.json();
      if (!resp.ok) {
        showToast(data.error || t('failed'));
        return;
      }
      showToast(data.output || 'OK');
    } catch {
      showToast(t('failed'));
    }
  };

  const handlePingHost = async (host: string): Promise<string> => {
    const resp = await fetch('/api/ping', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ host }),
    });
    const data = await resp.json();
    if (!resp.ok) throw new Error(data.error || t('failed'));
    return String(data.output || '');
  };

  const handleDeviceAction = async (
    deviceId: string,
    action: 'block' | 'unblock' | 'whitelist' | 'unwhitelist' | 'setCap',
    capMB?: number
  ) => {
    const resp = await fetch(`/api/devices/${encodeURIComponent(deviceId)}/action`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action, capMB }),
    });
    const data = await resp.json();
    if (!resp.ok) {
      showToast(data.error || t('failed'));
      return;
    }
    setState((prev) =>
      prev ? { ...prev, devices: prev.devices.map((d) => (d.id === deviceId ? data : d)) } : prev
    );
    showToast(t('saved'));
  };

  const handleSavePlan = async (patch: Partial<MonthlyPlanConfig>) => {
    const resp = await fetch('/api/plans', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(patch),
    });
    if (resp.ok) {
      const updated = await resp.json();
      setState((prev) => (prev ? { ...prev, plan: updated } : prev));
    }
  };

  const handleRolloverMonth = async () => {
    const resp = await fetch('/api/plans/rollover', { method: 'POST' });
    if (resp.ok) {
      const data = await resp.json();
      setState(data.state);
    }
  };

  const handlePruneGrace = async () => {
    const resp = await fetch('/api/plans/prune-grace', { method: 'POST' });
    if (resp.ok) {
      const data = await resp.json();
      setState(data.state);
    }
  };

  const handleRestoreState = async (snapshot: unknown) => {
    const resp = await fetch('/api/backup/restore', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ snapshot }),
    });
    const data = await resp.json();
    if (data.ok && data.state) setState(data.state);
    return { ok: Boolean(data.ok), message: String(data.message || '') };
  };

  const handleSaveTelegram = async (input: {
    botToken?: string;
    chatId: string;
    autoBackupOnRollover: boolean;
    scheduleHours: number;
    encryptTelegramBackups: boolean;
  }): Promise<TelegramConfig> => {
    const resp = await fetch('/api/telegram/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(input),
    });
    const cfg = await resp.json();
    setState((prev) => (prev ? { ...prev, telegram: cfg } : prev));
    return cfg;
  };

  const handleSendTelegramBackup = async (encryptedEnvelope?: string) => {
    const resp = await fetch('/api/telegram/send-backup', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ encryptedEnvelope }),
    });
    const data = await resp.json();
    if (data.telegram) {
      setState((prev) => (prev ? { ...prev, telegram: data.telegram } : prev));
    }
    return { ok: Boolean(data.ok), detail: String(data.detail || '') };
  };

  const handleToggleFeature = async (key: keyof OptimizationFeatures, value: boolean) => {
    const resp = await fetch('/api/features/toggle', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ key, value }),
    });
    if (resp.ok) {
      const features = await resp.json();
      setState((prev) => (prev ? { ...prev, features } : prev));
    }
  };

  return (
    <div className="app">
      <nav className="side" aria-label="Main">
        <div className="brand">
          <i>
            <svg viewBox="0 0 24 24">
              <path d="M5 12.55a11 11 0 0 1 14.08 0 M1.42 9a16 16 0 0 1 21.16 0 M8.53 16.11a6 6 0 0 1 6.95 0 M12 20h.01" />
            </svg>
          </i>
          <span>{t('brand')}</span>
        </div>

        {NAV_PATHS.map((item) => (
          <button
            key={item.key}
            type="button"
            className="nav"
            aria-current={page === item.key ? 'page' : undefined}
            onClick={() => setPage(item.key)}
          >
            <svg viewBox="0 0 24 24">
              <path d={item.d} />
            </svg>
            <span>{t(item.key)}</span>
          </button>
        ))}

        <div className="grow" />

        <div className="row" style={{ padding: '6px 6px 0', gap: 6 }}>
          <button
            type="button"
            className="btn sm"
            style={{ flex: 1 }}
            onClick={() => setLang(lang === 'en' ? 'ar' : lang === 'ar' ? 'fr' : 'en')}
          >
            {lang === 'en' ? 'العربية' : lang === 'ar' ? 'Français' : 'English'}
          </button>
          <button
            type="button"
            className="btn sm"
            onClick={() => setTheme(theme === 'dark' ? 'light' : 'dark')}
          >
            {theme === 'dark' ? '☀' : '☾'}
          </button>
        </div>
      </nav>

      <main>
        <header className="top">
          <h1>{t(page)}</h1>
          <span className="pill">
            <span className={`dot ${state?.connected ? 'on' : ''}`} />
            <span>{state?.connected ? t('connected') : t('standalone')}</span>
          </span>
        </header>

        {!state ? (
          <div className="empty">...</div>
        ) : (
          <>
            {page === 'overview' && (
              <OverviewTab
                state={state}
                lang={lang}
                internetOff={internetOff}
                onToggleInternet={() => {
                  setInternetOff((v) => !v);
                  showToast(t('saved'));
                }}
                optimizing={optimizing}
                onOptimizeChannels={handleOptimizeChannels}
                onPingNode={handlePingNode}
                onConnectBridge={handleConnectBridge}
                onSyncBridge={handleSyncBridge}
                onDisconnectBridge={handleDisconnectBridge}
              />
            )}
            {page === 'users' && (
              <UsersTab
                devices={state.devices}
                lang={lang}
                onDeviceAction={handleDeviceAction}
              />
            )}
            {page === 'apps' && <AppsTab devices={state.devices} lang={lang} />}
            {page === 'block' && <BlockTab lang={lang} onToast={showToast} />}
            {page === 'speed' && (
              <SpeedTab
                devices={state.devices}
                features={state.features}
                lang={lang}
                onToggleFeature={handleToggleFeature}
                onToast={showToast}
              />
            )}
            {page === 'settings' && (
              <SettingsTab
                state={state}
                lang={lang}
                theme={theme}
                onChangeLang={setLang}
                onChangeTheme={setTheme}
                onSavePlan={handleSavePlan}
                onRolloverMonth={handleRolloverMonth}
                onPruneGrace={handlePruneGrace}
                onRestoreState={handleRestoreState}
                onSaveTelegram={handleSaveTelegram}
                onSendTelegramBackup={handleSendTelegramBackup}
                onToast={showToast}
              />
            )}
            {page === 'system' && (
              <SystemTab
                state={state}
                lang={lang}
                onPingHost={handlePingHost}
                onToast={showToast}
              />
            )}
          </>
        )}
      </main>

      <nav className="bottom" aria-label="Main">
        {NAV_PATHS.map((item) => (
          <button
            key={item.key}
            type="button"
            className="nav"
            aria-current={page === item.key ? 'page' : undefined}
            onClick={() => setPage(item.key)}
          >
            <svg viewBox="0 0 24 24">
              <path d={item.d} />
            </svg>
            <span>{t(item.key)}</span>
          </button>
        ))}
      </nav>

      <div id="toast" className={toastVisible ? 'show' : ''} role="status" aria-live="polite">
        {toastMsg}
      </div>
    </div>
  );
}
