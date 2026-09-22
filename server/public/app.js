(() => {
  const canvas = document.getElementById('ecgCanvas');
  const ctx = canvas.getContext('2d');
  const statusDot = document.getElementById('statusDot');
  const statusText = document.getElementById('statusText');
  const bpmValueEl = document.getElementById('bpmValue');
  const bpmSubEl = document.getElementById('bpmSub');
  const rateInfoEl = document.getElementById('rateInfo');
  const sampleCountEl = document.getElementById('sampleCount');

  const HISTORY_SECONDS = 6;
  const MAX_POINTS = 250 * HISTORY_SECONDS; // assumes ~250Hz, buffer just needs to be "enough"

  /** @type {{v:number, t:number}[]} */
  let buffer = [];
  let totalSamples = 0;
  let lastRateCalcTime = 0;
  let lastRateCalcCount = 0;

  // --- Peak / BPM detection state ---
  let armed = true;
  let lastPeakT = -Infinity;
  let peakTimes = [];
  const REFRACTORY_MS = 280; // ~214 bpm max, filters double-triggering on one QRS

  function resizeCanvas() {
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }
  window.addEventListener('resize', resizeCanvas);
  resizeCanvas();

  function updatePeakDetection(sample) {
    if (buffer.length < 20) return;

    const windowSlice = buffer.slice(-500);
    let min = Infinity, max = -Infinity;
    for (const p of windowSlice) {
      if (p.v < min) min = p.v;
      if (p.v > max) max = p.v;
    }
    const range = max - min;
    if (range < 5) return; // flat/no signal, don't false-trigger on noise

    const highThresh = min + range * 0.62;
    const lowThresh = min + range * 0.5;

    if (armed && sample.v > highThresh && (sample.t - lastPeakT) > REFRACTORY_MS) {
      lastPeakT = sample.t;
      peakTimes.push(sample.t);
      if (peakTimes.length > 8) peakTimes.shift();
      armed = false;
      updateBpmDisplay();
    } else if (!armed && sample.v < lowThresh) {
      armed = true;
    }
  }

  function updateBpmDisplay() {
    if (peakTimes.length < 2) {
      bpmSubEl.textContent = 'detecting…';
      return;
    }
    const intervals = [];
    for (let i = 1; i < peakTimes.length; i++) {
      intervals.push(peakTimes[i] - peakTimes[i - 1]);
    }
    const avgInterval = intervals.reduce((a, b) => a + b, 0) / intervals.length;
    const bpm = Math.round(60000 / avgInterval);

    if (bpm >= 30 && bpm <= 220) {
      bpmValueEl.textContent = bpm;
      bpmSubEl.textContent = `over last ${peakTimes.length} beats`;
    }
  }

  function draw() {
    requestAnimationFrame(draw);
    const rect = canvas.getBoundingClientRect();
    const w = rect.width;
    const h = rect.height;
    ctx.clearRect(0, 0, w, h);

    if (buffer.length < 2) return;

    const visible = buffer.slice(-MAX_POINTS);
    const tMin = visible[0].t;
    const tMax = visible[visible.length - 1].t;
    const tSpan = Math.max(1, tMax - tMin);

    let vMin = Infinity, vMax = -Infinity;
    for (const p of visible) {
      if (p.v < vMin) vMin = p.v;
      if (p.v > vMax) vMax = p.v;
    }
    const vSpan = Math.max(1, vMax - vMin);
    const pad = 0.12 * h;

    ctx.beginPath();
    ctx.lineWidth = 2;
    ctx.strokeStyle = '#33e07a';
    ctx.shadowColor = 'rgba(51,224,122,0.6)';
    ctx.shadowBlur = 4;

    visible.forEach((p, i) => {
      const x = ((p.t - tMin) / tSpan) * w;
      const y = h - pad - ((p.v - vMin) / vSpan) * (h - 2 * pad);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
  }
  requestAnimationFrame(draw);

  function setStatus(connected, text) {
    statusDot.classList.toggle('connected', connected);
    statusText.textContent = text;
  }

  function connectWebSocket() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    const ws = new WebSocket(`${proto}://${location.host}/ws`);

    ws.addEventListener('open', () => setStatus(true, 'Connected'));

    ws.addEventListener('message', (event) => {
      let msg;
      try {
        msg = JSON.parse(event.data);
      } catch (err) {
        return;
      }
      if (msg.type !== 'sample') return;

      const sample = { v: msg.v, t: msg.t };
      buffer.push(sample);
      if (buffer.length > MAX_POINTS * 2) buffer = buffer.slice(-MAX_POINTS);

      totalSamples++;
      sampleCountEl.textContent = `${totalSamples} samples`;

      const now = performance.now();
      if (now - lastRateCalcTime > 1000) {
        const rate = totalSamples - lastRateCalcCount;
        rateInfoEl.textContent = `${rate} Hz`;
        lastRateCalcTime = now;
        lastRateCalcCount = totalSamples;
      }

      updatePeakDetection(sample);
    });

    ws.addEventListener('close', () => {
      setStatus(false, 'Disconnected — retrying…');
      setTimeout(connectWebSocket, 1500);
    });

    ws.addEventListener('error', () => {
      ws.close();
    });
  }

  connectWebSocket();
})();
