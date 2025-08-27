import { useEffect, useState } from "react";
import { deactivateMock, getBTHConnections, postConnect, postDisconnect, postStart, postStop, StatusLabel } from "./api";
import type { Status, WulpusConfig } from "./websocket-types";

export function ConnectionPanel(props: { effectiveConfig: WulpusConfig, status: Status | null }) {
    const { effectiveConfig, status } = props;
    const [connections, setConnections] = useState<string[][]>([]);
    const [selectedPort, setSelectedPort] = useState<string>("");

    const isMock = status?.mock ?? false;

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


    useEffect(() => {
        getBTHConnections()
            .then((list) => setConnections(list))
            .catch(() => setConnections([]))
    }, []);

    async function handleConnect() {
        console.log("Connecting to port:", selectedPort);
        if (!selectedPort) return;
        await postConnect(selectedPort);
    }

    async function handleStart() {
        await postStart(effectiveConfig);
    }

    return (
        <div className="p-4 space-y-3">
            <div className="flex gap-2 flex-row items-center">
                <h2 className="font-medium grow">Connection {isMock ? ' (Simulation)' : ''}</h2>
                {isMock &&
                    <>
                        <button
                            onClick={deactivateMock}
                            className="font-medium text-red-500 border-1 px-2 border-red-500 hover:bg-gray-50 rounded"
                        >
                            Stop Simulation
                        </button>
                    </>
                }
            </div>
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
    )
}