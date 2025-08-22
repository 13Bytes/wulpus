// Simple API client for the FastAPI backend

export type ConnectResponse = { ok: string } | { [key: string]: string };

// Use Vite proxy in dev to avoid CORS; see vite.config.ts
const BASE_URL = "/api";

export async function getConnections(): Promise<string[]> {
    const res = await fetch(`${BASE_URL}/connections`);
    if (!res.ok) throw new Error(`GET /connections failed: ${res.status}`);
    const data = await res.json();
    // Backend might return list of strings or objects; normalize to strings
    if (Array.isArray(data)) {
        return data.map((item) => {
            if (typeof item === "string") return item;
            // Try common fields from serial.tools.list_ports
            if (item && typeof item === "object") {
                const obj = item as Record<string, unknown>;
                const device = typeof obj.device === "string" ? obj.device : undefined;
                const name = typeof obj.name === "string" ? obj.name : undefined;
                const desc = typeof obj.description === "string" ? obj.description : undefined;
                return device || name || desc || JSON.stringify(item);
            }
            return String(item);
        });
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

export async function postStart(config: unknown): Promise<ConnectResponse> {
    const res = await fetch(`${BASE_URL}/start`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(config),
    });
    if (!res.ok) throw new Error(`POST /start failed: ${res.status}`);
    return res.json();
}
