import React, { useRef, useState } from 'react';
import type { Lang } from '../i18n';
import { I18N } from '../i18n';
import type { MonthlyPlanConfig, RepeaterSystemState, TelegramConfig } from '../types';
import {
  decryptBackupPayload,
  encryptBackupPayload,
  type EncryptedBackupEnvelope,
} from '../utils/cryptoBackup';

interface SettingsTabProps {
  state: RepeaterSystemState;
  lang: Lang;
  theme: 'dark' | 'light';
  onChangeLang: (l: Lang) => void;
  onChangeTheme: (th: 'dark' | 'light') => void;
  onSavePlan: (patch: Partial<MonthlyPlanConfig>) => Promise<void>;
  onRolloverMonth: () => Promise<void>;
  onPruneGrace: () => Promise<void>;
  onRestoreState: (snapshot: unknown) => Promise<{ ok: boolean; message: string }>;
  onSaveTelegram: (input: {
    botToken?: string;
    chatId: string;
    autoBackupOnRollover: boolean;
    scheduleHours: number;
    encryptTelegramBackups: boolean;
  }) => Promise<TelegramConfig>;
  onSendTelegramBackup: (encryptedEnvelope?: string) => Promise<{ ok: boolean; detail: string }>;
  onToast: (msg: string) => void;
}

export const SettingsTab: React.FC<SettingsTabProps> = ({
  state,
  lang,
  theme,
  onChangeLang,
  onChangeTheme,
  onSavePlan,
  onRolloverMonth,
  onPruneGrace,
  onRestoreState,
  onSaveTelegram,
  onSendTelegramBackup,
  onToast,
}) => {
  const t = (k: string) => I18N[lang][k] || k;

  // WiFi / Admin password (used also for AES-256-GCM backup encryption)
  const [staSsid, setStaSsid] = useState(state.nodes[0]?.staSsid || '');
  const [staPass, setStaPass] = useState('');
  const [apSsid, setApSsid] = useState(state.nodes[0]?.apSsid || '');
  const [apPass, setApPass] = useState('');
  const [adminPass, setAdminPass] = useState('');

  // Monthly plan state
  const [planName, setPlanName] = useState(state.plan.planName);
  const [totalCapGB, setTotalCapGB] = useState(state.plan.totalCapGB);
  const [cycleDay, setCycleDay] = useState(state.plan.cycleStartDay);
  const [defaultUserGB, setDefaultUserGB] = useState(state.plan.defaultUserCapGB);
  const [autoRollover, setAutoRollover] = useState(state.plan.autoRolloverEnabled);

  // Encrypted backup state
  const [backupMsg, setBackupMsg] = useState('');
  const [backupErr, setBackupErr] = useState('');
  const fileRef = useRef<HTMLInputElement | null>(null);

  // Telegram auto-backup state
  const [tgToken, setTgToken] = useState('');
  const [tgChat, setTgChat] = useState(state.telegram.chatId);
  const [tgAutoMonth, setTgAutoMonth] = useState(state.telegram.autoBackupOnRollover);
  const [tgAttachEnc, setTgAttachEnc] = useState(state.telegram.encryptTelegramBackups);
  const [tgSchedule, setTgSchedule] = useState(state.telegram.scheduleHours);
  const [tgMsg, setTgMsg] = useState('');

  const handleEncryptedDownload = async () => {
    setBackupErr('');
    setBackupMsg('');
    try {
      if (!adminPass.trim()) throw new Error(t('enterEncPass'));
      const resp = await fetch('/api/backup/payload');
      const payload = await resp.json();
      const envelope = await encryptBackupPayload(payload, adminPass);
      const blob = new Blob([JSON.stringify(envelope, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `esp32-repeater-backup-${new Date().toISOString().slice(0, 10)}.vortaxbak`;
      a.click();
      URL.revokeObjectURL(url);
      setBackupMsg(`AES-256-GCM (.vortaxbak · ${(blob.size / 1024).toFixed(1)} KB)`);
    } catch (err) {
      setBackupErr(err instanceof Error ? err.message : t('failed'));
    }
  };

  const handleEncryptedRestore = async (file: File) => {
    setBackupErr('');
    setBackupMsg('');
    try {
      if (!adminPass.trim()) throw new Error(t('enterDecPass'));
      const envelope = JSON.parse(await file.text()) as EncryptedBackupEnvelope;
      const { data, checksumVerified } = await decryptBackupPayload(envelope, adminPass);
      if (!checksumVerified) throw new Error('Checksum mismatch.');
      const res = await onRestoreState(data);
      if (!res.ok) throw new Error(res.message);
      setBackupMsg(res.message);
      onToast(t('saved'));
    } catch (err) {
      setBackupErr(err instanceof Error ? err.message : t('failed'));
    } finally {
      if (fileRef.current) fileRef.current.value = '';
    }
  };

  return (
    <div>
      {/* Language & Theme */}
      <div className="card">
        <div className="two">
          <div>
            <h2>{t('language')}</h2>
            <select value={lang} onChange={(e) => onChangeLang(e.target.value as Lang)}>
              <option value="en">English</option>
              <option value="ar">العربية</option>
              <option value="fr">Français</option>
            </select>
          </div>
          <div>
            <h2>{t('themeLabel')}</h2>
            <select value={theme} onChange={(e) => onChangeTheme(e.target.value as 'dark' | 'light')}>
              <option value="dark">{t('themeDark')}</option>
              <option value="light">{t('themeLight')}</option>
            </select>
          </div>
        </div>
      </div>

      {/* Original WiFi & Credentials Card */}
      <div className="card">
        <h2>{t('wifi')}</h2>
        <div className="two">
          <div>
            <label className="f" style={{ marginTop: 4 }}>{t('staSsid')}</label>
            <input value={staSsid} onChange={(e) => setStaSsid(e.target.value)} />
          </div>
          <div>
            <label className="f" style={{ marginTop: 4 }}>{t('staPass')}</label>
            <input type="password" value={staPass} onChange={(e) => setStaPass(e.target.value)} placeholder={t('keepBlank')} />
          </div>
          <div>
            <label className="f" style={{ marginTop: 4 }}>{t('apSsid')}</label>
            <input value={apSsid} onChange={(e) => setApSsid(e.target.value)} />
          </div>
          <div>
            <label className="f" style={{ marginTop: 4 }}>{t('apPass')}</label>
            <input type="password" value={apPass} onChange={(e) => setApPass(e.target.value)} placeholder={t('keepBlank')} />
          </div>
        </div>
        <label className="f">{t('adminPass')}</label>
        <input type="password" value={adminPass} onChange={(e) => setAdminPass(e.target.value)} />
        <div className="note">{t('keepBlank')}</div>
        <div style={{ marginTop: 12 }}>
          <button type="button" className="btn primary" onClick={() => onToast(t('saved'))}>
            {t('saveReboot')}
          </button>
        </div>
      </div>

      {/* NEW: Monthly Plan, Cycle Reset & 2-Day Grace Retention Card */}
      <div className="card">
        <h2>{t('monthlyPlanTitle')}</h2>
        <div className="muted small" style={{ marginBottom: 10 }}>
          {t('monthlyPlanHint')}
        </div>

        <label className="f" style={{ marginTop: 4 }}>{t('planName')}</label>
        <input value={planName} onChange={(e) => setPlanName(e.target.value)} />

        <div className="two">
          <div>
            <label className="f">{t('planCapGb')}</label>
            <input
              type="number"
              min={1}
              value={totalCapGB}
              onChange={(e) => setTotalCapGB(Number(e.target.value))}
            />
          </div>
          <div>
            <label className="f">{t('cycleDay')}</label>
            <input
              type="number"
              min={1}
              max={28}
              value={cycleDay}
              onChange={(e) => setCycleDay(Number(e.target.value))}
            />
          </div>
          <div>
            <label className="f">{t('defaultUserGb')}</label>
            <input
              type="number"
              min={0}
              value={defaultUserGB}
              onChange={(e) => setDefaultUserGB(Number(e.target.value))}
            />
          </div>
          <div style={{ display: 'flex', alignItems: 'flex-end', paddingBottom: 6 }}>
            <label className="row" style={{ cursor: 'pointer' }}>
              <input
                type="checkbox"
                checked={autoRollover}
                onChange={(e) => setAutoRollover(e.target.checked)}
              />
              <span className="small">{t('autoRollover')}</span>
            </label>
          </div>
        </div>

        <div className="row wrap" style={{ marginTop: 14 }}>
          <button
            type="button"
            className="btn primary"
            onClick={async () => {
              await onSavePlan({
                planName,
                totalCapGB,
                cycleStartDay: cycleDay,
                defaultUserCapGB: defaultUserGB,
                autoRolloverEnabled: autoRollover,
              });
              onToast(t('saved'));
            }}
          >
            {t('save')}
          </button>
          <button
            type="button"
            className="btn"
            onClick={async () => {
              await onRolloverMonth();
              onToast(t('saved'));
            }}
          >
            {t('closeMonthBtn')}
          </button>
          <button
            type="button"
            className="btn danger"
            disabled={state.graceArchives.length === 0}
            onClick={async () => {
              await onPruneGrace();
              onToast(t('saved'));
            }}
          >
            {t('pruneGraceBtn')}
          </button>
        </div>

        {/* 48-Hour Grace Archive */}
        <h2 style={{ marginTop: 18 }}>{t('graceBoxTitle')}</h2>
        {state.graceArchives.length === 0 ? (
          <div className="muted small">{t('graceEmpty')}</div>
        ) : (
          state.graceArchives.map((g) => (
            <div
              key={g.archiveId}
              style={{ borderTop: '1px solid var(--line)', paddingTop: 10, marginTop: 8 }}
            >
              <div className="row between wrap">
                <b>{g.cycleLabel}</b>
                <span className="tag amber">
                  {g.remainingHours}h left · {g.rawSliceCount} slices · {(g.totalPlanMB / 1024).toFixed(2)} GB
                </span>
              </div>
            </div>
          ))
        )}

        {/* Permanent Monthly Digests */}
        <h2 style={{ marginTop: 18 }}>{t('permBoxTitle')}</h2>
        {state.monthlySummaries.length === 0 ? (
          <div className="muted small">{t('graceEmpty')}</div>
        ) : (
          state.monthlySummaries.map((m) => (
            <div
              key={m.cycleLabel}
              style={{ borderTop: '1px solid var(--line)', paddingTop: 10, marginTop: 10 }}
            >
              <div className="row between wrap" style={{ marginBottom: 6 }}>
                <b>
                  {m.cycleLabel} · {m.totalUsedGB} / {m.planCapGB} GB
                </b>
                <span className={`tag ${m.rawLogsPurged ? 'green' : 'amber'}`}>
                  {m.rawLogsPurged ? t('rawPurgedTag') : t('graceActiveTag')}
                </span>
              </div>
              <div className="table-wrap">
                <table className="rtable">
                  <thead>
                    <tr>
                      <th>{t('user')}</th>
                      <th>{t('mostUsedApp')}</th>
                      <th style={{ textAlign: 'end' }}>{t('usage')}</th>
                    </tr>
                  </thead>
                  <tbody>
                    {m.userSummaries.map((u) => (
                      <tr key={u.mac}>
                        <td>{u.name}</td>
                        <td>
                          {u.mostUsedApp} <span className="muted small">({u.mostUsedAppGB} GB)</span>
                        </td>
                        <td className="n" style={{ textAlign: 'end' }}>
                          {u.totalUsedGB} GB ({u.sharePct}%)
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </div>
          ))
        )}
      </div>

      {/* NEW: Encrypted Backup Anytime Using UI Password */}
      <div className="card">
        <h2>{t('backupTitle')}</h2>
        <div className="muted small" style={{ marginBottom: 10 }}>
          {t('backupHint')}
        </div>

        <label className="f" style={{ marginTop: 4 }}>{t('backupPass')}</label>
        <input
          type="password"
          value={adminPass}
          onChange={(e) => setAdminPass(e.target.value)}
        />

        <div className="row wrap" style={{ marginTop: 12 }}>
          <button type="button" className="btn primary" onClick={handleEncryptedDownload}>
            {t('backupDownload')}
          </button>
          <input
            ref={fileRef}
            type="file"
            accept=".vortaxbak,.json"
            style={{ display: 'none' }}
            onChange={(e) => {
              const f = e.target.files?.[0];
              if (f) handleEncryptedRestore(f);
            }}
          />
          <button type="button" className="btn" onClick={() => fileRef.current?.click()}>
            {t('backupRestore')}
          </button>
        </div>

        {backupMsg && <div className="msg ok">{backupMsg}</div>}
        {backupErr && <div className="msg err">{backupErr}</div>}
      </div>

      {/* NEW: Telegram Bot Auto-Backup */}
      <div className="card">
        <h2>{t('tgBackupTitle')}</h2>
        <div className="two">
          <div>
            <label className="f" style={{ marginTop: 4 }}>
              {t('tgToken')} {state.telegram.maskedToken ? `(${state.telegram.maskedToken})` : ''}
            </label>
            <input
              type="password"
              value={tgToken}
              onChange={(e) => setTgToken(e.target.value)}
              placeholder={t('keepBlank')}
            />
          </div>
          <div>
            <label className="f" style={{ marginTop: 4 }}>{t('tgChat')}</label>
            <input value={tgChat} onChange={(e) => setTgChat(e.target.value)} placeholder="e.g. 582910432" />
          </div>
        </div>

        <label className="f">{t('tgInterval')}</label>
        <select value={tgSchedule} onChange={(e) => setTgSchedule(Number(e.target.value))}>
          <option value={0}>{t('rolloverOnly')}</option>
          <option value={24}>{t('every24h')}</option>
          <option value={168}>{t('every7d')}</option>
        </select>

        <div style={{ marginTop: 10 }}>
          <label className="row" style={{ cursor: 'pointer', marginBottom: 6 }}>
            <input
              type="checkbox"
              checked={tgAutoMonth}
              onChange={(e) => setTgAutoMonth(e.target.checked)}
            />
            <span className="small">{t('tgAutoMonth')}</span>
          </label>
          <label className="row" style={{ cursor: 'pointer' }}>
            <input
              type="checkbox"
              checked={tgAttachEnc}
              onChange={(e) => setTgAttachEnc(e.target.checked)}
            />
            <span className="small">{t('tgAttachEnc')}</span>
          </label>
        </div>

        <div className="row wrap" style={{ marginTop: 12 }}>
          <button
            type="button"
            className="btn primary"
            onClick={async () => {
              const cfg = await onSaveTelegram({
                botToken: tgToken || undefined,
                chatId: tgChat,
                autoBackupOnRollover: tgAutoMonth,
                scheduleHours: tgSchedule,
                encryptTelegramBackups: tgAttachEnc,
              });
              setTgToken('');
              setTgMsg(cfg.lastStatus);
              onToast(t('saved'));
            }}
          >
            {t('save')}
          </button>
          <button
            type="button"
            className="btn"
            onClick={async () => {
              try {
                let envStr: string | undefined;
                if (tgAttachEnc) {
                  if (!adminPass.trim()) {
                    setTgMsg(t('needPassForTg'));
                    return;
                  }
                  const payload = await (await fetch('/api/backup/payload')).json();
                  envStr = JSON.stringify(await encryptBackupPayload(payload, adminPass));
                }
                const res = await onSendTelegramBackup(envStr);
                setTgMsg(res.detail);
              } catch (err) {
                setTgMsg(err instanceof Error ? err.message : t('failed'));
              }
            }}
          >
            {t('tgSendNow')}
          </button>
        </div>
        {tgMsg && <div className="note">{tgMsg}</div>}
      </div>
    </div>
  );
};
