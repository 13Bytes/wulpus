import { useEffect, useMemo, useRef, useState } from 'react'
import useWebSocket from 'react-use-websocket';
import type { DataFrame, Status, UsConfig, TxRxConfig, WulpusConfig } from './websocket-types';
import Plot from 'react-plotly.js';
import type Plotly from 'plotly.js';
import { getBTHConnections, postConnect, postDisconnect, postStart, postStop, StatusLabel } from './api';
import { MultiNumField } from './MultiNumField';
import { NumberField, SelectField } from './Fields';
import { bandpassFIR, hilbertEnvelope } from './helper';

function App() {

  const wsUrl = `${window.location.protocol === 'https:' ? 'wss' : 'ws'}://${window.location.host}/ws`;
  const { lastJsonMessage } = useWebSocket<Status | DataFrame>(wsUrl, {
    shouldReconnect: () => true,
  });

  const [connections, setConnections] = useState<string[][]>([]);
  const [selectedPort, setSelectedPort] = useState<string>("");

  const [status, setStatus] = useState<Status | null>(null);
  const [dataFrame, setDataFrame] = useState<DataFrame | null>(null);


  const bmodeBufferSize = 8; // keep a small ring buffer of recent frames for b-mode (8 rows)
  const [showBMode, setShowBMode] = useState<boolean>(false);
  const [bmodeBuffer, setBmodeBuffer] = useState<number[][]>(() => []);
  // fullscreen graph support
  const plotContainerRef = useRef<HTMLDivElement | null>(null);
  const [isFullscreen, setIsFullscreen] = useState(false);

  // WulpusConfig state
  const [txRxConfigs, setTxRxConfigs] = useState<TxRxConfig[]>([{ config_id: 0, tx_channels: [0], rx_channels: [0], optimized_switching: true }]);
  const [usConfig, setUsConfig] = useState<UsConfig>({
    num_acqs: 400,
    dcdc_turnon: 195300,
    meas_period: 321965,
    trans_freq: 2250000,
    pulse_freq: 2250000,
    num_pulses: 2,
    sampling_freq: 8000000,
    num_samples: 400,
    rx_gain: 3.5,
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
      }
      else if (Array.isArray(lastJsonMessage)) {
        setDataFrame(lastJsonMessage);
        // push into bmode buffer
        setBmodeBuffer((prev) => {
          const next = [...prev, lastJsonMessage.slice()];
          if (next.length > bmodeBufferSize) next.shift();
          return next;
        });
      }
    }
  }, [lastJsonMessage, setStatus, setDataFrame]);

  useEffect(() => {
    getBTHConnections()
      .then((list) => setConnections(list))
      .catch(() => setConnections([]))
  }, []);

  // track fullscreen changes
  useEffect(() => {
    const onFsChange = () => setIsFullscreen(Boolean(document.fullscreenElement));
    document.addEventListener('fullscreenchange', onFsChange);
    return () => document.removeEventListener('fullscreenchange', onFsChange);
  }, []);

  async function toggleFullscreen() {
    const el = plotContainerRef.current;
    if (!el) return;
    if (!document.fullscreenElement) {
      const elWithVendors = el as HTMLElement & {
        webkitRequestFullscreen?: () => Promise<void> | void;
        msRequestFullscreen?: () => Promise<void> | void;
      };
      if (elWithVendors.requestFullscreen) await elWithVendors.requestFullscreen();
      else if (elWithVendors.webkitRequestFullscreen) await elWithVendors.webkitRequestFullscreen();
      else if (elWithVendors.msRequestFullscreen) await elWithVendors.msRequestFullscreen();
    } else {
      const docWithVendors = document as Document & {
        webkitExitFullscreen?: () => Promise<void> | void;
        msExitFullscreen?: () => Promise<void> | void;
      };
      if (document.exitFullscreen) await document.exitFullscreen();
      else if (docWithVendors.webkitExitFullscreen) await docWithVendors.webkitExitFullscreen();
      else if (docWithVendors.msExitFullscreen) await docWithVendors.msExitFullscreen();
    }
  }

  async function refreshConnections() {
    try {
      const list = await getBTHConnections();
      setConnections(list);
      const justPosts: string[] = list.map(item => item[1]);
      // if previously selected port is gone, clear selection
      if (selectedPort && !justPosts.includes(selectedPort)) {
        setSelectedPort("");
      }
    } catch (e) {
      setConnections([]);
    }
  }

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
    console.log("Connecting to port:", selectedPort);
    if (!selectedPort) return;
    await postConnect(selectedPort);
  }

  async function handleStart() {
    await postStart(effectiveConfig);
  }

  // compute filter/envelope just-in-time before rendering
  const lowCutHz = usConfig.sampling_freq / 2 * 0.1;
  const highCutHz = usConfig.sampling_freq / 2 * 0.9;
  const filteredFrame = dataFrame ? bandpassFIR(dataFrame, usConfig.sampling_freq, lowCutHz, highCutHz, 31) : [];
  const envelopeFrame = filteredFrame.length ? hilbertEnvelope(filteredFrame, 101) : [];

  return (
    <div className="min-h-screen bg-gray-50 text-gray-900">

      <main className="mx-auto max-w-7xl px-4 sm:px-6 lg:px-8 py-3 grid grid-cols-1 lg:grid-cols-3 gap-6">

        <section className="lg:col-span-1 space-y-4">
          <div className="bg-white rounded-lg shadow p-4 space-y-3">
            <h2 className="font-medium">Connection</h2>
            <div className="flex flex-col space-y-2">
              <div className="flex flex-row flex-nowrap items-center space-x-2">
                <select className="border rounded px-2 py-1 w-52"
                  disabled={(status?.status ?? 0) !== 0}
                  value={selectedPort}
                  onChange={(e) => setSelectedPort(e.target.value)}>
                  <option value="">Select port</option>
                  {connections.map((c) => (
                    <option key={c[0]} value={c[0]}>{c[1]}</option>
                  ))}
                </select>
                <button onClick={refreshConnections} title="Refresh" className="p-2 bg-gray-100 hover:bg-gray-200 rounded">
                  <svg className="h-5 w-5 text-gray-600" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <path d="M21 12a9 9 0 11-3.95-7.07" />
                    <path d="M21 3v6h-6" />
                  </svg>
                </button>
              </div>
              <div className='flex space-x-2'>
                {status?.status === 0 && (
                  <button
                    onClick={handleConnect}
                    className={`w-full bg-blue-600 hover:bg-blue-700 text-white rounded px-3 py-2 `}
                  >
                    Connect
                  </button>
                )}
                {status?.status !== 0 && (
                  <button
                    onClick={postDisconnect}
                    className={`w-full bg-yellow-600 hover:bg-yellow-700 text-white rounded px-3 py-2 ${status?.status === 1 ? 'opacity-50' : ''}`}
                  >
                    Disconnect
                  </button>
                )}

                {status?.status !== 3 && (
                  <button
                  onClick={handleStart}
                    className={`w-full bg-green-600 hover:bg-green-700 text-white rounded px-3 py-2 ${status?.status != 2 ? 'opacity-50' : ''}`}
                    disabled={status?.status != 2}
                  >
                    Start
                  </button>
                )}
                {status?.status === 3 && (
                  <button
                    onClick={postStop}
                    className={`w-full bg-red-600 hover:bg-red-700 text-white rounded px-3 py-2`}
                  >
                    Stop
                  </button>
                )}
              </div>
            </div>
            <div className="text-xs text-gray-600">
              Status: {status ? StatusLabel(status.status) : 'No Server/Backend'} · BT: {status?.bluetooth ?? '—'} · Progress: {Math.round((status?.progress ?? 0) * 100)}%
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
              <NumberField label="# Pulses" value={usConfig.num_pulses} onChange={(v) => setUsConfig(s => ({ ...s, num_pulses: v }))} />
              <SelectField label="Sampling freq"
                value={usConfig.sampling_freq}
                onChange={(v) => setUsConfig(s => ({ ...s, sampling_freq: Number(v) as UsConfig['sampling_freq'] }))}
                options={[8000000, 4000000, 2000000, 1000000, 500000].map(v => ({ value: String(v), label: String(v / 1000000) + "MHz" }))}
              />
              <SelectField label="RX gain (dB)"
                value={usConfig.rx_gain}
                onChange={(v) => setUsConfig(s => ({ ...s, rx_gain: parseFloat(v) }))}
                options={[-6.5, -5.5, -4.6, -4.1, -3.3, -2.3, -1.4, -0.8,
                  0.1, 1.0, 1.9, 2.6, 3.5, 4.4, 5.2, 6.0, 6.8, 7.7,
                  8.7, 9.0, 9.8, 10.7, 11.7, 12.2, 13, 13.9, 14.9,
                  15.5, 16.3, 17.2, 18.2, 18.8, 19.6, 20.5, 21.5,
                  22, 22.8, 23.6, 24.6, 25.0, 25.8, 26.7, 27.7,
                  28.1, 28.9, 29.8, 30.8].map(v => ({ value: String(v), label: String(v) }))}
              />
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

        <div className="col-span-2 space-y-3">
          <div ref={plotContainerRef} className="bg-white rounded-lg p-4 shadow">

            <h2 className="font-medium mb-3">Live Signal</h2>
            <div className="h-[400px]">
              {showBMode ? (
                <Plot
                  data={[{
                    z: bmodeBuffer.length ? bmodeBuffer : [[]],
                    type: 'heatmap',
                    colorscale: 'Viridis',
                    reversescale: true,
                  }] as unknown as Plotly.Data[]}
                  useResizeHandler
                  style={{ width: "100%", height: "100%" }}
                  layout={{ autosize: true, margin: { t: 10, r: 10, b: 30, l: 40 } }}
                />
              ) : (
                <Plot
                  data={([
                    {
                        x: dataFrame ? dataFrame.map((_, i) => i) : [],
                        y: dataFrame ?? [],
                        type: 'scatter', mode: 'lines', name: 'Raw', line: { color: 'blue' },
                      },
                      {
                        x: dataFrame ? dataFrame.map((_, i) => i) : [],
                        y: filteredFrame.length ? filteredFrame : [],
                        type: 'scatter', mode: 'lines', name: 'Filter', line: { color: 'green' },
                        visible: 'legendonly',
                      },
                      {
                        x: dataFrame ? dataFrame.map((_, i) => i) : [],
                        y: envelopeFrame.length ? envelopeFrame : [],
                        type: 'scatter', mode: 'lines', name: 'Envelope', line: { color: 'red' },
                        visible: 'legendonly',
                      },
                    ]) as unknown as Plotly.Data[]}
                    useResizeHandler
                    style={{ width: "100%", height: "100%" }}
                    layout={{
                      autosize: true, uirevision: "fixed",
                      showlegend: true,
                      legend: { orientation: 'h', },
                      margin: { t: 10, r: 10, b: 30, l: 40 },
                    }}
                  />
              )}
            </div>
            <div className="flex gap-2 mt-2">
              <button
                onClick={() => setShowBMode(o => !o)}
                className={`bg-gray-500 hover:bg-gray-600 text-white rounded p-1`}
              >
                {showBMode ? 'Disable' : 'Enable'} B Mode</button>
              <button
                onClick={toggleFullscreen}
                className={`border-gray-500 border-1 hover:bg-gray-200 rounded p-1 flex items-center justify-center`}
              >
                {isFullscreen ? <span className="material-symbols-rounded">fullscreen_exit</span> : <span className="material-symbols-rounded">fullscreen</span>}
              </button>
            </div>
          </div>

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
                    <MultiNumField label="TX channels (0-7)"
                      values={cfg.tx_channels}
                      onChange={(vals) => updateTxRx(idx, 'tx_channels', vals)}
                      showChannelBoxes={true}
                      color="bg-green-500" />
                    <MultiNumField label="RX channels (0-7)"
                      values={cfg.rx_channels}
                      onChange={(vals) => updateTxRx(idx, 'rx_channels', vals)}
                      showChannelBoxes={true}
                      color="bg-blue-500" />
                    <div className="col-span-3 flex items-center gap-2">
                      <input
                        id={`opt-${idx}`}
                        type="checkbox"
                        className="h-4 w-4"
                        checked={cfg.optimized_switching}
                        onChange={(e) => updateTxRx(idx, 'optimized_switching', e.target.checked)}
                      />
                      <label
                        htmlFor={`opt-${idx}`}
                        className="text-sm">Optimized switching
                      </label>
                    </div>
                  </div>
                </div>
              ))}
              {txRxConfigs.length < 8 && (
                <button onClick={addTxRx} className="bg-gray-100 hover:bg-gray-200 rounded px-3 py-2 text-sm">Add configuration</button>
              )}
            </div>
          </div>
        </div>


      </main>
    </div>
  )
}

export default App