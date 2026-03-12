const history = {};
const palette = ["#c2410c", "#0f766e", "#1d4ed8", "#be123c", "#ca8a04", "#0891b2"];

function appendHistory(metric) {
    if (!history[metric.host]) {
        history[metric.host] = { timestamps: [], cpu: [], memory: [], network: [] };
    }
    const store = history[metric.host];
    store.timestamps.push(metric.timestamp);
    store.cpu.push(metric.cpu_percent);
    store.memory.push(metric.memory_percent);
    store.network.push(metric.network_rx_kbps + metric.network_tx_kbps);
    if (store.timestamps.length > 20) {
        store.timestamps.shift();
        store.cpu.shift();
        store.memory.shift();
        store.network.shift();
    }
}

function renderCharts() {
    const hosts = Object.keys(history);
    const cpuTraces = [];
    const memTraces = [];
    const netTraces = [];

    hosts.forEach((host, index) => {
        const color = palette[index % palette.length];
        const traceBase = {
            x: history[host].timestamps,
            mode: "lines+markers",
            line: { color, width: 3 },
            marker: { size: 6 },
            name: host,
        };
        cpuTraces.push({ ...traceBase, y: history[host].cpu });
        memTraces.push({ ...traceBase, y: history[host].memory });
        netTraces.push({ ...traceBase, y: history[host].network });
    });

    const layout = (title, suffix) => ({
        title: { text: title, font: { family: "Georgia, serif", size: 20 } },
        paper_bgcolor: "rgba(0,0,0,0)",
        plot_bgcolor: "rgba(255,255,255,0.55)",
        margin: { l: 44, r: 18, t: 50, b: 44 },
        yaxis: { ticksuffix: suffix, gridcolor: "rgba(31,41,55,0.08)" },
        xaxis: { gridcolor: "rgba(31,41,55,0.05)" },
        legend: { orientation: "h" },
    });

    Plotly.react("cpuChart", cpuTraces, layout("CPU Utilization", "%"), { responsive: true });
    Plotly.react("memoryChart", memTraces, layout("Memory Pressure", "%"), { responsive: true });
    Plotly.react("networkChart", netTraces, layout("Network Throughput", " KB/s"), { responsive: true });
}

function renderHosts(metrics) {
    const hostGrid = document.getElementById("hostGrid");
    const cards = Object.values(metrics).sort((a, b) => a.host.localeCompare(b.host)).map((metric) => `
        <article class="host-card">
            <h3>${metric.host}</h3>
            <p><strong>CPU:</strong> ${metric.cpu_percent.toFixed(1)}%</p>
            <p><strong>Memory:</strong> ${metric.memory_used_mb}/${metric.memory_total_mb} MB (${metric.memory_percent.toFixed(1)}%)</p>
            <p><strong>Disk:</strong> ${metric.disk_used_mb}/${metric.disk_total_mb} MB (${metric.disk_percent.toFixed(1)}%)</p>
            <p><strong>Network:</strong> RX ${metric.network_rx_kbps.toFixed(1)} KB/s, TX ${metric.network_tx_kbps.toFixed(1)} KB/s</p>
            <p><strong>Last Update:</strong> ${metric.timestamp}</p>
        </article>
    `);
    hostGrid.innerHTML = cards.join("") || '<p class="subtle">No hosts connected yet.</p>';
}

function renderFeed(containerId, items, formatter) {
    const container = document.getElementById(containerId);
    container.innerHTML = items.map(formatter).join("") || '<div class="feed-item">No data yet.</div>';
}

function renderRules(rules) {
    const container = document.getElementById("ruleList");
    container.innerHTML = rules.map((rule) => `
        <div class="rule-item">${rule.command} <span class="subtle">(${rule.metric_key} threshold ${Number(rule.threshold).toFixed(1)})</span></div>
    `).join("") || '<p class="subtle">No active rules.</p>';
}

async function refreshState() {
    const response = await fetch("/api/state");
    const state = await response.json();
    const metrics = state.metrics || {};

    Object.values(metrics).forEach(appendHistory);
    renderCharts();
    renderHosts(metrics);
    renderRules(state.rules || []);
    renderFeed("alertFeed", (state.alerts || []).slice().reverse(), (item) => `
        <div class="feed-item">
            <strong>${item.display_metric}</strong> on ${item.host}<br>
            ${item.message}<br>
            <span class="subtle">${item.timestamp} | Threshold ${Number(item.threshold).toFixed(1)}</span>
        </div>
    `);
    renderFeed("chatFeed", (state.chat || []).slice().reverse(), (item) => `
        <div class="feed-item">
            <strong>${item.from}</strong><br>
            ${item.text}<br>
            <span class="subtle">${item.timestamp}</span>
        </div>
    `);

    document.getElementById("serverState").textContent = state.server.connected ? "Connected" : "Reconnecting";
    document.getElementById("hostCount").textContent = Object.keys(metrics).length;
    document.getElementById("alertCount").textContent = (state.alerts || []).length;
    document.getElementById("awsStatus").textContent = state.aws ? state.aws.message : "AWS upload idle";
}

async function postJson(url, body) {
    const response = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
    });
    return response.json();
}

document.getElementById("commandForm").addEventListener("submit", async (event) => {
    event.preventDefault();
    const text = document.getElementById("commandText").value.trim();
    if (!text) {
        return;
    }
    await postJson("/api/send-command", { text });
    document.getElementById("commandText").value = "";
    await refreshState();
});

document.getElementById("chatForm").addEventListener("submit", async (event) => {
    event.preventDefault();
    const from = document.getElementById("chatFrom").value.trim() || "dashboard-user";
    const text = document.getElementById("chatText").value.trim();
    if (!text) {
        return;
    }
    await postJson("/api/chat", { from, text });
    document.getElementById("chatText").value = "";
    await refreshState();
});

document.getElementById("uploadForm").addEventListener("submit", async (event) => {
    event.preventDefault();
    await postJson("/api/upload-latest-alert", {});
    await refreshState();
});

refreshState();
setInterval(refreshState, 2000);