import { useEffect, useMemo, useState } from 'react';
import useWebSocket from 'react-use-websocket';
import { ConnectionPanel } from './ConnectionPanel';
import { Graph } from './Graph';
import { TxRxConfigPanel } from './TxRxConfig';
import { USConfigPanel } from './UsConfig';
import type { DataFrame, Status, TxRxConfig, UsConfig, WulpusConfig } from './websocket-types';

function App() {

  const wsUrl = `${window.location.protocol === 'https:' ? 'wss' : 'ws'}://${window.location.host}/ws`;
  const { lastJsonMessage } = useWebSocket<Status | DataFrame>(wsUrl, {
    shouldReconnect: () => true,
  });

  const [status, setStatus] = useState<Status | null>(null);
  const [dataFrame, setDataFrame] = useState<DataFrame | null>(null);

  const bmodeBufferSize = 8; // keep a small ring buffer of recent frames for b-mode (8 rows)
  const [bmodeBuffer, setBmodeBuffer] = useState<number[][]>(() => []);

  // WulpusConfig state
  const [txRxConfigs, setTxRxConfigs] = useState<TxRxConfig[]>([{ config_id: 0, tx_channels: [0], rx_channels: [0], optimized_switching: true }]);
  const [usConfig, setUsConfig] = useState<UsConfig>({
    num_acqs: 400,
    dcdc_turnon: 100,
    meas_period: 321965,
    trans_freq: 2250000,
    pulse_freq: 2250000,
    num_pulses: 1,
    sampling_freq: 8000000,
    num_samples: 400,
    rx_gain: 3.5,
    num_txrx_configs: 1,
    tx_configs: [0],
    rx_configs: [1],
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

  return (
    <div className="min-h-screen bg-gray-50 text-gray-900">
      <main className="mx-auto max-w-7xl px-4 sm:px-6 lg:px-8 py-3 grid grid-cols-1 lg:grid-cols-3 gap-3">
        <section className="lg:col-span-1 space-y-3">
          <div className="bg-white rounded-lg shadow">
            <ConnectionPanel effectiveConfig={effectiveConfig} status={status} />
          </div>

          <div className="bg-white rounded-lg shadow">
            <USConfigPanel usConfig={usConfig} setUsConfig={setUsConfig} />
          </div>
        </section>

        <div className="col-span-2 space-y-3">
          <div className="bg-white rounded-lg shadow">
            <Graph dataFrame={dataFrame} bmodeBuffer={bmodeBuffer} usConfig={usConfig} />
          </div>

          <div className="bg-white rounded-lg shadow">
            <TxRxConfigPanel txRxConfigs={txRxConfigs} setTxRxConfigs={setTxRxConfigs} />
          </div>
        </div>
      </main>
    </div>
  )
}

export default App