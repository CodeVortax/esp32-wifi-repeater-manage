import React, { useState } from 'react';
import type { Lang } from '../i18n';
import { I18N } from '../i18n';

interface BlockTabProps {
  lang: Lang;
  onToast: (msg: string) => void;
}

const DNS_PRESETS = [
  { name: 'Router default', a: '', b: '' },
  { name: 'Cloudflare (Fast)', a: '1.1.1.1', b: '1.0.0.1' },
  { name: 'Cloudflare Family', a: '1.1.1.3', b: '1.0.0.3' },
  { name: 'AdGuard DNS', a: '94.140.14.14', b: '94.140.15.15' },
  { name: 'AdGuard Family', a: '94.140.14.15', b: '94.140.15.16' },
];

export const BlockTab: React.FC<BlockTabProps> = ({ lang, onToast }) => {
  const t = (k: string) => I18N[lang][k] || k;
  const [adsEnabled, setAdsEnabled] = useState(true);
  const [adultEnabled, setAdultEnabled] = useState(false);
  const [dnsIdx, setDnsIdx] = useState(1);
  const [siteInput, setSiteInput] = useState('');
  const [hostsText, setHostsText] = useState('');
  const [recentBlocked, setRecentBlocked] = useState<
    Array<{ domain: string; count: number; verdict: number; who: string }>
  >([]);

  const addBlockRule = () => {
    const clean = siteInput.trim().toLowerCase();
    if (!clean || !clean.includes('.')) {
      onToast(t('failed'));
      return;
    }
    setHostsText((prev) => `${prev ? prev.trimEnd() + '\n' : ''}0.0.0.0 ${clean}\n`);
    setRecentBlocked((prev) => [{ domain: clean, count: 1, verdict: 1, who: 'admin' }, ...prev]);
    setSiteInput('');
    onToast(t('saved'));
  };

  return (
    <div>
      <div className="card">
        <h2>{t('adsTitle')}</h2>
        <label className="row" style={{ cursor: 'pointer' }}>
          <input
            type="checkbox"
            checked={adsEnabled}
            onChange={(e) => {
              setAdsEnabled(e.target.checked);
              onToast(t('saved'));
            }}
          />
          <span>{t('adsOn')}</span>
        </label>
        <div className="stats" style={{ marginTop: 12, marginBottom: 0 }}>
          <div className="stat">
            <span>{t('adsCount')}</span>
            <b>{recentBlocked.reduce((a, r) => a + r.count, 0)}</b>
          </div>
          <div className="stat">
            <span>{t('hostsRules')}</span>
            <b>{hostsText ? hostsText.trim().split('\n').filter(Boolean).length : 0}</b>
          </div>
        </div>
      </div>

      <div className="card">
        <h2>{t('adultTitle')}</h2>
        <label className="row" style={{ cursor: 'pointer' }}>
          <input
            type="checkbox"
            checked={adultEnabled}
            onChange={(e) => {
              setAdultEnabled(e.target.checked);
              onToast(t('saved'));
            }}
          />
          <span>{t('adultOn')}</span>
        </label>
      </div>

      <div className="card">
        <h2>{t('blockedNow')}</h2>
        {recentBlocked.length === 0 ? (
          <div className="empty">{t('noBlockedQueries')}</div>
        ) : (
          <div className="table-wrap">
            <table className="rtable">
              <tbody>
                {recentBlocked.map((r) => (
                  <tr key={r.domain}>
                    <td className="dom">{r.domain}</td>
                    <td className="n small muted">×{r.count}</td>
                    <td>
                      <span className={`tag ${r.verdict === 1 ? 'red' : ''}`}>
                        {r.verdict === 1 ? t('yourList') : t('adList')}
                      </span>
                    </td>
                    <td className="muted small">{r.who}</td>
                    <td style={{ textAlign: 'end' }}>
                      <button
                        type="button"
                        className="btn sm"
                        onClick={() => {
                          setRecentBlocked((prev) => prev.filter((x) => x.domain !== r.domain));
                          onToast(t('saved'));
                        }}
                      >
                        {r.verdict === 1 ? t('unblockRule') : t('allow')}
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      <div className="card">
        <h2>{t('siteBox')}</h2>
        <div className="row">
          <input
            value={siteInput}
            onChange={(e) => setSiteInput(e.target.value)}
            placeholder={t('sitePh')}
            autoCapitalize="off"
          />
          <button type="button" className="btn primary" onClick={addBlockRule}>
            {t('blockSite')}
          </button>
        </div>
      </div>

      <div className="card">
        <h2>{t('dnsProvider')}</h2>
        <select value={dnsIdx} onChange={(e) => setDnsIdx(Number(e.target.value))}>
          {DNS_PRESETS.map((p, i) => (
            <option key={p.name} value={i}>
              {p.name} {p.a ? `(${p.a})` : ''}
            </option>
          ))}
        </select>
        <div style={{ marginTop: 10 }}>
          <button type="button" className="btn primary" onClick={() => onToast(t('saved'))}>
            {t('save')}
          </button>
        </div>
      </div>

      <div className="card">
        <h2>{t('hostsTitle')}</h2>
        <textarea
          rows={6}
          value={hostsText}
          onChange={(e) => setHostsText(e.target.value)}
          placeholder="# 0.0.0.0 example.com"
          style={{ fontFamily: 'ui-monospace, Menlo, Consolas, monospace', fontSize: 13, direction: 'ltr' }}
        />
        <div className="row between wrap" style={{ marginTop: 8 }}>
          <span className="muted small">{new Blob([hostsText]).size} / 12288 B</span>
          <button type="button" className="btn primary" onClick={() => onToast(t('saved'))}>
            {t('save')}
          </button>
        </div>
      </div>
    </div>
  );
};
