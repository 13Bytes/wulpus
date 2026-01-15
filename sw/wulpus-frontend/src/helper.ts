import { LOCAL_KEY } from "./constants";
import type { WulpusConfig } from "./websocket-types";


export const getDefaultConfig = (): WulpusConfig => {
  return {
    tx_rx_config: [{ config_id: 0, tx_channels: [0], rx_channels: [0], optimized_switching: true }],
    us_config: {
      num_acqs: 500,
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
    }
  }
}

export const getInitialConfig = () => {
  let defaultConfig: WulpusConfig = getDefaultConfig();
  const raw = localStorage.getItem(LOCAL_KEY);
  if (raw) {
    const parsed = JSON.parse(raw) as Partial<WulpusConfig>;
    if (parsed && typeof parsed === 'object' && parsed.tx_rx_config && parsed.us_config) {
      defaultConfig = parsed as WulpusConfig;
    }
  }
  return defaultConfig;
};


// Helper DSP utilities
function sinc(x: number) {
  if (x === 0) return 1;
  const pix = Math.PI * x;
  return Math.sin(pix) / pix;
}

function hammingWindow(n: number) {
  const ALPHA = 0.54;
  const BETA = 0.46;
  const out = new Array<number>(n);

  for (let i = 0; i < n; i++) {
    out[i] = ALPHA - BETA * Math.cos((2 * Math.PI * i) / (n - 1));
  }
  return out;
}

export function bandpassFIR(data: number[], fs: number, lowHz: number, highHz: number, nTaps = 101) {
  // design windowed-sinc bandpass (linear-phase FIR)
  if (nTaps % 2 === 0) nTaps += 1; // make odd
  const mid = (nTaps - 1) / 2;
  const low = lowHz / fs; // normalized (0..0.5)
  const high = highHz / fs;
  const win = hammingWindow(nTaps);
  const h: number[] = new Array(nTaps);
  for (let n = 0; n <= (nTaps - 1); n++) {
    const k = n - mid;
    // ideal bandpass = high * sinc(2*high*k) - low * sinc(2*low*k)
    h[n] = 2 * high * sinc(2 * high * k) - 2 * low * sinc(2 * low * k);
    h[n] *= win[n];
  }
  // apply forward-backward filtering to approximate filtfilt (zero-phase)
  const tmp = new Array<number>(data.length).fill(0);
  for (let i = 0; i < data.length; i++) {
    let acc = 0;
    for (let k = 0; k < nTaps; k++) {
      const idx = i - (nTaps - 1 - k);
      if (idx >= 0 && idx < data.length) acc += h[k] * data[idx];
    }
    tmp[i] = acc;
  }

  // reverse, filter again, then reverse to get zero-phase effect
  const revIn = tmp.slice().reverse();
  const tmp2 = new Array<number>(data.length).fill(0);
  for (let i = 0; i < revIn.length; i++) {
    let acc = 0;
    for (let k = 0; k < nTaps; k++) {
      const idx = i - (nTaps - 1 - k);
      if (idx >= 0 && idx < revIn.length) acc += h[k] * revIn[idx];
    }
    tmp2[i] = acc;
  }
  return tmp2.reverse();
}

export function hilbertEnvelope(data: number[], nTaps = 101) {
  // approximate analytic signal via FIR Hilbert transformer
  if (nTaps % 2 === 0) nTaps += 1; // ensure odd
  const mid = (nTaps - 1) / 2;
  const win = hammingWindow(nTaps);
  const h: number[] = new Array(nTaps).fill(0);
  for (let n = 0; n < nTaps; n++) {
    const k = n - mid;
    if (k === 0) {
      h[n] = 0;
    } else if (k % 2 === 0) {
      h[n] = 0;
    } else {
      h[n] = 2 / (Math.PI * k);
    }
    h[n] *= win[n];
  }
  // compute imaginary part (convolution)
  const imag = new Array<number>(data.length).fill(0);
  for (let i = 0; i < data.length; i++) {
    let acc = 0;
    for (let k = 0; k < nTaps; k++) {
      const idx = i - (nTaps - 1 - k);
      if (idx >= 0 && idx < data.length) acc += h[k] * data[idx];
    }
    imag[i] = acc;
  }
  // envelope sqrt(real^2 + imag^2)
  const out = new Array<number>(data.length);
  for (let i = 0; i < data.length; i++) {
    out[i] = Math.hypot(data[i], imag[i]);
  }
  return out;
}


export async function toggleFullscreen(plotContainerRef: React.RefObject<HTMLDivElement | null>) {
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

export function formatHexNodeName(node?: number): string | undefined {
  return node?.toString(16).padStart(4, '0').toUpperCase()
}

// --- Backend config-packing compatibility (matches sw/wulpus/wulpus_api.py) ---

const START_BYTE_CONF_PACK = 250;
const PACKAGE_LEN = 68;

const TX_RX_MAX_NUM_OF_CONFIGS = 16;
const RX_MAP = [0, 2, 4, 6, 8, 10, 12, 14] as const;
const TX_MAP = [1, 3, 5, 7, 9, 11, 13, 15] as const;

// Lookup table for us to ticks conversion (from sw/wulpus/wulpus_api_helper.py)
const usToTicks = {
  dcdc_turnon: 65535 / 2000000,
  meas_period: 65535 / 2000000,
  start_hvmuxrx: 8,
  start_ppg: 5,
  turnon_adc: 5,
  start_pgainbias: 5,
  start_adcsampl: 5,
  restart_capt: 5 / 16,
  capt_timeout: 5 / 4,
} as const;

const USS_CAPTURE_ACQ_RATES_VALUE = [8000000, 4000000, 2000000, 1000000, 500000] as const;
const USS_CAPT_OVER_SAMPLE_RATES_REG = [0, 1, 2, 3, 4] as const;

// PGA gain mapping (from sw/wulpus/wulpus_config_models.py)
const PGA_GAIN_VALUE = [
  -6.5, -5.5, -4.6, -4.1, -3.3, -2.3, -1.4, -0.8,
  0.1, 1.0, 1.9, 2.6, 3.5, 4.4, 5.2, 6.0, 6.8, 7.7,
  8.7, 9.0, 9.8, 10.7, 11.7, 12.2, 13, 13.9, 14.9,
  15.5, 16.3, 17.2, 18.2, 18.8, 19.6, 20.5, 21.5,
  22, 22.8, 23.6, 24.6, 25.0, 25.8, 26.7, 27.7,
  28.1, 28.9, 29.8, 30.8,
] as const;

const PGA_GAIN_REG = [
  17, 18, 19, 20, 21, 22, 23, 24,
  25, 26, 27, 28, 29, 30, 31, 32,
  33, 34, 35, 36, 37, 38, 39, 40,
  41, 42, 43, 44, 45, 46, 47, 48,
  49, 50, 51, 52, 53, 54, 55, 56,
  57, 58, 59, 60, 61, 62, 63,
] as const;

type ByteFormat = '<u1' | '<u2' | '<u4';

function asByte(value: number, format: ByteFormat): Uint8Array {
  const v = Math.trunc(value);
  const buffer = new ArrayBuffer(format === '<u1' ? 1 : format === '<u2' ? 2 : 4);
  const view = new DataView(buffer);

  if (format === '<u1') {
    view.setUint8(0, v & 0xFF);
  } else if (format === '<u2') {
    view.setUint16(0, v & 0xFFFF, true);
  } else {
    // Keep exact unsigned 32-bit behavior.
    view.setUint32(0, v >>> 0, true);
  }

  return new Uint8Array(buffer);
}

function fillPackageToMinLen(bytes: Uint8Array): Uint8Array {
  if (bytes.length >= PACKAGE_LEN) return bytes;
  const out = new Uint8Array(PACKAGE_LEN);
  out.set(bytes);
  return out;
}

function bitOrReduce(values: number[]): number {
  let out = 0;
  for (const v of values) out |= v;
  return out;
}

function buildTxRxConfigs(systemConfig: WulpusConfig): { txCfgs: number[]; rxCfgs: number[] } {
  const txCfgs: number[] = [];
  const rxCfgs: number[] = [];

  let i = 0;
  for (const cfg of systemConfig.tx_rx_config) {
    if (i >= TX_RX_MAX_NUM_OF_CONFIGS) break;

    const txChannelsRaw = cfg.tx_channels ?? [];
    const rxChannelsRaw = cfg.rx_channels ?? [];

    const txSet = new Set<number>(txChannelsRaw);
    const rxSet = new Set<number>(rxChannelsRaw);
    const txChannels = [...txSet];
    const rxChannels = [...rxSet];

    let txMask = 0;
    let rxMask = 0;

    if (txChannels.length === 0) {
      txMask = 0;
    } else {
      txMask = bitOrReduce(txChannels.map((ch) => (1 << TX_MAP[ch as 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7])));
    }

    if (rxChannels.length === 0) {
      rxMask = 0;
    } else {
      rxMask = bitOrReduce(rxChannels.map((ch) => (1 << RX_MAP[ch as 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7])));
    }

    if (cfg.optimized_switching) {
      const rxTxIntersect = txChannels.filter((ch) => rxSet.has(ch));
      const rxOnly = rxChannels.filter((ch) => !txSet.has(ch));

      if (rxTxIntersect.length > rxOnly.length) {
        const tempSwitchConfig = rxTxIntersect.length === 0
          ? 0
          : bitOrReduce(rxTxIntersect.map((ch) => (1 << RX_MAP[ch as 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7])));
        txMask |= tempSwitchConfig;
      } else if (rxOnly.length > 0) {
        const tempSwitchConfig = bitOrReduce(rxOnly.map((ch) => (1 << RX_MAP[ch as 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7])));
        txMask |= tempSwitchConfig;
      }
    }

    txCfgs.push(txMask & 0xFFFF);
    rxCfgs.push(rxMask & 0xFFFF);
    i += 1;
  }

  return { txCfgs, rxCfgs };
}

function indexOrThrow<T>(arr: readonly T[], value: T, label: string): number {
  const idx = arr.indexOf(value);
  if (idx < 0) throw new Error(`${label} value not found: ${String(value)}`);
  return idx;
}

export function genConfPackage(systemConfig: WulpusConfig): Uint8Array {
  const config = systemConfig.us_config;

  const dcdc_turnon_reg = asByte(config.dcdc_turnon * usToTicks.dcdc_turnon, '<u2');
  const meas_period_reg = asByte(config.meas_period * usToTicks.meas_period, '<u2');
  const trans_freq_reg = asByte(config.trans_freq, '<u4');
  const pulse_freq_reg = asByte(config.pulse_freq, '<u4');
  const num_pulses_reg = asByte(config.num_pulses, '<u1');

  const samplingIdx = indexOrThrow(USS_CAPTURE_ACQ_RATES_VALUE, config.sampling_freq, 'sampling_freq');
  const sampling_freq_reg = asByte(USS_CAPT_OVER_SAMPLE_RATES_REG[samplingIdx], '<u2');

  const num_samples_reg = asByte(config.num_samples * 2, '<u2');

  const gainIdx = indexOrThrow(PGA_GAIN_VALUE, config.rx_gain, 'rx_gain');
  const rx_gain_reg = asByte(PGA_GAIN_REG[gainIdx], '<u1');

  const num_txrx_configs_reg = asByte(config.num_txrx_configs, '<u1');

  const start_hvmuxrx_reg = asByte(config.start_hvmuxrx * usToTicks.start_hvmuxrx, '<u2');
  const start_ppg_reg = asByte(config.start_ppg * usToTicks.start_ppg, '<u2');
  const turnon_adc_reg = asByte(config.turnon_adc * usToTicks.turnon_adc, '<u2');
  const start_pgainbias_reg = asByte(config.start_pgainbias * usToTicks.start_pgainbias, '<u2');
  const start_adcsampl_reg = asByte(config.start_adcsampl * usToTicks.start_adcsampl, '<u2');
  const restart_capt_reg = asByte(config.restart_capt * usToTicks.restart_capt, '<u2');
  const capt_timeout_reg = asByte(config.capt_timeout * usToTicks.capt_timeout, '<u2');

  const bytes: number[] = [START_BYTE_CONF_PACK & 0xFF];
  const append = (u8: Uint8Array) => {
    for (const b of u8) bytes.push(b);
  };

  for (const param of [
    dcdc_turnon_reg,
    meas_period_reg,
    trans_freq_reg,
    pulse_freq_reg,
    num_pulses_reg,
    sampling_freq_reg,
    num_samples_reg,
    rx_gain_reg,
    num_txrx_configs_reg,
  ]) {
    append(param);
  }

  const { txCfgs, rxCfgs } = buildTxRxConfigs(systemConfig);
  for (let i = 0; i < config.num_txrx_configs; i++) {
    if (i >= txCfgs.length || i >= rxCfgs.length) {
      throw new Error(
        `TX/RX config mismatch: num_txrx_configs=${config.num_txrx_configs} but tx_rx_config has ${txCfgs.length} entries`,
      );
    }
    append(asByte(txCfgs[i], '<u2'));
    append(asByte(rxCfgs[i], '<u2'));
  }

  for (const advancedParam of [
    start_hvmuxrx_reg,
    start_ppg_reg,
    turnon_adc_reg,
    start_pgainbias_reg,
    start_adcsampl_reg,
    restart_capt_reg,
    capt_timeout_reg,
  ]) {
    append(advancedParam);
  }

  return fillPackageToMinLen(new Uint8Array(bytes));
}