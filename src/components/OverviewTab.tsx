import React, { useState } from 'react';
import type { Lang } from '../i18n';
import { I18N } from '../i18n';
import type { RepeaterSystemState } from '../types';

interface OverviewTabProps {
  state: RepeaterSystemState;
  lang: Lang;
  internetOff: boolean;
  onToggleInternet: () => void;
  optimizing: boolean;
  onOptimizeChannels: () => void;
  onPingNode: (nodeId: string) => void;
  onConnectBridge: (url: string, password: string) => Promise<void>;
  onSyncBridge: () => Promise<void>;
  onDisconnectBridge: () => Promise<void>;
}

const CAT_WEIGHTS = [0.34, 0.14, 0.11, 0.16, 0.04, 0.09, 0.07, 0.05];

function fmtMB(mb: number): string {
  if (mb >= 1024) return `${(mb / 1024).toFixed(2)} GB`;
  return `${mb.toFixed(1)} MB`;
}

export const OverviewTab: React.FC<OverviewTabProps> = ({
  state,
  lang,
  internetOff,
  onToggleInternet,
  optimizing,
  onOptimizeChannels,
  onPingNode,
  onConnectBridge,
  onSyncBridge,
  onDisconnectBridge,
}) => {
  const t = (k: string) => I18N[lang][k] || k;
  const [hoverHour, setHoverHour] = useState<number | null>(null);
  const [bridgeUrl, setBridgeUrl] = useState(state.liveBridgeUrl || 'http://192.168.4.1');
  const [bridgePass, setBridgePass] = useState('');
  const [connecting, setConnecting] = useState(false);
  const [connectErr, setConnectErr] = useState('');

  const rootNode = state.nodes[0];
  const onlineCount = state.devices.filter((d) => d.online).length;
  const todayTotalMB = state.devices.reduce((a, d) => a + d.dailyUsedMB, 0);
  const monthTotalGB = state.devices.reduce((a, d) => a + d.monthlyUsedMB, 0) / 1024;
  const monthPct = Math.min(100, (monthTotalGB / Math.max(1, state.plan.totalCapGB)) * 100);

  const topUsers = [...state.devices]
    .sort((a, b) => b.monthlyUsedMB - a.monthlyUsedMB)
    .slice(0, 5);
  const maxUserMB = Math.max(1, ...topUsers.map((u) => u.monthlyUsedMB));

  // Build 24 hourly stacked columns from real recorded 5-minute slices
  const now = Date.now();
  const last24h = state.timeSlices.filter((s) => s.timestamp >= now - 24 * 3600 * 1000);
  const hours = Array.from({ length: 24 }, (_, hr) => {
    const matching = last24h.filter((s) => new Date(s.timestamp).getHours() === hr);
    const total = matching.reduce((a, s) => a + s.totalMB, 0);
    return CAT_WEIGHTS.map((w) => total * w);
  });
  const hourTotals = hours.map((r) => r.reduce((a, b) => a + b, 0));
  const hasHours = hourTotals.some((v) => v > 0);
  const maxHour = Math.max(1, ...hourTotals);
  const peakHour = hourTotals.indexOf(Math.max(...hourTotals));

  const maxScore = Math.max(100, ...(state.spectrum.length ? state.spectrum.map((s) => s.interferenceScore) : [100]));

  const handleConnect = async (e: React.FormEvent) => {
    e.preventDefault();
    setConnectErr('');
    setConnecting(true);
    try {
      await onConnectBridge(bridgeUrl, bridgePass);
      setBridgePass('');
    } catch (err) {
      setConnectErr(err instanceof Error ? err.message : t('failed'));
    } finally {
      setConnecting(false);
    }
  };

  return (
    <div>
      {/* Real ESP32 Repeater Connection Bar */}
      <div className="card">
        <div className="row between wrap" style={{ marginBottom: 8 }}>
          <div>
            <h2 style={{ margin: '0 0 2px' }}>
              {state.connected ? `${t('connectedTo')}: ${state.liveBridgeUrl}` : t('connectTitle')}
            </h2>
            <div className="muted small">
              {state.connected
                ? `${t('lastSynced')}: ${state.lastSyncedAt?.slice(11, 19) || t('justNow')}`
                : t('connectHint')}
            </div>
          </div>
          {state.connected && (
            <div className="row" style={{ gap: 6 }}>
              <button type="button" className="btn sm primary" onClick={onSyncBridge}>
                {t('syncNow')}
              </button>
              <button type="button" className="btn sm danger" onClick={onDisconnectBridge}>
                {t('disconnectBtn')}
              </button>
            </div>
          )}
        </div>

        {!state.connected && (
          <form onSubmit={handleConnect} className="two" style={{ marginTop: 8 }}>
            <input
              value={bridgeUrl}
              onChange={(e) => setBridgeUrl(e.target.value)}
              placeholder="http://192.168.4.1"
              autoCapitalize="off"
            />
            <div className="row" style={{ gap: 6 }}>
              <input
                type="password"
                value={bridgePass}
                onChange={(e) => setBridgePass(e.target.value)}
                placeholder={t('espPassPh')}
                style={{ flex: 1 }}
              />
              <button type="submit" className="btn primary" disabled={connecting}>
                {connecting ? '...' : t('connectBtn')}
              </button>
            </div>
          </form>
        )}
        {connectErr && <div className="msg err">{connectErr}</div>}
      </div>

      {/* Original 4-stat row */}
      <div className="stats">
        <div className="stat">
          <span>{t('internet')}</span>
          <b>
            <span className="row" style={{ fontSize: '1rem', marginTop: 2 }}>
              <span className={`dot ${state.connected && !internetOff ? 'on' : 'off'}`} />
              <span>{rootNode?.staSsid || '—'}</span>
            </span>
          </b>
        </div>
        <div className="stat">
          <span>{t('devices')}</span>
          <b>
            {onlineCount} / {state.devices.length}
          </b>
        </div>
        <div className="stat">
          <span>{t('usedToday')}</span>
          <b>{fmtMB(todayTotalMB)}</b>
        </div>
        <div className="stat">
          <span>
            {t('signal')} / {t('latency')}
          </span>
          <b>
            {rootNode ? `${rootNode.rssiDbm} dBm · ${rootNode.latencyMs} ms` : '—'}
          </b>
        </div>
      </div>

      {/* Internet On/Off Card */}
      <div className="card">
        <div className="row between wrap">
          <div>
            <h2 style={{ margin: '0 0 4px' }}>{t('inetTitle')}</h2>
            <span className="row">
              <span className={`dot ${internetOff ? 'off' : state.connected ? 'on' : ''}`} />
              <span>{internetOff ? t('inetOff') : t('inetOn')}</span>
            </span>
          </div>
          <button
            type="button"
            className={`btn ${internetOff ? 'primary' : 'danger'}`}
            disabled={!state.connected}
            onClick={onToggleInternet}
          >
            {internetOff ? t('inetBtnOn') : t('inetBtnOff')}
          </button>
        </div>
        <div className="note">{t('inetNote')}</div>
      </div>

      {/* Auto Channel & Latency Optimizer Card (mobile overflow fixed!) */}
      <div className="card">
        <div className="row between wrap" style={{ marginBottom: 10 }}>
          <div style={{ minWidth: 0, flex: '1 1 220px' }}>
            <h2 style={{ margin: '0 0 4px' }}>{t('rfTitle')}</h2>
            <div className="muted small">{t('rfDesc')}</div>
          </div>
          <button
            type="button"
            className="btn primary"
            disabled={optimizing || !state.connected}
            onClick={onOptimizeChannels}
          >
            {optimizing ? t('rfBusy') : t('rfBtn')}
          </button>
        </div>

        {state.spectrum.length > 0 ? (
          <>
            <div className="chart" style={{ height: 84 }}>
              {state.spectrum.map((ch) => {
                const pct = Math.max(10, Math.round((ch.interferenceScore / maxScore) * 100));
                const color = ch.recommended
                  ? 'var(--green)'
                  : ch.interferenceScore > 120
                  ? 'var(--red)'
                  : ch.interferenceScore > 75
                  ? 'var(--amber)'
                  : 'var(--blue)';
                return (
                  <div
                    key={ch.channel}
                    className="col"
                    title={`CH ${ch.channel}: score ${ch.interferenceScore}, ${ch.overlappingAps} APs, ${ch.noiseFloorDbm} dBm`}
                  >
                    <i style={{ height: `${pct}%`, background: color }} />
                  </div>
                );
              })}
            </div>
            <div className="axis">
              {state.spectrum.map((ch) => {
                const active = state.nodes.some((n) => n.channel === ch.channel);
                return (
                  <span
                    key={ch.channel}
                    style={{
                      color: active ? 'var(--blue)' : undefined,
                      fontWeight: active ? 700 : undefined,
                    }}
                  >
                    {ch.channel}
                    {active ? '*' : ''}
                  </span>
                );
              })}
            </div>
          </>
        ) : (
          <div className="empty">{t('noSpectrum')}</div>
        )}

        {state.nodes.length > 0 && (
          <div className="table-wrap" style={{ marginTop: 10 }}>
            <table className="rtable">
              <thead>
                <tr>
                  <th>{t('repeater')}</th>
                  <th>{t('channel')}</th>
                  <th>{t('signal')}</th>
                  <th>{t('latency')}</th>
                  <th style={{ textAlign: 'end' }}>{t('ping')}</th>
                </tr>
              </thead>
              <tbody>
                {state.nodes.map((n) => (
                  <tr key={n.id}>
                    <td>
                      <span className={`dot ${n.status === 'Optimal' ? 'on' : 'warn'}`} /> {n.name}{' '}
                      <span className="tag">{n.psDisabled ? 'PS_NONE' : 'PS_MIN'}</span>
                    </td>
                    <td className="n">
                      CH {n.channel} · {n.bandwidthMHz}MHz
                    </td>
                    <td className="n">{n.rssiDbm} dBm</td>
                    <td className="n">
                      {n.latencyMs} ms <span className="muted small">({n.jitterMs} ms)</span>
                    </td>
                    <td style={{ textAlign: 'end' }}>
                      <button type="button" className="btn sm" onClick={() => onPingNode(n.id)}>
                        {t('ping')}
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      {/* Monthly Plan Progress Card */}
      <div className="card">
        <div className="row between wrap" style={{ marginBottom: 8 }}>
          <h2 style={{ margin: 0 }}>{state.plan.planName}</h2>
          <span className="n small muted">
            {monthTotalGB.toFixed(2)} / {state.plan.totalCapGB} GB ({monthPct.toFixed(0)}%)
          </span>
        </div>
        <div className="hbar" style={{ height: 8 }}>
          <i
            style={{
              width: `${monthPct.toFixed(1)}%`,
              background: monthPct > 90 ? 'var(--red)' : monthPct > 75 ? 'var(--amber)' : 'var(--blue)',
            }}
          />
        </div>
        <div className="row between wrap note">
          <span>
            {t('cycleDay')}: {state.plan.cycleStartDay}
          </span>
          <span>
            {state.graceArchives.length > 0
              ? `${state.graceArchives.length} × ${t('graceCountLabel')}`
              : `${state.monthlySummaries.length} ${t('archivedMonthsLabel')}`}
          </span>
        </div>
      </div>

      {/* Hourly Chart Card */}
      <div className="card">
        <div className="row between wrap">
          <h2 style={{ margin: 0 }}>{t('hourly')}</h2>
          {hoverHour !== null && hasHours && (
            <span className="muted small n">
              {String(hoverHour).padStart(2, '0')}:00 · {fmtMB(hourTotals[hoverHour])}
            </span>
          )}
        </div>
        {!hasHours ? (
          <div className="empty">{t('noData')}</div>
        ) : (
          <>
            <div className="chart">
              {hours.map((cols, hr) => (
                <div
                  key={hr}
                  className="col"
                  onMouseEnter={() => setHoverHour(hr)}
                  onMouseLeave={() => setHoverHour(null)}
                  title={`${String(hr).padStart(2, '0')}:00  ${fmtMB(hourTotals[hr])}`}
                >
                  {cols.map((v, c) =>
                    v > 0 ? (
                      <i key={c} style={{ height: `${((v / maxHour) * 100).toFixed(2)}%`, background: `var(--c${c})` }} />
                    ) : null
                  )}
                </div>
              ))}
            </div>
            <div className="axis">
              {[0, 3, 6, 9, 12, 15, 18, 21].map((h) => (
                <span key={h}>{String(h).padStart(2, '0')}</span>
              ))}
            </div>
            <div className="muted small" style={{ marginTop: 6 }}>
              {t('peak')}: {String(peakHour).padStart(2, '0')}:00 ({fmtMB(hourTotals[peakHour])})
            </div>
          </>
        )}
      </div>

      {/* Top Users Card */}
      <div className="card">
        <h2>{t('users')}</h2>
        {topUsers.length === 0 ? (
          <div className="empty">{t('noUsers')}</div>
        ) : (
          <div className="table-wrap">
            <table className="rtable">
              <tbody>
                {topUsers.map((d) => (
                  <tr key={d.id}>
                    <td>
                      <span className={`dot ${d.online ? 'on' : ''}`} /> {d.name}
                    </td>
                    <td style={{ width: '38%' }}>
                      <div className="hbar">
                        <i style={{ width: `${((d.monthlyUsedMB / maxUserMB) * 100).toFixed(1)}%` }} />
                      </div>
                    </td>
                    <td className="n">{fmtMB(d.monthlyUsedMB)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
    </div>
  );
};
