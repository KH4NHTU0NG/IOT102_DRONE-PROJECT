const TOPIC_SENSORS = "drone/payload/sensors";
const TOPIC_FLIGHT  = "drone/control/flight";
const TOPIC_PAYLOAD = "drone/control/payload";

const logConsole = { value: '', scrollTop: 0, scrollHeight: 100 };
const hostInput = { value: '127.0.0.1' };
const portInput = { value: '9001' };
const btnConnect = { innerText: '', className: '' };

let client = null;
let isConnected = false;

function startConnection() {
    const host = hostInput.value.trim() || '127.0.0.1';
    const port = Number(portInput.value) || 9001;
    const clientId = 'web_client_' + Math.random().toString(16).slice(2, 10);

    addLog(`[SYSTEM] Bắt đầu kết nối MQTT Broker tại ws://${host}:${port}/mqtt...`);
}

function updateUIConnected(connected) {}

function onConnect() {}

function onFailure(message) {}

function onConnectionLost(responseObject) {}

function onMessageArrived(message) {}

function sendFlightCommand(cmdName) {}

function sendPayloadCommand(cmdName) {}

function addLog(msg) {
    const d = new Date();
    const timeStr = `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}:${d.getSeconds().toString().padStart(2, '0')}`;

    logConsole.value += `[${timeStr}] ${msg}\n`;
    logConsole.scrollTop = logConsole.scrollHeight;

    const lines = logConsole.value.split('\n');
    if (lines.length > 202) {
        logConsole.value = lines.slice(lines.length - 200).join('\n');
    }
}

function clearLogs() {
    logConsole.value = "";
}

startConnection();
console.log("Syntax OK:", logConsole.value);
