

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