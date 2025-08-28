import type Plotly from 'plotly.js';
import { useCallback, useEffect, useRef, useState } from "react";
import Plot from 'react-plotly.js';
import { bandpassFIR, hilbertEnvelope, toggleFullscreen } from './helper';
import type { DataFrame, UsConfig } from './websocket-types';
import RangeSlider from 'react-range-slider-input';

export function Graph(props: { dataFrame: DataFrame | null, bmodeBuffer: number[][], usConfig: UsConfig }) {
    const { dataFrame, bmodeBuffer, usConfig } = props;
    const data = dataFrame?.data ?? []
    const sampling_freq = usConfig.sampling_freq;
    const plotContainerRef = useRef<HTMLDivElement | null>(null);
    const [showBMode, setShowBMode] = useState<boolean>(false);

    // fullscreen graph support
    const [isFullscreen, setIsFullscreen] = useState(false);

    // track fullscreen changes
    useEffect(() => {
        const onFsChange = () => setIsFullscreen(Boolean(document.fullscreenElement));
        document.addEventListener('fullscreenchange', onFsChange);
        return () => document.removeEventListener('fullscreenchange', onFsChange);
    }, []);

    // compute filter/envelope just-in-time before rendering
    const minLowCutHz = useCallback((sampling_freq: number) => sampling_freq / 2 * 0.1, []);
    const maxHighCutHz = useCallback((sampling_freq: number) => sampling_freq / 2 * 0.9, []);
    const [lowCutHz, setLowCutHz] = useState(minLowCutHz(sampling_freq));
    const [highCutHz, setHighCutHz] = useState(maxHighCutHz(sampling_freq));
    const filteredFrame = data ? bandpassFIR(data, sampling_freq, lowCutHz, highCutHz, 31) : [];
    const envelopeFrame = filteredFrame.length ? hilbertEnvelope(filteredFrame, 101) : [];

    useEffect(() => {
        setLowCutHz(minLowCutHz(sampling_freq));
        setHighCutHz(maxHighCutHz(sampling_freq));
    }, [sampling_freq, setHighCutHz, minLowCutHz, maxHighCutHz]);

    return (
        <div ref={plotContainerRef} className="bg-white p-4">
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
                                x: data ? data.map((_, i) => i) : [],
                                y: data ?? [],
                                type: 'scatter', mode: 'lines', name: 'Raw', line: { color: 'blue' },
                            },
                            {
                                x: data ? data.map((_, i) => i) : [],
                                y: filteredFrame.length ? filteredFrame : [],
                                type: 'scatter', mode: 'lines', name: 'Filter', line: { color: 'green' },
                                visible: 'legendonly',
                            },
                            {
                                x: data ? data.map((_, i) => i) : [],
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
                    onClick={() => toggleFullscreen(plotContainerRef)}
                    className={`border-gray-500 border-1 hover:bg-gray-200 rounded p-1 flex items-center justify-center`}
                >
                    {isFullscreen ? <span className="material-symbols-rounded">fullscreen_exit</span> : <span className="material-symbols-rounded">fullscreen</span>}
                </button>

                {!showBMode && (
                    <div className="flex grow items-center ml-4 gap-3">
                        <span>Filter: </span>
                        <div className='flex grow max-w-96 items-center justify-start gap-2'>
                            <span className='w-32'>{Math.round(lowCutHz / 1e4) / 100} MHz</span>
                            <div className='w-full'>
                                <RangeSlider
                                    min={minLowCutHz(sampling_freq)}
                                    max={maxHighCutHz(sampling_freq)}
                                    step={sampling_freq / 1e4}
                                    value={[lowCutHz, highCutHz]}
                                    onInput={i => {
                                        const [low, high] = i;
                                        setLowCutHz(low);
                                        setHighCutHz(high);
                                    }}
                                />
                            </div>
                            <span className='w-32'>{Math.round(highCutHz / 1e4) / 100} MHz</span>
                        </div>
                    </div>
                )}
            </div>
        </div>)
}