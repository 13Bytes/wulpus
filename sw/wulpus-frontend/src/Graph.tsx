import type Plotly from 'plotly.js';
import { useEffect, useState } from "react";
import Plot from 'react-plotly.js';
import { CHANNEL_SIZE } from './App';
import { bandpassFIR, formatHexNodeName, hilbertEnvelope } from './helper';
import type { DataFrame, UsConfig } from './websocket-types';

export function Graph(props: { dataFrame: DataFrame | undefined, usConfig: UsConfig, showBMode: boolean, lowCutHz: number, highCutHz: number }) {
    const { dataFrame, usConfig, showBMode, lowCutHz, highCutHz } = props;
    const data = dataFrame?.measurement.data ?? [];
    const wavelet_transform = dataFrame?.wavelet ?? [];
    const peaks = dataFrame?.peaks ?? [];
    const sampling_freq = usConfig.sampling_freq;
    const UPSAMPLING_FACTOR = 10

    const [bmodeBuffer, setBmodeBuffer] = useState<number[][]>(Array.from({ length: CHANNEL_SIZE }, () => []));
    const [peaksPerChannel, setPeaksPerChannel] = useState<number[][]>(Array.from({ length: CHANNEL_SIZE }, () => []));
    const [lastWulpusId, setLastWulpusId] = useState<number>();

    useEffect(() => {
        if (!dataFrame) {
            return;
        }
        if (dataFrame.wulpus_id != lastWulpusId) {
            // new wulpus, reset buffers
            setLastWulpusId(dataFrame.wulpus_id);
            setBmodeBuffer(Array.from({ length: CHANNEL_SIZE }, () => []));
            setPeaksPerChannel(Array.from({ length: CHANNEL_SIZE }, () => []));
        }

        const rx_channel = dataFrame.measurement.rx;
        if (Array.isArray(dataFrame.peaks)) {
            setPeaksPerChannel(prev => {
                const next = [...prev];
                for (const channel of rx_channel) {
                    if (channel >= CHANNEL_SIZE) break;
                    next[channel] = dataFrame.peaks.slice();
                }
                return next;
            });
        }
        const new_data = dataFrame.measurement.data.slice();
        setBmodeBuffer(prev => {
            const next = [...prev];
            for (const channel of rx_channel) {
                if (channel >= CHANNEL_SIZE) break;
                next[channel] = new_data;
            }
            return next;
        });
    }, [dataFrame, setLastWulpusId, lastWulpusId])


    // compute filter/envelope just-in-time before rendering
    const filteredFrame = data ? bandpassFIR(data, sampling_freq, lowCutHz, highCutHz, 31) : [];
    const envelopeFrame = filteredFrame.length ? hilbertEnvelope(filteredFrame, 101) : [];

    // Vertical line shapes for the time-domain (non B-mode) plot spanning full height
    const signalPeakShapes: Partial<Plotly.Shape>[] = peaks.map(p => ({
        type: 'line', x0: p, x1: p, xref: 'x', yref: 'paper', y0: 0, y1: 1,
        line: { color: 'rgba(255,140,0,0.35)', width: 2, dash: 'dot' }, layer: 'below'
    }));

    const spacerShape: Partial<Plotly.Shape> = {
        type: 'rect', x0: 0, x1: dataFrame?.spacer_region[1] ?? 0, xref: 'x', yref: 'paper', y0: 0, y1: 1,
        line: { color: 'rgba(100,120,135,0.35)' }, fillcolor: 'rgba(100,120,135,0.35)', layer: 'below'
    }

    const bmodePeakShapes: Partial<Plotly.Shape>[] = [];
    if (peaksPerChannel && peaksPerChannel.length) {
        for (let ch = 0; ch < peaksPerChannel.length; ch++) {
            const chPeaks = peaksPerChannel[ch];
            if (!chPeaks || !chPeaks.length) continue;
            if (ch < 0 || ch >= bmodeBuffer.length) continue;
            for (const p of chPeaks) {
                bmodePeakShapes.push({
                    type: 'line',
                    x0: p, x1: p,
                    xref: 'x', yref: 'y',
                    y0: ch - 0.5, y1: ch + 0.5,
                    line: { color: 'rgba(255,140,0,0.55)', width: 2, dash: 'dot' },
                    layer: 'above'
                });
            }
        }
    }


    return (
        <>
            {showBMode ? (
                <Plot
                    data={[{
                        z: bmodeBuffer.length ? bmodeBuffer : [[]],
                        type: 'heatmap',
                        colorscale: 'Viridis',
                        reversescale: true,
                    }] as Plotly.Data[]}
                    useResizeHandler
                    style={{ width: "100%", height: "100%" }}
                    layout={{
                        autosize: true,
                        margin: { t: 10, r: 10, b: 30, l: 40 },
                        shapes: [...bmodePeakShapes, spacerShape],
                        yaxis: { autorange: true, title: { text: 'Channel' } },
                    }}
                />
            ) : (
                <Plot
                    data={([
                        {
                            x: data ? data.map((_, i) => i) : [],
                            y: data ?? [],
                            type: 'scatter', mode: 'lines', name: 'Raw', line: { color: 'blue' },
                        },
                        {
                            x: data ? data.map((_, i) => i) : [],
                            y: envelopeFrame.length ? envelopeFrame : [],
                            type: 'scatter', mode: 'lines', name: 'Filtered Envelope', line: { color: 'fuchsia' },
                            visible: 'legendonly',
                        },
                        {
                            x: wavelet_transform ? wavelet_transform.map((_, i) => i / UPSAMPLING_FACTOR) : [],
                            y: wavelet_transform ?? [],
                            type: 'scatter', mode: 'lines', name: 'Wavelet Envelope', line: { color: 'red' },
                            visible: 'legendonly',
                        }
                    ]) as Plotly.Data[]}
                    useResizeHandler
                    style={{ width: "100%", height: "100%" }}
                    layout={{
                        autosize: true,
                        uirevision: "fixed",
                        showlegend: true,
                        title: {
                            text: `Node ${formatHexNodeName(dataFrame?.mesh_origin)} (${dataFrame?.measurement.rx && dataFrame.measurement.rx.length > 0 ? `Rx: ${dataFrame.measurement.rx.join(', ')})` : 'No Signal'}`, y: 0.95, x: 0.5
                        },
                        legend: { orientation: 'h' },
                        margin: { t: 10, r: 10, b: 30, l: 40 },
                        yaxis: { range: [-2000, 2000] },
                        shapes: [...signalPeakShapes, spacerShape],
                    }}
                />
            )}
        </>
    )
}