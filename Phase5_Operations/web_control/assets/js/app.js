/* Constants */
    const TOPIC_SENSORS = "iot102_drone/payload/sensors";
    const TOPIC_FLIGHT  = "iot102_drone/control/flight";
    const TOPIC_PAYLOAD = "iot102_drone/control/payload";
    const TOPIC_MOTORS  = "iot102_drone/telemetry/motors";   // [NEW]
    const TOPIC_WEATHER = "iot102_drone/control/weather";    // [NEW]
    const TOPIC_HEARTBEAT = "iot102_drone/control/heartbeat"; // [NEW] Watchdog Ping
    const TOPIC_MISSION   = "iot102_drone/control/mission";   // [NEW] Waypoint Mission
    const TOPIC_SIM       = "iot102_drone/control/sim_param"; // [NEW] SITL Params
    const TOPIC_ATTITUDE  = "iot102_drone/telemetry/attitude";
    const TOPIC_GPS       = "iot102_drone/telemetry/gps";
    const TOPIC_STATUS    = "iot102_drone/telemetry/status";  // [FIX] Phản hồi trạng thái bay
    const CHART_MAX_PTS = 60;

    /* Cached DOM refs */
    const logConsole  = document.getElementById('log_console');
    const hostInput   = document.getElementById('mqtt_host');
    const portInput   = document.getElementById('mqtt_port');
    const btnConnect  = document.getElementById('btn_connect');
    const btnConnect2 = document.getElementById('btn_connect_2');
    const statusBadge = document.getElementById('status_badge');
    const statusText  = document.getElementById('status_text');
    const tempEl      = document.getElementById('temp_value');
    const humEl       = document.getElementById('hum_value');
    const co2El       = document.getElementById('co2_value');
    const alertEl     = document.getElementById('alert_value');
    const rssiEl      = document.getElementById('rssi_value');
    const sonarEl     = document.getElementById('sonar_value');

    /* State */
    let client           = null;
    let isConnected      = false;
    let userDisconnected = false;
    let reconnectAttempts = 0;
    let reconnectTimer   = null;
    let logLineCount     = 0;

    /* Chart Setup */
    function makeChart(canvasId, color, yLabel, unit, suggestedMin, suggestedMax) {
        const ctx = document.getElementById(canvasId).getContext('2d');
        return new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    data: [],
                    borderColor: color,
                    backgroundColor: color + '18',
                    borderWidth: 1.5,
                    pointRadius: 0,
                    pointHoverRadius: 3,
                    fill: true,
                    tension: 0.3
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: { duration: 200 },
                interaction: { mode: 'index', intersect: false },
                plugins: {
                    legend: { display: false },
                    tooltip: {
                        callbacks: {
                            label: ctx => `${ctx.parsed.y} ${unit}`
                        },
                        backgroundColor: '#1f2937',
                        titleFont: { family: 'JetBrains Mono', size: 11 },
                        bodyFont:  { family: 'JetBrains Mono', size: 11 },
                        padding: 8,
                        cornerRadius: 4
                    }
                },
                scales: {
                    x: {
                        ticks: {
                            font: { family: 'JetBrains Mono', size: 9 },
                            color: '#9ca3af',
                            maxTicksLimit: 6,
                            maxRotation: 0
                        },
                        grid: { color: '#1e2d3d' }
                    },
                    y: {
                        suggestedMin: suggestedMin,
                        suggestedMax: suggestedMax,
                        ticks: {
                            font: { family: 'JetBrains Mono', size: 9 },
                            color: '#4a6278',
                            maxTicksLimit: 4
                        },
                        grid: { color: '#1e2d3d' }
                    }
                }
            }
        });
    }

    const charts = {
        temp: makeChart('chart_temp', '#fb923c', 'Temperature', '°C',  0,  50),
        hum:  makeChart('chart_hum',  '#38bdf8', 'Humidity',    '%',   0, 100),
        co2:  makeChart('chart_co2',  '#fbbf24', 'CO2',         'ADC', 0, 4095),
        rssi: makeChart('chart_rssi', '#a78bfa', 'RSSI',        'dBm', -90, -20)
    };

    function pushChart(chartKey, value, latestId, noDataId, unit) {
        if (value === null || value === undefined || isNaN(+value)) return;
        const chart = charts[chartKey];
        const now = new Date();
        const ts = `${String(now.getHours()).padStart(2,'0')}:${String(now.getMinutes()).padStart(2,'0')}:${String(now.getSeconds()).padStart(2,'0')}`;
        chart.data.labels.push(ts);
        chart.data.datasets[0].data.push(+value);
        if (chart.data.labels.length > CHART_MAX_PTS) {
            chart.data.labels.shift();
            chart.data.datasets[0].data.shift();
        }
        chart.update('none');
        document.getElementById(latestId).textContent = (+value).toFixed(1) + ' ' + unit;
        const noData = document.getElementById(noDataId);
        if (noData) noData.style.display = 'none';
    }

    /* Connection */
    function startConnection() {
        const host = hostInput.value.trim() || 'broker.hivemq.com';
        const isHttps = window.location.protocol === 'https:';
        let port = Number(portInput.value);
        if (!port) port = isHttps ? 8884 : 8000;
        const useSSL = isHttps || port === 8084 || port === 443 || port === 8884;
        const clientId = 'web_' + Math.random().toString(16).slice(2, 10);

        addLog(`[CONN] Đang kết nối ${useSSL ? 'wss' : 'ws'}://${host}:${port}/mqtt ...`);
        try {
            const MQTTClient = (typeof Paho.MQTT !== "undefined") ? Paho.MQTT.Client : Paho.Client;
            client = new MQTTClient(host, port, "/mqtt", clientId);
            client.onConnectionLost = onConnectionLost;
            client.onMessageArrived = onMessageArrived;
            client.connect({
                onSuccess: onConnect,
                onFailure: onFailure,
                keepAliveInterval: 60,
                useSSL: useSSL,
                mqttVersion: 4,
                reconnect: false
            });
        } catch (e) {
            addLog("[CONN] Lỗi khởi tạo client: " + (e.message || e));
            updateUIConnected(false);
        }
    }

    function toggleConnection() {
        if (client && isConnected) {
            userDisconnected = true;
            addLog("[CONN] Ngắt kết nối chủ động.");
            try { client.disconnect(); } catch (e) {}
            client = null;
            updateUIConnected(false);
        } else {
            userDisconnected = false;
            reconnectAttempts = 0;
            startConnection();
        }
    }

    function updateUIConnected(connected) {
        isConnected = connected;
        if (connected) {
            statusBadge.className = "status ok";
            statusText.textContent = "Đã kết nối";
            btnConnect.textContent  = "Ngắt kết nối";
            btnConnect2.textContent = "Ngắt kết nối";
            btnConnect.className  = "btn btn-connect-off";
            btnConnect2.className = "btn btn-connect-off";
        } else {
            statusBadge.className = "status err";
            statusText.textContent = "Chưa kết nối";
            btnConnect.textContent  = "Kết nối";
            btnConnect2.textContent = "Kết nối";
            btnConnect.className  = "btn btn-connect-on";
            btnConnect2.className = "btn btn-connect-on";
        }
        document.querySelectorAll('.card .btn:not(#btn_connect):not(#btn_connect_2)').forEach(btn => {
            btn.disabled = !connected;
        });
        const slider = document.getElementById('servo_slider');
        if (slider) slider.disabled = !connected;
    }

    /* MQTT Callbacks */
    function onConnect() {
        updateUIConnected(true);
        reconnectAttempts = 0;
        if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
        addLog("[MQTT]  Kết nối thành công");
        client.subscribe(TOPIC_SENSORS);
        client.subscribe(TOPIC_FLIGHT);
        client.subscribe(TOPIC_PAYLOAD);
        client.subscribe(TOPIC_MOTORS);  // [NEW]
        client.subscribe(TOPIC_ATTITUDE);// [NEW]
        client.subscribe(TOPIC_GPS);     // [NEW]
        client.subscribe(TOPIC_STATUS);  // [FIX] Flight status feedback
        addLog("[MQTT] Subscribe: sensors, flight, payload, motors, attitude, gps");

        // [NEW] Khởi động Failsafe Heartbeat (mỗi 3 giây)
        if (heartbeatTimer) clearInterval(heartbeatTimer);
        heartbeatTimer = setInterval(sendHeartbeat, 3000);
    }

    function onFailure(message) {
        updateUIConnected(false);
        if (heartbeatTimer) clearInterval(heartbeatTimer); // [NEW] Dừng heartbeat khi mất kết nối
        const err = message.errorMessage || "Network Error";
        addLog("[MQTT]  Kết nối thất bại: " + err);
        if (err.includes("Network Error"))
            addLog("[HINT] Đảm bảo Docker đang chạy và Mosquitto đã start.");
    }

    function onConnectionLost(responseObject) {
        updateUIConnected(false);
        if (responseObject.errorCode !== 0)
            addLog("[MQTT] Mất kết nối: " + (responseObject.errorMessage || "unknown"));
        if (!userDisconnected) {
            reconnectAttempts++;
            const delay = Math.min(1000 * Math.pow(2, reconnectAttempts), 30000);
            addLog(`[CONN] Thử lại sau ${delay / 1000}s (lần #${reconnectAttempts})`);
            if (reconnectTimer) clearTimeout(reconnectTimer);
            reconnectTimer = setTimeout(() => startConnection(), delay);
        }
    }

    /* Message Handler */
    function onMessageArrived(message) {
        const topic   = message.destinationName;
        const payload = message.payloadString;

        if (topic !== TOPIC_SENSORS) {
            // [NEW] Xử lý dữ liệu động cơ và 3D Attitude
            if (topic === TOPIC_MOTORS) {
                try {
                    const m = JSON.parse(payload);
                    updateMotorBars(m);
                    if (typeof updatePropellerSpeeds === 'function') updatePropellerSpeeds(m);
                } catch(e) {}
            
            } else if (topic === TOPIC_GPS) {
                try {
                    const g = JSON.parse(payload);
                    document.getElementById('chart_gps_lat_latest').textContent = g.lat.toFixed(7);
                    document.getElementById('chart_gps_lon_latest').textContent = g.lon.toFixed(7);
                    document.getElementById('chart_gps_alt_latest').textContent = (g.relative_alt ?? g.alt).toFixed(1) + ' m';

                    // Heading: GLOBAL_POSITION_INT.hdg la centi-degrees (0-35999) => chia 100 ra degrees
                    const headingDeg = (g.hdg != null && g.hdg <= 36000)
                        ? g.hdg / 100
                        : (g.heading ?? null);

                    updateMap(g.lat, g.lon, headingDeg);

                    // ── QGC Telemetry Panel Update ──────────────────────────
                    const setQt = (id, val, decimals = 1, warnLow, warnHigh) => {
                        const el = document.getElementById(id);
                        if (!el) return;
                        const v = (val != null && !isNaN(+val)) ? (+val).toFixed(decimals) : '--';
                        el.textContent = v;
                        // Color logic for GPS quality metrics
                        if (warnLow !== undefined && warnHigh !== undefined && v !== '--') {
                            const n = +v;
                            el.classList.toggle('val-red',   n > warnHigh);
                            el.classList.toggle('val-amber', n > warnLow && n <= warnHigh);
                        }
                    };

                    // Altitude (relative if available, else absolute)
                    setQt('qt_alt',       g.relative_alt ?? g.alt, 1);

                    // Ground speed: vx²+vy² from velocity vector, or direct field
                    const spd = (g.vx != null && g.vy != null)
                        ? Math.sqrt(g.vx * g.vx + g.vy * g.vy).toFixed(1)
                        : (g.ground_speed ?? g.speed ?? null);
                    setQt('qt_speed', spd, 1);

                    // Heading: hdg in centi-degrees → degrees, or direct heading field
                    const hdg = (g.hdg != null) ? (g.hdg / 100).toFixed(0) : (g.heading ?? null);
                    setQt('qt_heading', hdg, 0);

                    // Home distance (backend must publish, fallback to '--')
                    setQt('qt_home_dist', g.home_dist ?? null, 1);

                    // GPS Fix Type (0=No Fix,1=Dead,2=2D,3=3D,4=DGPS,5=RTK Float,6=RTK Fixed)
                    const fixNames = { 0:'NO FIX', 1:'DEAD', 2:'2D', 3:'3D', 4:'DGPS', 5:'RTK~', 6:'RTK' };
                    const fixEl = document.getElementById('qt_fix_type');
                    if (fixEl && g.fix_type != null) {
                        const fixN = parseInt(g.fix_type);
                        fixEl.textContent = fixNames[fixN] ?? fixN;
                        fixEl.className = 'qt-value ' + (fixN >= 3 ? 'val-green' : fixN === 2 ? 'val-amber' : 'val-red');
                    }

                    // Satellite count
                    const satEl = document.getElementById('qt_sat');
                    if (satEl && g.satellites_visible != null) {
                        const s = parseInt(g.satellites_visible);
                        satEl.textContent = s;
                        satEl.className = 'qt-value ' + (s >= 10 ? 'val-green' : s >= 6 ? 'val-amber' : 'val-red');
                    }

                    // HDOP & VDOP (×100 from MAVLink GPS_RAW_INT → divide by 100)
                    const hdop = (g.eph != null) ? (g.eph / 100).toFixed(2) : (g.hdop ?? null);
                    const vdop = (g.epv != null) ? (g.epv / 100).toFixed(2) : (g.vdop ?? null);
                    setQt('qt_hdop', hdop, 2, 1.5, 3.0);
                    setQt('qt_vdop', vdop, 2, 1.5, 3.0);
                    // ─────────────────────────────────────────────────────────
                } catch(e) { console.warn('[GPS panel]', e); }
            } else if (topic === TOPIC_STATUS) {
                try {
                    const s = JSON.parse(payload);
                    // Update mode badge
                    const modeBadge  = document.getElementById('flight_mode_text');
                    const modeWrap   = document.getElementById('flight_mode_badge');
                    const armedBadge = document.getElementById('armed_badge');
                    const cmdBadge   = document.getElementById('cmd_status_badge');

                    if (s.mode && modeBadge) {
                        modeBadge.textContent = s.mode;
                        const colors = { GUIDED:'#3b82f6', AUTO:'#8b5cf6', LAND:'#f59e0b',
                                         RTL:'#ef4444', LOITER:'#10b981', STABILIZE:'#06b6d4' };
                        modeWrap.style.color  = colors[s.mode] || '#9ca3af';
                        modeWrap.querySelector('span').style.color = colors[s.mode] || '#4b5563';
                    }

                    if (s.armed !== undefined && armedBadge) {
                        armedBadge.textContent  = s.armed ? 'ARMED ' : 'DISARMED';
                        armedBadge.style.color  = s.armed ? '#dc2626' : '#4b5563';
                        armedBadge.style.background = s.armed ? '#450a0a' : '#1f2937';
                    }

                    if (s.status && cmdBadge) {
                        const icons = { ARMED:'', DISARMED:'', FLYING:'', BUSY:'⏳',
                                        OK:'', WARN:'', ERROR:'' };
                        cmdBadge.textContent = `${icons[s.status] || ''} ${s.detail || s.status}`;
                        cmdBadge.style.color = s.status === 'ERROR' ? '#dc2626' :
                                               s.status === 'WARN'  ? '#f59e0b' :
                                               s.status === 'BUSY'  ? '#06b6d4' : '#10b981';
                        setTimeout(() => { if(cmdBadge) cmdBadge.textContent = ''; }, 5000);
                    }
                } catch(e) {}
            } else if (topic === TOPIC_ATTITUDE) {
                try {
                    const a = JSON.parse(payload);
                    if (typeof updateDrone3D === 'function') updateDrone3D(a);
                } catch(e) {}
            } else {
                // Ignore other background logs unless they are not telemetry
                if (!topic.includes("telemetry") && !topic.includes("sensors")) {
                    addLog(`[SUB] [${topic}] ${payload}`);
                }
            }
            return;
        }

        try {
            const d = JSON.parse(payload);
            const humVal = d.humidity !== undefined ? d.humidity : d.hum;
            const temp = (d.temp != null && !isNaN(+d.temp)) ? (+d.temp).toFixed(1) : '--';
            const hum  = (humVal  != null && !isNaN(+humVal))  ? (+humVal).toFixed(1)  : '--';
            const co2  = (d.co2   != null) ? d.co2 : '--';

            tempEl.textContent = temp;
            humEl.textContent  = hum;
            co2El.textContent  = co2;

            if (d.alert === 1) {
                alertEl.textContent = "CẢNH BÁO";
                alertEl.className   = "alert-active";
            } else {
                alertEl.textContent = "AN TOÀN";
                alertEl.className   = "ok";
            }

            if (d.rssi != null) rssiEl.textContent = d.rssi;
            if (d.distance != null) {
                sonarEl.textContent = d.distance >= 0 ? d.distance.toFixed(1) : 'ERR';
                if (d.distance >= 0 && d.distance < 50) {
                    sonarEl.style.color = '#ef4444'; // Danger red if too close
                } else {
                    sonarEl.style.color = 'var(--cyan)';
                }
            }

            // === [ENV-MAP] Cập nhật biến toàn cục cho bản đồ màu ===
            if (d.temp  != null && !isNaN(+d.temp))  latestTemp  = +d.temp;
            if (d.co2   != null && !isNaN(+d.co2))   latestCo2   = +d.co2;
            if (d.alert != null)                      latestAlert = d.alert;

            // === [ENV-MAP] Cập nhật overlay badges trên bản đồ ===
            const tempBadge = document.getElementById('env_temp_badge');
            const co2Badge  = document.getElementById('env_co2_badge');
            if (tempBadge && latestTemp !== null) {
                const cls = latestTemp >= 45 ? 'danger' : latestTemp >= 35 ? 'warn' : '';
                tempBadge.className = 'env-badge' + (cls ? ' ' + cls : '');
                tempBadge.querySelector('span').textContent = `Temp: ${latestTemp.toFixed(1)}°C`;
            }
            if (co2Badge && latestCo2 !== null) {
                const cls = latestCo2 >= 700 ? 'danger' : latestCo2 >= 400 ? 'warn' : '';
                co2Badge.className = 'env-badge' + (cls ? ' ' + cls : '');
                co2Badge.querySelector('span').textContent = `CO₂: ${latestCo2} ADC`;
            }


            // Charts
            pushChart('temp', d.temp,     'chart_temp_latest', 'no_data_temp', '°C');
            pushChart('hum',  humVal,     'chart_hum_latest',  'no_data_hum',  '%');
            pushChart('co2',  d.co2,      'chart_co2_latest',  'no_data_co2',  'ADC');
            pushChart('rssi', d.rssi,     'chart_rssi_latest', 'no_data_rssi', 'dBm');

        } catch (e) {
            addLog(`[ERR] Parse JSON thất bại: ${payload}`);
        }
    }

    /* Commands */
    function sendFlightCommand(cmdName) {
        if (!client || !isConnected) { addLog("[ERR] Chưa kết nối MQTT"); return; }
        const dangerous = ['ARM', 'DISARM', 'TAKEOFF', 'LAND', 'RTL'];
        if (dangerous.includes(cmdName) && !confirm(`Xác nhận gửi lệnh ${cmdName} đến Drone?\n\n Hãy đảm bảo drone ở vị trí an toàn.`)) return;
        const msg = buildMsg({ command: cmdName, alt: cmdName === "TAKEOFF" ? 10.0 : 0.0 }, TOPIC_FLIGHT);
        client.send(msg);
        addLog(`[PUB] ${cmdName} → ${TOPIC_FLIGHT}`);
    }

    function sendPayloadCommand(cmdName) {
        if (!client || !isConnected) { addLog("[ERR] Chưa kết nối MQTT"); return; }
        client.send(buildMsg({ command: cmdName }, TOPIC_PAYLOAD));
        addLog(`[PUB] ${cmdName} → ${TOPIC_PAYLOAD}`);
    }

    function updateServoDisplay(val) {
        document.getElementById("servo_angle_display").textContent = val;
    }

    function sendServoCommand(angle) {
        if (!client || !isConnected) { addLog("[ERR] Chưa kết nối MQTT"); return; }
        client.send(buildMsg({ command: "SERVO", angle: parseInt(angle) }, TOPIC_PAYLOAD));
        document.getElementById("servo_slider").value = angle;
        updateServoDisplay(angle);
        addLog(`[PUB] SERVO ${angle}° → ${TOPIC_PAYLOAD}`);
    }

    function buildMsg(obj, topic) {
        const MQTTMsg = (typeof Paho.MQTT !== "undefined") ? Paho.MQTT.Message : Paho.Message;
        const m = new MQTTMsg(JSON.stringify({ ...obj, timestamp: Date.now() }));
        m.destinationName = topic;
        return m;
    }

    /* [NEW] Motor PWM Bar Update */
    function updateMotorBars(m) {
        ['m1','m2','m3','m4'].forEach(id => {
            const val = m[id] || 1000;
            const pct = Math.min(100, Math.max(0, ((val - 1000) / 1000) * 100));
            const bar = document.getElementById('bar_' + id);
            const lbl = document.getElementById('lbl_' + id);
            if (bar) bar.style.height = pct + '%';
            if (lbl) lbl.textContent = val;
        });
    }

    /* [NEW] Send Wind Speed to fusion.py */
    function sendWindSpeed(val) {
        document.getElementById('wind_val_display').textContent = val + ' m/s';
        if (!client || !client.isConnected()) return;
        client.send(buildMsg({ wind_speed: parseFloat(val) }, TOPIC_WEATHER));
        addLog(`[PUB] Wind Speed ${val} m/s → ${TOPIC_WEATHER}`);
    }

    /* [NEW] Send Mission Command */
    function sendMissionCommand(cmd) {
        if (!client || !client.isConnected()) return;
        client.send(buildMsg({ command: cmd }, TOPIC_MISSION));
        addLog(`[PUB] Mission: ${cmd} → ${TOPIC_MISSION}`);
    }

    /* [NEW] Send Heartbeat Ping */
    let heartbeatTimer = null;
    function sendHeartbeat() {
        if (!client || !client.isConnected()) return;
        client.send(buildMsg({ ping: 1 }, TOPIC_HEARTBEAT));
        // addLog(`[PUB] Heartbeat Ping → ${TOPIC_HEARTBEAT}`); // Ẩn bớt log để đỡ trôi
    }

    /* [NEW] Send Sim Param (Survival Testing Suite) */
    function sendSimParam(paramName, value) {
        if (!client || !isConnected) { addLog('[ERR] Chưa kết nối MQTT'); return; }
        client.send(buildMsg({ param: paramName, value: parseFloat(value) }, TOPIC_SIM));
        addLog(`[PUB] Sim Param: ${paramName} = ${value} → ${TOPIC_SIM}`);
    }

    /* Geofence Failsafe */
    function enableGeofence() {
        if (!client || !isConnected) { addLog('[ERR] Chưa kết nối MQTT'); return; }
        sendSimParam('FENCE_ENABLE', 1);
        setTimeout(() => sendSimParam('FENCE_TYPE', 2), 300);
        setTimeout(() => sendSimParam('FENCE_RADIUS', 50), 600);
        setTimeout(() => sendSimParam('FENCE_ACTION', 1), 900);
        if (geofenceCircle && typeof geofenceCircle.setStyle === 'function') {
            geofenceCircle.setStyle({ color: 'orange', fillOpacity: 0.15 });
        }
        const st = document.getElementById('geo_status');
        if (st) { st.textContent = '🟠 Trạng thái: Đã bật (RTL 50m)'; st.style.color = '#f59e0b'; }
        addLog('[GEO]  Hàng rào điện tử 50m đã bật — RTL nếu vi phạm');
    }

    function disableGeofence() {
        sendSimParam('FENCE_ENABLE', 0);
        if (geofenceCircle && typeof geofenceCircle.setStyle === 'function') {
            geofenceCircle.setStyle({ color: 'gray', fillOpacity: 0.05 });
        }
        const st = document.getElementById('geo_status');
        if (st) { st.textContent = ' Trạng thái: Tắt'; st.style.color = 'var(--text-3)'; }
        addLog('[GEO] Hàng rào điện tử đã tắt');
    }

    function resetAllSim() {
        if (!client || !isConnected) { addLog('[ERR] Chưa kết nối MQTT'); return; }
        sendSimParam('SIM_ENGINE_FAIL', 0);
        setTimeout(() => sendSimParam('SIM_GPS_DISABLE', 0), 200);
        setTimeout(() => sendSimParam('SIM_WIND_TURB', 0), 400);
        setTimeout(() => { sendWindSpeed(0); document.getElementById('wind_slider').value = 0; document.getElementById('turb_slider').value = 0; document.getElementById('turb_val').innerText = 0; }, 600);
        setTimeout(() => disableGeofence(), 800);
        addLog('[SIM]  Đã reset toàn bộ tham số SITL về mặc định');
    }

    /* ============================================================
       MAP — Leaflet.js Live Trajectory + Waypoint Mission
    ============================================================ */
    let map = null;
    let droneMapMarker = null;
    let droneTrail = null;
    let waypoints = [];
    let waypointMarkers = [];
    let waypointPolyline = null;
    let geofenceCircle = null;
    let mapInitialized = false;
    let droneIcon = null;

    // === Heatmap Variables ===
    let heatLayer = null;
    let heatData = [];
    let isHeatmapActive = false;

    // === [ENV-MAP] Color-coded trajectory state ===
    let trailSegments = [];        // Mảng các polyline ngắn có màu
    let pollutionMarkers = [];     // Mảng icon cảnh báo trên bản đồ
    let lastTrailLatLng = null;    // Điểm cuối của đoạn trước để nối liền

    // Biến chia sẻ dữ liệu cảm biến mới nhất cho bản đồ
    let latestTemp  = null;
    let latestCo2   = null;
    let latestAlert = 0;

    // Hàm tính màu quỹ đạo theo ngưỡng môi trường
    function getTrailColor() {
        if (latestAlert === 1)           return '#dc2626'; // Đỏ: Cảnh báo khẩn cấp
        if (latestTemp !== null) {
            if (latestTemp >= 45)        return '#dc2626'; // Đỏ: Nhiệt độ cực cao (cháy rừng)
            if (latestTemp >= 35)        return '#f59e0b'; // Vàng: Nhiệt độ cao
        }
        if (latestCo2 !== null && !isNaN(latestCo2)) {
            if (latestCo2 >= 700)        return '#dc2626'; // Đỏ: CO2 nguy hiểm
            if (latestCo2 >= 400)        return '#f59e0b'; // Vàng: CO2 trung bình
        }
        return '#16a34a';                                  // Xanh: Môi trường trong lành
    }

    function initMap(lat, lon) {
        if (mapInitialized) return;
        const container = document.getElementById('map');
        if (!container) return;
        if (typeof L === 'undefined') {
            addLog('[MAP] Leaflet chưa tải được. Kiểm tra kết nối mạng.');
            return;
        }
        map = L.map('map', { zoomControl: true, scrollWheelZoom: true }).setView([lat, lon], 18);
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            attribution: '© OpenStreetMap contributors',
            maxZoom: 22
        }).addTo(map);

        droneIcon = L.divIcon({
            html: `<div id="drone-icon-wrap" style="width:28px;height:28px;display:flex;align-items:center;justify-content:center;transform-origin:center center;">
                <svg width="28" height="28" viewBox="0 0 28 28" fill="none" xmlns="http://www.w3.org/2000/svg">
                    <circle cx="14" cy="14" r="5" fill="#38bdf8" opacity="0.95"/>
                    <polygon points="14,3 17,12 14,10 11,12" fill="#f8fafc" opacity="0.95"/>
                    <line x1="14" y1="9" x2="5" y2="5" stroke="#94a3b8" stroke-width="1.5"/>
                    <line x1="14" y1="9" x2="23" y2="5" stroke="#94a3b8" stroke-width="1.5"/>
                    <line x1="14" y1="19" x2="5" y2="23" stroke="#94a3b8" stroke-width="1.5"/>
                    <line x1="14" y1="19" x2="23" y2="23" stroke="#94a3b8" stroke-width="1.5"/>
                    <circle cx="5" cy="5" r="3.5" stroke="#38bdf8" stroke-width="1.2" fill="none" opacity="0.7"/>
                    <circle cx="23" cy="5" r="3.5" stroke="#38bdf8" stroke-width="1.2" fill="none" opacity="0.7"/>
                    <circle cx="5" cy="23" r="3.5" stroke="#38bdf8" stroke-width="1.2" fill="none" opacity="0.7"/>
                    <circle cx="23" cy="23" r="3.5" stroke="#38bdf8" stroke-width="1.2" fill="none" opacity="0.7"/>
                    <circle cx="14" cy="14" r="8" stroke="#38bdf8" stroke-width="0.6" fill="none" opacity="0.3"/>
                </svg>
            </div>`,
            className: '',
            iconSize: [28, 28],
            iconAnchor: [14, 14]
        });
        droneMapMarker = L.marker([lat, lon], { icon: droneIcon }).addTo(map).bindTooltip('Drone');
        droneTrail = null;

        // Thêm Legend bảng chú giải màu môi trường vào bản đồ
        const legend = L.control({ position: 'bottomleft' });
        legend.onAdd = function() {
            const div = L.DomUtil.create('div');
            div.style.cssText = 'background:#1e293b;color:#f1f5f9;padding:8px 10px;border-radius:8px;font-size:11px;font-family:monospace;border:1px solid #334155;line-height:1.7;';
            div.innerHTML = '<b style="color:#94a3b8"> Chất lượng không khí</b><br>'
                          + '<span style="color:#16a34a"></span> An toàn (Temp &lt;35°C, CO2 thấp)<br>'
                          + '<span style="color:#f59e0b"></span> Chú ý (Temp 35–45°C, CO2 400–700)<br>'
                          + '<span style="color:#dc2626"></span> Nguy hiểm (Temp &gt;45°C / Alert)<br>'
                          + '<span></span> Điểm phát hiện khí độc';
            return div;
        };
        legend.addTo(map);
        waypointPolyline = L.polyline([], { color: '#3b82f6', weight: 2, dashArray: '6 4' }).addTo(map);
        geofenceCircle = L.circle([lat, lon], {
            radius: 50, color: 'gray', weight: 1.5,
            fillColor: '#f59e0b', fillOpacity: 0.05
        }).addTo(map).bindTooltip('Geofence 50m');

        map.on('click', function(e) {
            const n = waypoints.length + 1;
            waypoints.push([e.latlng.lat, e.latlng.lng]);
            const m = L.circleMarker(e.latlng, {
                radius: 6, color: '#3b82f6', fillColor: '#3b82f6', fillOpacity: 0.9, weight: 2
            }).addTo(map).bindTooltip('WP' + n);
            waypointMarkers.push(m);
            waypointPolyline.setLatLngs(waypoints);
            const wp_el = document.getElementById('wp_count_display');
            if (wp_el) wp_el.textContent = waypoints.length + ' diem';
            addLog(`[MAP] Them Waypoint ${n}: ${e.latlng.lat.toFixed(6)}, ${e.latlng.lng.toFixed(6)}`);
        });

        // Nut Follow Drone (toggle)
        const followCtrl = L.control({ position: 'topleft' });
        followCtrl.onAdd = function() {
            const btn = L.DomUtil.create('button');
            btn.id = 'btn_follow';
            btn.style.cssText = 'backdrop-filter:blur(6px);background:rgba(8,12,16,.82);color:#22d3a4;font-size:10px;padding:3px 8px;margin-top:44px;cursor:pointer;border:1px solid #1e2d3d;border-radius:4px;font-family:JetBrains Mono,monospace;font-weight:700;';
            btn.textContent = 'FOLLOW: ON';
            L.DomEvent.on(btn, 'click', function(ev) {
                L.DomEvent.stopPropagation(ev);
                mapFollowDrone = !mapFollowDrone;
                btn.textContent = mapFollowDrone ? 'FOLLOW: ON' : 'FOLLOW: OFF';
                btn.style.color = mapFollowDrone ? '#22d3a4' : '#4a6278';
            });
            return btn;
        };
        followCtrl.addTo(map);

        mapInitialized = true;
        addLog('[MAP] Ban do da khoi dong!');
    }

    // State: auto-follow toggle
    let mapFollowDrone = true;


    function toggleHeatmap() {
        isHeatmapActive = !isHeatmapActive;
        const btn = document.getElementById('btnHeatmap');
        if (isHeatmapActive) {
            btn.style.background = 'var(--cyan)';
            btn.style.color = '#000';
            btn.textContent = 'Tắt Heatmap';
            if (!heatLayer) {
                heatLayer = L.heatLayer(heatData, {radius: 25, blur: 15, maxZoom: 17, minOpacity: 0.5});
            }
            heatLayer.addTo(map);
            addLog('[MAP] Đã BẬT Bản đồ Nhiệt độ');
        } else {
            btn.style.background = 'rgba(8,12,16,.82)';
            btn.style.color = 'var(--text-1)';
            btn.textContent = 'Bật Heatmap';
            if (heatLayer) map.removeLayer(heatLayer);
            addLog('[MAP] Đã TẮT Bản đồ Nhiệt độ');
        }
    }

    function updateMap(lat, lon, headingDeg) {
        if (!mapInitialized) {
            initMap(lat, lon);
            return;
        }
        droneMapMarker.setLatLng([lat, lon]);

        // -- Rotate drone SVG icon theo heading (0 deg = North) --
        if (headingDeg !== undefined && headingDeg !== null && !isNaN(headingDeg)) {
            const wrap = document.getElementById('drone-icon-wrap');
            if (wrap) wrap.style.transform = 'rotate(' + headingDeg + 'deg)';
        }

        // -- Auto-pan map theo drone neu FOLLOW: ON --
        if (mapFollowDrone && map) {
            const bounds = map.getBounds();
            const ns = bounds.getNorth() - bounds.getSouth();
            const ew = bounds.getEast()  - bounds.getWest();
            const dLat = Math.abs(lat - bounds.getCenter().lat);
            const dLon = Math.abs(lon - bounds.getCenter().lng);
            // Pan khi drone ra khoai 30% vung nhin hien tai
            if (!bounds.contains([lat, lon]) || dLat > ns * 0.3 || dLon > ew * 0.3) {
                map.panTo([lat, lon], { animate: true, duration: 0.6, easeLinearity: 0.4 });
            }
        }


        // Ghi dữ liệu cho Heatmap
        if (latestTemp !== null) {
            let intensity = Math.max(0, (latestTemp - 25) * 10); // Scale temp to intensity
            heatData.push([lat, lon, intensity]);
            if (heatData.length > 500) heatData.shift(); // Keep last 500 points
            if (isHeatmapActive && heatLayer) {
                heatLayer.setLatLngs(heatData);
            }
        }

        // === [ENV-MAP] Vẽ đoạn quỹ đạo có màu theo cảm biến ===
        const currentLatLng = [lat, lon];
        const color = getTrailColor();

        if (lastTrailLatLng !== null) {
            // Vẽ đoạn polyline từ điểm trước tới điểm hiện tại với màu hiện tại
            const segment = L.polyline([lastTrailLatLng, currentLatLng], {
                color: color,
                weight: 4,
                opacity: 0.85
            }).addTo(map);
            trailSegments.push(segment);

            // Giữ tối đa 300 đoạn để không lag bản đồ
            if (trailSegments.length > 300) {
                const old = trailSegments.shift();
                map.removeLayer(old);
            }

            // Nếu có cảnh báo Alert = 1 → Thả icon  tại tọa độ hiện tại
            if (latestAlert === 1) {
                const lastPMarker = pollutionMarkers[pollutionMarkers.length - 1];
                // Tránh thêm icon trùng lặp quá gần nhau (cách nhau ít nhất 0.0001 độ)
                const tooClose = lastPMarker &&
                    Math.abs(lastPMarker.getLatLng().lat - lat) < 0.0001 &&
                    Math.abs(lastPMarker.getLatLng().lng - lon) < 0.0001;

                if (!tooClose) {
                    const pIcon = L.divIcon({
                        html: '<div style="font-size:18px;filter:drop-shadow(0 0 4px #dc2626);">⚠️</div>',
                        className: '', iconSize: [22, 22], iconAnchor: [11, 11]
                    });
                    const pMarker = L.marker([lat, lon], { icon: pIcon })
                        .addTo(map)
                        .bindTooltip(` Khí độc phát hiện!\nTemp: ${latestTemp}°C | CO2: ${latestCo2}`, {
                            permanent: false, direction: 'top'
                        });
                    pollutionMarkers.push(pMarker);
                    addLog(`[ENV]  Điểm ô nhiễm được ghi nhận tại (${lat.toFixed(5)}, ${lon.toFixed(5)})`);
                    
                    // Giữ tối đa 300 điểm cảnh báo để không lag
                    if (pollutionMarkers.length > 300) {
                        const old = pollutionMarkers.shift();
                        map.removeLayer(old);
                    }
                }
            }
        }
        lastTrailLatLng = currentLatLng;
    }

    function clearMap() {
        if (!mapInitialized) return;
        // Xóa Waypoints
        waypoints = [];
        waypointMarkers.forEach(m => map.removeLayer(m));
        waypointMarkers = [];
        waypointPolyline.setLatLngs([]);
        // [ENV-MAP] Xóa các đoạn quỹ đạo màu
        trailSegments.forEach(s => map.removeLayer(s));
        trailSegments = [];
        lastTrailLatLng = null;
        // [ENV-MAP] Xóa các icon cảnh báo ô nhiễm
        pollutionMarkers.forEach(m => map.removeLayer(m));
        pollutionMarkers = [];
        const wp_el = document.getElementById('wp_count_display');
        if (wp_el) wp_el.textContent = '0 điểm';
        addLog('[MAP] Đã xóa tất cả waypoints, quỹ đạo màu và điểm ô nhiễm');
    }

    function uploadMission() {
        if (!client || !isConnected) { addLog('[ERR] Chưa kết nối MQTT'); return; }
        if (waypoints.length === 0) {
            addLog('[MAP]  Vui lòng click lên bản đồ để tạo waypoint trước!');
            return;
        }
        const pts = waypoints.map(wp => ({ lat: wp[0], lon: wp[1] }));
        client.send(buildMsg({ command: 'UPLOAD', points: pts }, TOPIC_MISSION));
        addLog(`[MAP]  Đang upload ${pts.length} waypoint lên Drone...`);
    }

    /* Log */
    // Log buffer - keep full history in memory, show only latest in ticker
    let _logBuffer = [];

    function addLog(msg) {
        const d = new Date();
        const ts = `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
        const line = `[${ts}] ${msg}`;
        _logBuffer.push(line);
        if (_logBuffer.length > 500) _logBuffer.shift();
        // Single-line ticker: show only latest entry
        if (logConsole) {
            logConsole.value = line;
        }
        logLineCount = _logBuffer.length;
    }
    function clearLogs() {
        _logBuffer = [];
        logLineCount = 0;
        if (logConsole) logConsole.value = '';
    }
    function pad(n) { return String(n).padStart(2, '0'); }

    /* Init */
    updateUIConnected(false);
    // Auto-connect on load
    setTimeout(startConnection, 500);

    /* ==============================================================
       [NEW] THREE.JS 3D DRONE MODEL IMPLEMENTATION
       ============================================================== */
    let scene, camera, renderer, droneGroup;
    let propMeshes = [];
    let propSpeeds = [0, 0, 0, 0];

    function init3D() {
        const container = document.getElementById('drone3d_container');
        if (!container) return;
        const width = container.clientWidth;
        const height = container.clientHeight;

        scene = new THREE.Scene();
        scene.background = new THREE.Color(0x0b0c10);
        camera = new THREE.PerspectiveCamera(45, width / height, 0.1, 1000);
        camera.position.set(3, 2, 4);
        camera.lookAt(0, 0, 0);

        renderer = new THREE.WebGLRenderer({ antialias: true });
        renderer.setSize(width, height);
        container.appendChild(renderer.domElement);

        scene.add(new THREE.AmbientLight(0xffffff, 0.6));
        const pointLight = new THREE.PointLight(0x00ffcc, 1, 10);
        pointLight.position.set(2, 5, 2);
        scene.add(pointLight);

        // Grid helper
        const gridHelper = new THREE.GridHelper(10, 20, 0x00ffcc, 0x1f2833);
        gridHelper.position.y = -0.5;
        scene.add(gridHelper);

        droneGroup = new THREE.Group();
        
        // Body
        droneGroup.add(new THREE.Mesh(new THREE.BoxGeometry(1, 0.3, 1), new THREE.MeshPhongMaterial({ color: 0x1f2833 })));
        
        // Front Indicator
        const frontMark = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.1, 0.1), new THREE.MeshBasicMaterial({ color: 0xff0000 }));
        frontMark.position.set(0, 0.15, -0.5);
        droneGroup.add(frontMark);
        
        // Arms and Propellers
        const armLength = 1.2;
        const positions = [
            { x: armLength, z: -armLength },
            { x: -armLength, z: armLength },
            { x: armLength, z: armLength },
            { x: -armLength, z: -armLength }
        ];

        positions.forEach((pos) => {
            const prop = new THREE.Mesh(new THREE.CylinderGeometry(0.6, 0.6, 0.02, 16), new THREE.MeshBasicMaterial({ color: 0x00ffcc, transparent: true, opacity: 0.8 }));
            prop.position.set(pos.x, 0.2, pos.z);
            droneGroup.add(prop);
            propMeshes.push(prop);

            const armGeo = new THREE.CylinderGeometry(0.05, 0.05, Math.sqrt(pos.x*pos.x + pos.z*pos.z));
            const arm = new THREE.Mesh(armGeo, new THREE.MeshPhongMaterial({ color: 0x888888 }));
            arm.rotation.x = Math.PI / 2;
            arm.rotation.z = Math.atan2(pos.x, pos.z);
            arm.position.set(pos.x/2, 0, pos.z/2);
            droneGroup.add(arm);
        });

        scene.add(droneGroup);
        animate3D();
    }

    function animate3D() {
        requestAnimationFrame(animate3D);
        for (let i = 0; i < 4; i++) {
            if (propSpeeds[i] > 0) propMeshes[i].rotation.y += propSpeeds[i] * 0.5;
        }
        renderer.render(scene, camera);
    }

    function updateDrone3D(data) {
        if (!droneGroup) return;
        droneGroup.rotation.x = -(data.pitch || 0);
        droneGroup.rotation.z = -(data.roll || 0);
        droneGroup.rotation.y = -(data.yaw || 0);
        document.getElementById('att_r').innerText = ((data.roll || 0) * 180/Math.PI).toFixed(1);
        document.getElementById('att_p').innerText = ((data.pitch || 0) * 180/Math.PI).toFixed(1);
        document.getElementById('att_y').innerText = ((data.yaw || 0) * 180/Math.PI).toFixed(1);
    }

    function updatePropellerSpeeds(data) {
        const pwm = [data.m1, data.m2, data.m3, data.m4];
        propSpeeds = pwm.map(v => (v > 1050 ? (v - 1000) / 1000.0 : 0));
        const spinning = propSpeeds.some(v => v > 0);
        const el = document.getElementById('eng_st');
        if (el) {
            el.innerText = spinning ? 'SPINNING' : 'STANDBY';
            el.style.color = spinning ? '#00ffcc' : '#9ca3af';
        }
    }

    window.addEventListener('load', function() {
        init3D();
        initMap(10.8231, 106.6297); // Khởi tạo bản đồ ngay lập tức với tọa độ mặc định (SITL/TP.HCM)
    });