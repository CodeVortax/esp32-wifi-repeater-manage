import React, { useState } from 'react';
import type { Lang } from '../i18n';
import { I18N } from '../i18n';
import type { DeviceRecord } from '../types';

interface UsersTabProps {
  devices: DeviceRecord[];
  lang: Lang;
  onDeviceAction: (
    deviceId: string,
    action: 'block' | 'unblock' | 'whitelist' | 'unwhitelist' | 'setCap',
    capMB?: number
  ) => Promise<void>;
}

interface RangeResponse {
  totalMB: number;
  avgMbps: number;
  avgLatencyMs: number;
  sliceCount: number;
  appBreakdown: Array<{ name: string; usedMB: number; sharePct: number }>;
}

function fmtMB(mb: number): string {
  if (mb >= 1024) return `${(mb / 1024).toFixed(2)} GB`;
  return `${mb.toFixed(0)} MB`;
}

function toLocalInput(ms: number): string {
  const d = new Date(ms);
  const p = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}`;
}

export const UsersTab: React.FC<UsersTabProps> = ({ devices, lang, onDeviceAction }) => {
  const t = (k: string) => I18N[lang][k] || k;
  const [editDev, setEditDev] = useState<DeviceRecord | null>(null);
  const [capInput, setCapInput] = useState<string>('0');

  const [detailDev, setDetailDev] = useState<DeviceRecord | null>(null);
  const [fromStr, setFromStr] = useState<string>(() => toLocalInput(Date.now() - 6 * 3600 * 1000));
  const [toStr, setToStr] = useState<string>(() => toLocalInput(Date.now()));
  const [detailRange, setDetailRange] = useState<RangeResponse | null>(null);

  const sorted = [...devices].sort((a, b) => Number(b.online) - Number(a.online));

  const openEdit = (d: DeviceRecord) => {
    setEditDev(d);
    setCapInput(String(d.monthlyCapMB));
  };

  const openDetails = async (d: DeviceRecord) => {
    setDetailDev(d);
    const f = new Date(fromStr).getTime();
    const to = new Date(toStr).getTime();
    const r = await fetch(`/api/usage/range?from=${f}&to=${to}&deviceId=${encodeURIComponent(d.id)}`);
    if (r.ok) setDetailRange(await r.json());
  };

  const queryUserRange = async () => {
    if (!detailDev) return;
    const f = new Date(fromStr).getTime();
    const to = new Date(toStr).getTime();
    if (!Number.isFinite(f) || !Number.isFinite(to) || f >= to) return;
    const r = await fetch(`/api/usage/range?from=${f}&to=${to}&deviceId=${encodeURIComponent(detailDev.id)}`);
    if (r.ok) setDetailRange(await r.json());
  };

  if (sorted.length === 0) {
    return <div className="empty">{t('noUsers')}</div>;
  }

  return (
    <div>
      <div className="grid">
        {sorted.map((d, idx) => {
          const apps = Object.entries(d.appBreakdownMB)
            .sort((a, b) => b[1] - a[1])
            .slice(0, 4);
          const totalApps = apps.reduce((acc, [, v]) => acc + v, 0) || 1;

          return (
            <div key={d.id} className="card user">
              <div className="row">
                <div className="avatar">{idx + 1}</div>
                <div style={{ flex: 1, minWidth: 0 }}>
                  <div className="name">{d.name}</div>
                  <div className="muted small dom">
                    {d.ip} · {d.mac} · <span className={`dot ${d.online ? 'on' : ''}`} />{' '}
                    {d.online ? t('online') : t('offlineU')}
                  </div>
                </div>
                <span className="row" style={{ gap: 6 }}>
                  <button type="button" className="btn sm" onClick={() => openDetails(d)}>
                    {t('details')}
                  </button>
                  <button type="button" className="btn sm" onClick={() => openEdit(d)}>
                    {t('rename')}
                  </button>
                </span>
              </div>

              <div className="row between">
                <span className="muted small">{t('thisMonth')}</span>
                <b>{fmtMB(d.monthlyUsedMB)}</b>
              </div>

              <div className="bar">
                {apps.map(([name, val], ci) => (
                  <i
                    key={name}
                    style={{
                      width: `${((val / totalApps) * 100).toFixed(1)}%`,
                      background: `var(--c${ci % 8})`,
                    }}
                  />
                ))}
              </div>

              <div className="small">
                {apps.map(([name, val]) => (
                  <div key={name} className="row between" style={{ padding: '2px 0' }}>
                    <span>{name}</span>
                    <span className="muted">{fmtMB(val)}</span>
                  </div>
                ))}
              </div>

              <div className="row wrap" style={{ gap: 6 }}>
                {d.blocked && <span className="tag red">{t('blocked')}</span>}
                {d.whitelisted && <span className="tag green">{t('trusted')}</span>}
                {d.monthlyCapMB > 0 && (
                  <span className="tag amber">
                    {fmtMB(d.monthlyUsedMB)} / {fmtMB(d.monthlyCapMB)}
                  </span>
                )}
                <span className="tag">
                  ↓ {d.speedDownKbps === 0 ? t('unlimited') : `${d.speedDownKbps / 1000} Mbps`}
                </span>
                <span className={`tag ${d.signalDbm <= -80 ? 'red' : d.signalDbm <= -70 ? 'amber' : ''}`}>
                  {t('signal')} {d.signalDbm} dBm · {d.latencyMs} ms
                </span>
              </div>

              <div className="muted small">
                {t('today')}: {fmtMB(d.dailyUsedMB)} · {t('mostUsedApp')}: {d.topApp}
              </div>
            </div>
          );
        })}
      </div>

      {/* Edit User Modal (exact original dialog style) */}
      {editDev && (
        <div className="modal-backdrop" onClick={() => setEditDev(null)}>
          <div className="modal-box" onClick={(e) => e.stopPropagation()}>
            <h3>{editDev.name}</h3>
            <div className="muted small">
              {editDev.ip} · {editDev.mac}
            </div>

            <label className="f">{t('monthlyLimit')}</label>
            <input
              type="number"
              min={0}
              value={capInput}
              onChange={(e) => setCapInput(e.target.value)}
            />

            <label className="row" style={{ marginTop: 14, cursor: 'pointer' }}>
              <input
                type="checkbox"
                checked={editDev.whitelisted}
                onChange={async (e) => {
                  await onDeviceAction(
                    editDev.id,
                    e.target.checked ? 'whitelist' : 'unwhitelist'
                  );
                  setEditDev({ ...editDev, whitelisted: e.target.checked });
                }}
              />
              <span>{t('whitelist')}</span>
            </label>

            <div className="row between wrap" style={{ marginTop: 18 }}>
              <button
                type="button"
                className={`btn ${editDev.blocked ? '' : 'danger'}`}
                onClick={async () => {
                  await onDeviceAction(editDev.id, editDev.blocked ? 'unblock' : 'block');
                  setEditDev(null);
                }}
              >
                {editDev.blocked ? t('unblockBtn') : t('blockBtn')}
              </button>
              <span className="row">
                <button type="button" className="btn" onClick={() => setEditDev(null)}>
                  {t('cancel')}
                </button>
                <button
                  type="button"
                  className="btn primary"
                  onClick={async () => {
                    await onDeviceAction(editDev.id, 'setCap', Number(capInput) || 0);
                    setEditDev(null);
                  }}
                >
                  {t('save')}
                </button>
              </span>
            </div>
          </div>
        </div>
      )}

      {/* User Details + Precise Time-Window Query Modal */}
      {detailDev && (
        <div className="modal-backdrop" onClick={() => setDetailDev(null)}>
          <div className="modal-box" onClick={(e) => e.stopPropagation()}>
            <div className="row between">
              <h3 style={{ margin: 0 }}>{detailDev.name}</h3>
              <button type="button" className="btn sm" onClick={() => setDetailDev(null)}>
                {t('close')}
              </button>
            </div>
            <div className="muted small">
              {detailDev.ip} · {detailDev.mac}
            </div>

            <div style={{ marginTop: 14 }}>
              <h2 style={{ fontSize: '0.95rem', margin: '0 0 6px' }}>{t('timeRangeTitle')}</h2>
              <div className="two">
                <div>
                  <label className="f" style={{ marginTop: 4 }}>{t('fromTime')}</label>
                  <input
                    type="datetime-local"
                    value={fromStr}
                    onChange={(e) => setFromStr(e.target.value)}
                  />
                </div>
                <div>
                  <label className="f" style={{ marginTop: 4 }}>{t('toTime')}</label>
                  <input
                    type="datetime-local"
                    value={toStr}
                    onChange={(e) => setToStr(e.target.value)}
                  />
                </div>
              </div>
              <div style={{ marginTop: 10 }}>
                <button type="button" className="btn primary sm" onClick={queryUserRange}>
                  {t('queryBtn')}
                </button>
              </div>
            </div>

            {detailRange && (
              <div style={{ marginTop: 14 }}>
                <div className="stats" style={{ marginBottom: 10 }}>
                  <div className="stat">
                    <span>{t('windowTotal')}</span>
                    <b>{detailRange.totalMB.toFixed(1)} MB</b>
                  </div>
                  <div className="stat">
                    <span>{t('avgSpeed')}</span>
                    <b>{detailRange.avgMbps} Mbps</b>
                  </div>
                </div>
                <div className="table-wrap">
                  <table className="rtable">
                    <tbody>
                      {detailRange.appBreakdown.map((a) => (
                        <tr key={a.name}>
                          <td>{a.name}</td>
                          <td className="n">{a.usedMB.toFixed(1)} MB</td>
                          <td className="n muted">{a.sharePct}%</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </div>
            )}
          </div>
        </div>
      )}
    </div>
  );
};
