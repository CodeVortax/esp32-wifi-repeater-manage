import React, { useEffect, useState } from 'react';
import type { Lang } from '../i18n';
import { I18N } from '../i18n';
import type { DeviceRecord } from '../types';

interface AppsTabProps {
  devices: DeviceRecord[];
  lang: Lang;
}

interface RangeData {
  fromMs: number;
  toMs: number;
  sliceCount: number;
  totalMB: number;
  avgMbps: number;
  avgLatencyMs: number;
  deviceBreakdown: Array<{ id: string; name: string; mac: string; usedMB: number; topApp: string; sharePct: number }>;
  appBreakdown: Array<{ name: string; usedMB: number; sharePct: number }>;
  series: Array<{ timestamp: number; label: string; mb: number; avgLatencyMs: number }>;
}

function toLocalInput(ms: number): string {
  const d = new Date(ms);
  const p = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}`;
}

export const AppsTab: React.FC<AppsTabProps> = ({ devices, lang }) => {
  const t = (k: string) => I18N[lang][k] || k;
  const [scope, setScope] = useState<string>('all');
  const [fromStr, setFromStr] = useState<string>(() => toLocalInput(Date.now() - 6 * 3600 * 1000));
  const [toStr, setToStr] = useState<string>(() => toLocalInput(Date.now()));
  const [range, setRange] = useState<RangeData | null>(null);
  const [hoverIdx, setHoverIdx] = useState<number | null>(null);

  const runRange = async (fMs?: number, tMs?: number, target?: string) => {
    const start = fMs ?? new Date(fromStr).getTime();
    const end = tMs ?? new Date(toStr).getTime();
    const sc = target ?? scope;
    if (!Number.isFinite(start) || !Number.isFinite(end) || start >= end) return;
    const resp = await fetch(`/api/usage/range?from=${start}&to=${end}&deviceId=${encodeURIComponent(sc)}`);
    if (resp.ok) setRange(await resp.json());
  };

  useEffect(() => {
    runRange();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const applyPreset = (hours: number) => {
    const end = Date.now();
    const start = end - hours * 3600 * 1000;
    setFromStr(toLocalInput(start));
    setToStr(toLocalInput(end));
    runRange(start, end, scope);
  };

  const exportCsv = () => {
    if (!range) return;
    const lines = [
      ['Timestamp', 'Label', 'Usage_MB', 'Latency_ms'].join(','),
      ...range.series.map((s) => [new Date(s.timestamp).toISOString(), s.label, s.mb, s.avgLatencyMs].join(',')),
    ];
    const blob = new Blob([lines.join('\n')], { type: 'text/csv;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `esp32-usage-${scope}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  };

  // Downsample visually to max 48 bars for crisp .chart layout
  const bars = (() => {
    if (!range || !range.series.length) return [];
    const s = range.series;
    if (s.length <= 48) return s;
    const step = Math.ceil(s.length / 48);
    const out: RangeData['series'] = [];
    for (let i = 0; i < s.length; i += step) {
      const chunk = s.slice(i, i + step);
      out.push({
        timestamp: chunk[0].timestamp,
        label: chunk[0].label,
        mb: Number(chunk.reduce((a, b) => a + b.mb, 0).toFixed(2)),
        avgLatencyMs: Number((chunk.reduce((a, b) => a + b.avgLatencyMs, 0) / chunk.length).toFixed(1)),
      });
    }
    return out;
  })();

  const maxBar = Math.max(1, ...bars.map((b) => b.mb));
  const maxApp = Math.max(1, ...(range?.appBreakdown.map((a) => a.usedMB) || [1]));

  return (
    <div>
      {/* Precise Time-Window Query Card */}
      <div className="card">
        <div className="row between wrap" style={{ marginBottom: 8 }}>
          <h2 style={{ margin: 0 }}>{t('timeRangeTitle')}</h2>
          <div className="row wrap" style={{ gap: 6 }}>
            {[1, 6, 12, 24, 48].map((h) => (
              <button key={h} type="button" className="btn sm" onClick={() => applyPreset(h)}>
                {h}h
              </button>
            ))}
          </div>
        </div>
        <div className="muted small" style={{ marginBottom: 10 }}>
          {t('timeRangeHint')}
        </div>

        <div className="two">
          <div>
            <label className="f" style={{ marginTop: 4 }}>{t('fromTime')}</label>
            <input type="datetime-local" value={fromStr} onChange={(e) => setFromStr(e.target.value)} />
          </div>
          <div>
            <label className="f" style={{ marginTop: 4 }}>{t('toTime')}</label>
            <input type="datetime-local" value={toStr} onChange={(e) => setToStr(e.target.value)} />
          </div>
        </div>

        <label className="f">{t('user')}</label>
        <div className="row wrap">
          <select
            style={{ flex: 1, minWidth: 180 }}
            value={scope}
            onChange={(e) => {
              setScope(e.target.value);
              runRange(undefined, undefined, e.target.value);
            }}
          >
            <option value="all">{t('thisDevice')}</option>
            {devices.map((d) => (
              <option key={d.id} value={d.id}>
                {d.name} ({d.mac})
              </option>
            ))}
          </select>
          <button type="button" className="btn primary" onClick={() => runRange()}>
            {t('queryBtn')}
          </button>
          <button type="button" className="btn" onClick={exportCsv} disabled={!range}>
            {t('exportCsv')}
          </button>
        </div>

        {range && (
          <div style={{ marginTop: 14 }}>
            <div className="stats" style={{ marginBottom: 10 }}>
              <div className="stat">
                <span>{t('windowTotal')}</span>
                <b>
                  {range.totalMB >= 1024
                    ? `${(range.totalMB / 1024).toFixed(2)} GB`
                    : `${range.totalMB.toFixed(1)} MB`}
                </b>
              </div>
              <div className="stat">
                <span>{t('avgSpeed')}</span>
                <b>{range.avgMbps} Mbps</b>
              </div>
              <div className="stat">
                <span>{t('slicesLabel')}</span>
                <b>{range.sliceCount}</b>
              </div>
              <div className="stat">
                <span>{t('latency')}</span>
                <b>{range.avgLatencyMs} ms</b>
              </div>
            </div>

            {hoverIdx !== null && bars[hoverIdx] && (
              <div className="muted small" style={{ marginBottom: 4 }}>
                {bars[hoverIdx].label} · {bars[hoverIdx].mb} MB · {bars[hoverIdx].avgLatencyMs} ms
              </div>
            )}

            <div className="chart">
              {bars.map((b, idx) => (
                <div
                  key={b.timestamp}
                  className="col"
                  onMouseEnter={() => setHoverIdx(idx)}
                  onMouseLeave={() => setHoverIdx(null)}
                  title={`${b.label}: ${b.mb} MB`}
                >
                  <i
                    style={{
                      height: `${Math.max(4, (b.mb / maxBar) * 100).toFixed(1)}%`,
                      background: 'var(--blue)',
                    }}
                  />
                </div>
              ))}
            </div>
            <div className="axis">
              <span>{bars[0]?.label}</span>
              <span>{bars[Math.floor(bars.length / 2)]?.label}</span>
              <span>{bars[bars.length - 1]?.label}</span>
            </div>
          </div>
        )}
      </div>

      {/* By App Table in exact original style */}
      {range && (
        <>
          <div className="card">
            <h2>{t('byApp')}</h2>
            {range.appBreakdown.length === 0 ? (
              <div className="empty">{t('noUsers')}</div>
            ) : (
              <div className="table-wrap">
                <table className="rtable">
                  <tbody>
                    {range.appBreakdown.map((a, idx) => (
                      <tr key={a.name}>
                        <td>
                          {a.name} <span className="tag">{a.sharePct}%</span>
                        </td>
                        <td style={{ width: '35%' }}>
                          <div className="hbar">
                            <i
                              style={{
                                width: `${((a.usedMB / maxApp) * 100).toFixed(1)}%`,
                                background: `var(--c${idx % 8})`,
                              }}
                            />
                          </div>
                        </td>
                        <td className="n">{a.usedMB.toFixed(1)} MB</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )}
          </div>

          <div className="card">
            <h2>{t('users')}</h2>
            {range.deviceBreakdown.length === 0 ? (
              <div className="empty">{t('noUsers')}</div>
            ) : (
              <div className="table-wrap">
                <table className="rtable">
                  <tbody>
                    {range.deviceBreakdown.map((d) => (
                      <tr key={d.id}>
                        <td>
                          {d.name} <span className="muted small dom">({d.mac})</span>
                        </td>
                        <td className="muted small">{d.topApp}</td>
                        <td className="n">{d.usedMB.toFixed(1)} MB</td>
                        <td className="n muted">{d.sharePct}%</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )}
          </div>
        </>
      )}
    </div>
  );
};
