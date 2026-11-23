import React, { useState } from 'react';
import type { Lang } from '../i18n';
import { I18N } from '../i18n';
import type { RepeaterSystemState } from '../types';
import { FIRMWARE_MODULES } from '../firmware/esp32Templates';
import { buildZipBlob } from '../utils/zipBuilder';

interface SystemTabProps {
  state: RepeaterSystemState;
  lang: Lang;
  onPingHost: (host: string) => Promise<string>;
  onToast: (msg: string) => void;
}

function Meter({ label, used, total, detail }: { label: string; used: number; total: number; detail?: string }) {
  const pct = total > 0 ? Math.min(100, (used / total) * 100) : 0;
  const color = pct > 90 ? 'var(--red)' : pct > 75 ? 'var(--amber)' : 'var(--blue)';
  return (
    <div style={{ margin: '10px 0' }}>
      <div className="row between small wrap">
        <span>{label}</span>
        <span className="muted n">
          {detail || `${(used / 1024).toFixed(0)} KB / ${(total / 1024).toFixed(0)} KB`}
        </span>
      </div>
      <div className="hbar" style={{ marginTop: 4, height: 8 }}>
        <i style={{ width: `${pct.toFixed(1)}%`, background: color }} />
      </div>
    </div>
  );
}

export const SystemTab: React.FC<SystemTabProps> = ({
  state,
  lang,
  onPingHost,
  onToast,
}) => {
  const t = (k: string) => I18N[lang][k] || k;
  const root = state.nodes[0];
  const [txLevel, setTxLevel] = useState('0');
  const [pingHost, setPingHost] = useState('8.8.8.8');
  const [pingOut, setPingOut] = useState('');
  const [pinging, setPinging] = useState(false);
  const [selectedModId, setSelectedModId] = useState<string>(FIRMWARE_MODULES[0].id);
  const [copied, setCopied] = useState(false);

  const activeMod = FIRMWARE_MODULES.find((m) => m.id === selectedModId) || FIRMWARE_MODULES[0];

  const handleDownloadFirmwareZip = () => {
    const blob = buildZipBlob(
      FIRMWARE_MODULES.map((m) => ({
        path: m.path,
        content: m.code,
      }))
    );
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'esp32-wifi-repeater-v2-upgrade.zip';
    a.click();
    URL.revokeObjectURL(url);
    onToast(t('saved'));
  };

  const handleRunPing = async () => {
    setPinging(true);
    setPingOut('');
    try {
      const out = await onPingHost(pingHost || '8.8.8.8');
      setPingOut(out);
    } catch (err) {
      setPingOut(err instanceof Error ? err.message : t('failed'));
    } finally {
      setPinging(false);
    }
  };

  return (
    <div>
      {/* Real Hardware Metrics */}
      <div className="stats">
        <div className="stat">
          <span>{t('version')}</span>
          <b style={{ fontSize: '1rem' }}>{state.firmwareVersion}</b>
        </div>
        <div className="stat">
          <span>{t('uptime')}</span>
          <b>
            {root
              ? `${Math.floor(root.uptimeSec / 3600)}h ${Math.floor((root.uptimeSec % 3600) / 60)}m`
              : '—'}
          </b>
        </div>
        <div className="stat">
          <span>{t('freeMem')}</span>
          <b>{root ? `${Math.round(root.freeHeapBytes / 1024)} KB` : '—'}</b>
        </div>
        <div className="stat">
          <span>{t('lastRestart')}</span>
          <b style={{ fontSize: '1rem' }}>
            {root ? `${root.wdtResetsAvoided} ${t('wdtAvoided')}` : t('notConnected')}
          </b>
        </div>
      </div>

      {/* Memory & Partition Architecture */}
      <div className="card">
        <h2>{t('memory')}</h2>
        {root ? (
          <>
            <Meter
              label={t('ram')}
              used={Math.max(0, 327680 - root.freeHeapBytes)}
              total={327680}
              detail={`${Math.round(root.freeHeapBytes / 1024)} KB free (${t('largest')}: ${Math.round(
                root.largestContigBlockBytes / 1024
              )} KB, ${t('lowest')}: ${Math.round(root.minFreeHeapBytes / 1024)} KB)`}
            />
            <Meter
              label={t('staticBlock')}
              used={root.staticBlockUsedBytes}
              total={root.staticBlockTotalBytes}
              detail={`${Math.round(root.staticBlockUsedBytes / 1024)} KB / ${Math.round(
                root.staticBlockTotalBytes / 1024
              )} KB`}
            />
          </>
        ) : (
          <div className="empty" style={{ padding: '10px 0' }}>
            {t('ramConnectHint')}
          </div>
        )}
        <div className="note">{t('partNote')}</div>
      </div>

      {/* Radio TX Power & Real Ping Test */}
      <div className="card">
        <h2>{t('radio')}</h2>
        <label className="f" style={{ marginTop: 4 }}>{t('txLevel')}</label>
        <select
          value={txLevel}
          onChange={(e) => {
            setTxLevel(e.target.value);
            onToast(t('saved'));
          }}
        >
          <option value="0">{t('tx0')}</option>
          <option value="1">{t('tx1')}</option>
          <option value="2">{t('tx2')}</option>
          <option value="3">{t('tx3')}</option>
        </select>

        <h2 style={{ marginTop: 16 }}>{t('ping')}</h2>
        <div className="row">
          <input
            value={pingHost}
            onChange={(e) => setPingHost(e.target.value)}
            placeholder={t('pingHost')}
          />
          <button type="button" className="btn primary" disabled={pinging || !state.connected} onClick={handleRunPing}>
            {pinging ? '...' : t('run')}
          </button>
        </div>
        {pingOut && <div className="note dom">{pingOut}</div>}
      </div>

      {/* Non-Resetting Error & Event Log */}
      <div className="card">
        <h2>{t('evTitle')}</h2>
        <div className="muted small" style={{ marginBottom: 8 }}>
          {t('evNote')}
        </div>
        {state.errorLogs.length === 0 ? (
          <div className="empty">{t('noErrorsLogged')}</div>
        ) : (
          <div className="table-wrap">
            <table className="rtable">
              <tbody>
                {state.errorLogs.slice(0, 15).map((ev) => (
                  <tr key={ev.id}>
                    <td className="muted small n" style={{ verticalAlign: 'top' }}>
                      {ev.timestamp.slice(11, 19)}
                    </td>
                    <td>
                      <span
                        className={`tag ${
                          ev.severity === 'Recovered'
                            ? 'green'
                            : ev.severity === 'Warning'
                            ? 'amber'
                            : 'blue'
                        }`}
                      >
                        {ev.code}
                      </span>{' '}
                      <span className="small">{ev.message}</span>
                      <div className="muted small">{ev.actionTaken}</div>
                    </td>
                    <td className="n muted small">{ev.heapAtEventKB} KB</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      {/* Upgraded C++ Firmware, Partition Table & GitHub Actions Auto-Release Pack */}
      <div className="card">
        <div className="row between wrap" style={{ marginBottom: 10 }}>
          <div>
            <h2 style={{ margin: '0 0 4px' }}>{t('firmwareSourceTitle')}</h2>
            <div className="muted small dom">
              {t('fwSubtitle')}
            </div>
          </div>
          <button type="button" className="btn primary" onClick={handleDownloadFirmwareZip}>
            {t('downloadZip')}
          </button>
        </div>

        <div className="row wrap" style={{ gap: 6, marginBottom: 10 }}>
          {FIRMWARE_MODULES.map((m) => (
            <button
              key={m.id}
              type="button"
              className={`btn sm ${m.id === activeMod.id ? 'primary' : ''}`}
              onClick={() => setSelectedModId(m.id)}
            >
              {m.path}
            </button>
          ))}
        </div>

        <div className="row between wrap" style={{ marginBottom: 8 }}>
          <span className="small muted">{activeMod.summary}</span>
          <button
            type="button"
            className="btn sm"
            onClick={async () => {
              await navigator.clipboard.writeText(activeMod.code);
              setCopied(true);
              setTimeout(() => setCopied(false), 1500);
            }}
          >
            {copied ? t('copied') : t('copyFile')}
          </button>
        </div>

        <pre className="codebox">
          <code>{activeMod.code}</code>
        </pre>
      </div>
    </div>
  );
};
