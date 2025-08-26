// Simple API client for the FastAPI backend

export type ConnectResponse = { ok: string } | { [key: string]: string };

// Use Vite proxy in dev to avoid CORS; see vite.config.ts
const BASE_URL = "/api";

export async function getBTHConnections(): Promise<string[][]> {
    const res = await fetch(`${BASE_URL}/connections`);
    if (!res.ok) throw new Error(`GET /connections failed: ${res.status}`);
    const data = await res.json() as { [key: string]: string }[]
    // Backend might return list of strings or objects; normalize to strings
    if (Array.isArray(data)) {
        return data.map((item) => {
            // get key of object:
            const key = Object.keys(item)[0];
            return [key, item[key]];
        })
    }
    return [];
}

export async function postConnect(com_port: string): Promise<void> {
    const res = await fetch(`${BASE_URL}/connect`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ com_port }),
    });
    if (!res.ok) throw new Error(`POST /connect failed: ${res.status}`);
}

export async function postDisconnect(): Promise<void> {
    const res = await fetch(`${BASE_URL}/disconnect`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
    });
    if (!res.ok) throw new Error(`POST /disconnect failed: ${res.status}`);
}

export async function postStart(config: unknown): Promise<ConnectResponse> {
    const res = await fetch(`${BASE_URL}/start`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(config),
    });
    if (!res.ok) throw new Error(`POST /start failed: ${res.status}`);
    return res.json();
}

export async function postStop(): Promise<ConnectResponse> {
    const res = await fetch(`${BASE_URL}/stop`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
    });
    if (!res.ok) throw new Error(`POST /stop failed: ${res.status}`);
    return res.json();
}

export async function getLogs(): Promise<string[]> {
    const res = await fetch('/logs');
    if (!res.ok) throw new Error(await res.text());
    return res.json();
}

export function StatusLabel(s?: number) {
    switch (s) {
        case 0: return 'NOT_CONNECTED';
        case 1: return 'CONNECTING';
        case 2: return 'READY';
        case 3: return 'RUNNING';
        case 9: return 'ERROR';
        default: return String(s ?? '—');
    }
}