import React, { useState } from 'react';
import type { Lang } from '../i18n';
import { I18N } from '../i18n';
import type { DeviceRecord, OptimizationFeatures } from '../types';

interface SpeedTabProps {
  devices: DeviceRecord[];
  features: OptimizationFeatures;
  lang: Lang;
  onToggleFeature: (key: keyof OptimizationFeatures, value: boolean) => Promise<void>;
  onToast: (msg: string) => void;
}

export const SpeedTab: React.FC<SpeedTabProps> = ({
  devices,
  features,
  lang,
  onToggleFeature,
  onToast,
}) => {
  const t = (k: string) => I18N[lang][k] || k;
  const [gDown, setGDown] = useState('0');
  const [gUp, setGUp] = useState('0');
  const [dDown, setDDown] = useState('15');
  const [dUp, setDUp] = useState('4');

  return (
    <div>
      <div className="card">
        <h2>{t('speedTitle')} & QoS</h2>
        <label className="row" style={{ marginBottom: 10, cursor: 'pointer' }}>
          <input
            type="checkbox"
            checked={features.qosLowLatencyLane}
            onChange={(e) => {
              onToggleFeature('qosLowLatencyLane', e.target.checked);
              onToast(t('saved'));
            }}
          />
          <span>{t('qosLane')}</span>
        </label>
        <label className="row" style={{ cursor: 'pointer' }}>
          <input
            type="checkbox"
            checked={features.dnsResponseCache}
            onChange={(e) => {
              onToggleFeature('dnsResponseCache', e.target.checked);
              onToast(t('saved'));
            }}
          />
          <span>{t('zeroAllocDns')}</span>
        </label>
      </div>

      <div className="card">
        <h2>{t('globalCap')}</h2>
        <div className="two">
          <div>
            <label className="f" style={{ marginTop: 4 }}>{t('downMbps')}</label>
            <input type="number" min="0" value={gDown} onChange={(e) => setGDown(e.target.value)} />
          </div>
          <div>
            <label className="f" style={{ marginTop: 4 }}>{t('upMbps')}</label>
            <input type="number" min="0" value={gUp} onChange={(e) => setGUp(e.target.value)} />
          </div>
        </div>

        <h2 style={{ marginTop: 18 }}>{t('defaultCap')}</h2>
        <div className="two">
          <div>
            <label className="f" style={{ marginTop: 4 }}>{t('downMbps')}</label>
            <input type="number" min="0" value={dDown} onChange={(e) => setDDown(e.target.value)} />
          </div>
          <div>
            <label className="f" style={{ marginTop: 4 }}>{t('upMbps')}</label>
            <input type="number" min="0" value={dUp} onChange={(e) => setDUp(e.target.value)} />
          </div>
        </div>

        <div style={{ marginTop: 14 }}>
          <button type="button" className="btn primary" onClick={() => onToast(t('saved'))}>
            {t('saveSpeed')}
          </button>
        </div>
        <div className="note">{t('speedNote')}</div>
      </div>

      <div className="card">
        <h2>{t('perUser')}</h2>
        {devices.length === 0 ? (
          <div className="empty">{t('noUsers')}</div>
        ) : (
          <div className="table-wrap">
            <table className="rtable">
              <thead>
                <tr>
                  <th>{t('user')}</th>
                  <th>{t('down')}</th>
                  <th>{t('up')}</th>
                  <th>{t('latency')}</th>
                </tr>
              </thead>
              <tbody>
                {devices.map((d) => (
                  <tr key={d.id}>
                    <td>
                      {d.name} {d.whitelisted && <span className="tag green">{t('trusted')}</span>}
                    </td>
                    <td className="n">
                      {d.speedDownKbps === 0 ? t('unlimited') : `${(d.speedDownKbps / 1000).toFixed(1)} Mbps`}
                    </td>
                    <td className="n">
                      {d.speedUpKbps === 0 ? t('unlimited') : `${(d.speedUpKbps / 1000).toFixed(1)} Mbps`}
                    </td>
                    <td className="n muted">{d.latencyMs} ms</td>
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
