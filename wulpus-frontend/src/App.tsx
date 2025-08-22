import { useEffect, useMemo, useState } from 'react'
import useWebSocket from 'react-use-websocket';
import type { DataFrame, Status, UsConfig, TxRxConfig, WulpusConfig } from './websocket-types';
import Plot from 'react-plotly.js';
// import Plotly types if needed in future
import { getConnections, postConnect, postStart } from './api';

function App() {

  const wsUrl = `${window.location.protocol === 'https:' ? 'wss' : 'ws'}://${window.location.host}/ws`;
  const { lastJsonMessage, readyState } = useWebSocket<Status | DataFrame>(wsUrl, {
    shouldReconnect: () => true,
  });

  const [status, setStatus] = useState<Status | null>(null);

  const [dataFrame, setDataFrame] = useState<DataFrame | null>(null);
  const [connections, setConnections] = useState<string[]>([]);
  const [selectedPort, setSelectedPort] = useState<string>("");

  // WulpusConfig state
  const [txRxConfigs, setTxRxConfigs] = useState<TxRxConfig[]>([{ config_id: 0, tx_channels: [0], rx_channels: [0], optimized_switching: true }]);
  const [usConfig, setUsConfig] = useState<UsConfig>({
    num_acqs: 100,
    dcdc_turnon: 195300,
    meas_period: 321965,
    trans_freq: 2250000,
    pulse_freq: 2250000,
    num_pulses: 2,
    sampling_freq: 8000000,
    num_samples: 400,
    rx_gain: 21.5,
    num_txrx_configs: 1,
    tx_configs: [0],
    rx_configs: [0],
    start_hvmuxrx: 500,
    start_ppg: 500,
    turnon_adc: 5,
    start_pgainbias: 5,
    start_adcsampl: 503,
    restart_capt: 3000,
    capt_timeout: 3000,
  });

  const effectiveConfig: WulpusConfig = useMemo(() => ({
    tx_rx_config: txRxConfigs,
    us_config: {
      ...usConfig, num_txrx_configs: txRxConfigs.length,
      // ensure tx/rx bitmasks lists align in length
      tx_configs: txRxConfigs.map((c) => c.config_id),
      rx_configs: txRxConfigs.map((c) => c.config_id),
    },
  }), [txRxConfigs, usConfig]);

  useEffect(() => {
    if (lastJsonMessage) {
      if ('status' in lastJsonMessage) {
        setStatus(lastJsonMessage);
        // If backend provides config, sync UI (non-destructive)
        if (lastJsonMessage.us_config) {
          setUsConfig(lastJsonMessage.us_config);
        }
        if (lastJsonMessage.tx_rx_config && Array.isArray(lastJsonMessage.tx_rx_config)) {
          setTxRxConfigs(lastJsonMessage.tx_rx_config);
        }
      }
      else if (Array.isArray(lastJsonMessage)) {
        setDataFrame(lastJsonMessage);
      }
    }
  }, [lastJsonMessage, setStatus, setDataFrame]);

  useEffect(() => {
    // load available connections once
    getConnections().then(setConnections).catch(() => setConnections([]));
  }, []);

  function updateTxRx<K extends keyof TxRxConfig>(idx: number, field: K, value: TxRxConfig[K]) {
    setTxRxConfigs((prev) => prev.map((c, i) => i === idx ? { ...c, [field]: value } : c));
  }

  function addTxRx() {
    setTxRxConfigs((prev) => [...prev, { config_id: prev.length, tx_channels: [], rx_channels: [], optimized_switching: true }]);
  }

  function removeTxRx(idx: number) {
    setTxRxConfigs((prev) => prev.filter((_, i) => i !== idx).map((c, i) => ({ ...c, config_id: i })));
  }

  async function handleConnect() {
    if (!selectedPort) return;
    await postConnect(selectedPort);
  }

  async function handleStart() {
    await postStart(effectiveConfig);
  }

  return (
    <div className="min-h-screen bg-gray-50 text-gray-900">

      <main className="mx-auto max-w-7xl px-4 sm:px-6 lg:px-8 py-3 grid grid-cols-1 lg:grid-cols-3 gap-6">

        <section className="lg:col-span-1 space-y-4">
          <div className="bg-white rounded-lg shadow p-4 space-y-3">
            <h2 className="font-medium">Connection</h2>
            <div className="space-y-2">
              <select className="w-full border rounded px-2 py-1" value={selectedPort} onChange={(e) => setSelectedPort(e.target.value)}>
                <option value="">Select port</option>
                {connections.map((c) => (
                  <option key={c} value={c}>{c}</option>
                ))}
              </select>
              <div className='flex space-x-2'>
                <button onClick={handleConnect} className={`w-full bg-blue-600 hover:bg-blue-700 text-white rounded px-3 py-2 ${status?.status != 0 ? 'opacity-50' : 'cursor-pointer'}`}
                  disabled={status?.status != 0}>Connect</button>
                <button onClick={handleStart} className={`w-full bg-green-600 hover:bg-green-700 text-white rounded px-3 py-2 ${status?.status != 2 ? 'opacity-50' : 'cursor-pointer'}`}
                  disabled={status?.status != 2}>Start</button>
              </div>
            </div>
            <div className="text-xs text-gray-600">
              Status: {status ? StatusLabel(status.status) : 'N/A'} · BT: {status?.bluetooth ?? '—'} · Progress: {Math.round((status?.progress ?? 0) * 100)}%
            </div>
          </div>

          <div className="bg-white rounded-lg shadow p-4 space-y-3">
            <h2 className="font-medium">US Config</h2>
            <div className="grid grid-cols-2 gap-3">
              <NumberField label="Num acquisitions" value={usConfig.num_acqs} onChange={(v) => setUsConfig(s => ({ ...s, num_acqs: v }))} />
              <NumberField label="Num samples" value={usConfig.num_samples} onChange={(v) => setUsConfig(s => ({ ...s, num_samples: v }))} />
              <NumberField label="Meas period (us)" value={usConfig.meas_period} onChange={(v) => setUsConfig(s => ({ ...s, meas_period: v }))} />
              <NumberField label="DCDC turn on (us)" value={usConfig.dcdc_turnon} onChange={(v) => setUsConfig(s => ({ ...s, dcdc_turnon: v }))} />
              <NumberField label="Trans freq (Hz)" value={usConfig.trans_freq} onChange={(v) => setUsConfig(s => ({ ...s, trans_freq: v }))} />
              <NumberField label="Pulse freq (Hz)" value={usConfig.pulse_freq} onChange={(v) => setUsConfig(s => ({ ...s, pulse_freq: v }))} />
              <NumberField label="# pulses" value={usConfig.num_pulses} onChange={(v) => setUsConfig(s => ({ ...s, num_pulses: v }))} />
              <SelectField label="Sampling freq" value={usConfig.sampling_freq} onChange={(v) => setUsConfig(s => ({ ...s, sampling_freq: Number(v) as UsConfig['sampling_freq'] }))} options={[8000000, 4000000, 2000000, 1000000, 500000].map(v => ({ value: String(v), label: String(v / 1000000) + "MHz" }))} />
              <NumberField label="RX gain (dB)" step={0.1} value={usConfig.rx_gain} onChange={(v) => setUsConfig(s => ({ ...s, rx_gain: v }))} />
              <NumberField label="Start HV-MUX RX (us)" value={usConfig.start_hvmuxrx} onChange={(v) => setUsConfig(s => ({ ...s, start_hvmuxrx: v }))} />
              <NumberField label="Start PPG (us)" value={usConfig.start_ppg} onChange={(v) => setUsConfig(s => ({ ...s, start_ppg: v }))} />
              <NumberField label="Turn on ADC (us)" value={usConfig.turnon_adc} onChange={(v) => setUsConfig(s => ({ ...s, turnon_adc: v }))} />
              <NumberField label="Start PGA bias (us)" value={usConfig.start_pgainbias} onChange={(v) => setUsConfig(s => ({ ...s, start_pgainbias: v }))} />
              <NumberField label="Start ADC sample (us)" value={usConfig.start_adcsampl} onChange={(v) => setUsConfig(s => ({ ...s, start_adcsampl: v }))} />
              <NumberField label="Restart capture (us)" value={usConfig.restart_capt} onChange={(v) => setUsConfig(s => ({ ...s, restart_capt: v }))} />
              <NumberField label="Capture timeout (us)" value={usConfig.capt_timeout} onChange={(v) => setUsConfig(s => ({ ...s, capt_timeout: v }))} />
            </div>
          </div>
        </section>

        <section className="lg:col-span-2 space-y-4">
          <div className="bg-white rounded-lg shadow p-4">
            <h2 className="font-medium mb-3">TX/RX Configurations</h2>
            <div className="space-y-4">
              {txRxConfigs.map((cfg, idx) => (
                <div key={idx} className="border rounded p-3">
                  <div className="flex items-center justify-between mb-2">
                    <div className="font-medium">Config #{cfg.config_id}</div>
                    <button onClick={() => removeTxRx(idx)} className="text-red-600 text-sm">Remove</button>
                  </div>
                  <div className="grid grid-cols-3 gap-3">
                    <NumberField label="Config ID" value={cfg.config_id} onChange={(v) => updateTxRx(idx, 'config_id', v)} min={0} />
                    <MultiNumField label="TX channels (0-7)" values={cfg.tx_channels} onChange={(vals) => updateTxRx(idx, 'tx_channels', vals)} />
                    <MultiNumField label="RX channels (0-7)" values={cfg.rx_channels} onChange={(vals) => updateTxRx(idx, 'rx_channels', vals)} />
                    <div className="col-span-3 flex items-center gap-2">
                      <input id={`opt-${idx}`} type="checkbox" className="h-4 w-4" checked={cfg.optimized_switching} onChange={(e) => updateTxRx(idx, 'optimized_switching', e.target.checked)} />
                      <label htmlFor={`opt-${idx}`} className="text-sm">Optimized switching</label>
                    </div>
                  </div>
                </div>
              ))}
              <button onClick={addTxRx} className="bg-gray-100 hover:bg-gray-200 rounded px-3 py-2 text-sm">Add configuration</button>
            </div>
          </div>

          <div className="bg-white rounded-lg shadow p-4">
            <h2 className="font-medium mb-3">Live Signal</h2>
            <div className="h-[400px]">
              <Plot
                data={[
                  {
                    x: dataFrame?.map((_, index) => index) ?? [],
                    y: dataFrame?.map((value) => value) ?? [],
                    type: 'scatter',
                    mode: 'lines',
                    marker: { color: 'red' },
                  },
                ]}
                style={{ width: "100%", height: "100%" }}
                layout={{
                  autosize: true, uirevision: "fixed",
                  showlegend: false,
                  margin: { t: 10, r: 10, b: 30, l: 40 },
                }}
              />
            </div>
          </div>
        </section>
      </main>
    </div>
  )
}

export default App

// UI helpers
function NumberField({ label, value, onChange, step = 1, min }: { label: string; value: number; onChange: (v: number) => void; step?: number; min?: number }) {
  return (
    <label className="text-sm">
      <div className="mb-1 text-gray-600">{label}</div>
      <input type="number" className="w-full border rounded px-2 py-1" value={value} step={step} min={min}
        onChange={(e) => onChange(Number(e.target.value))} />
    </label>
  );
}

function SelectField({ label, value, onChange, options }: { label: string; value: string | number; onChange: (v: string) => void; options: { value: string; label: string }[] }) {
  return (
    <label className="text-sm">
      <div className="mb-1 text-gray-600">{label}</div>
      <select className="w-full border rounded px-2 py-1" value={String(value)} onChange={(e) => onChange(e.target.value)}>
        {options.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}
      </select>
    </label>
  );
}

function MultiNumField({ label, values, onChange }: { label: string; values: number[]; onChange: (vals: number[]) => void }) {
  const [text, setText] = useState(values.join(','));
  useEffect(() => { setText(values.join(',')); }, [values]);
  return (
    <label className="text-sm col-span-2">
      <div className="mb-1 text-gray-600">{label}</div>
      <input className="w-full border rounded px-2 py-1" value={text} onChange={(e) => {
        setText(e.target.value);
        const nums = e.target.value.split(',').map(s => s.trim()).filter(Boolean).map(Number).filter(n => !Number.isNaN(n));
        onChange(nums);
      }} placeholder="e.g., 0,1,2" />
    </label>
  );
}

function StatusLabel(s?: number) {
  switch (s) {
    case 0: return 'NOT_CONNECTED';
    case 1: return 'CONNECTING';
    case 2: return 'READY';
    case 3: return 'RUNNING';
    case 9: return 'ERROR';
    default: return String(s ?? '—');
  }
}
