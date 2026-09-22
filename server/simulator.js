/**
 * Fake "ESP32 + AD8232" data source for local testing without hardware.
 * Connects to the server exactly like the real board would and streams a
 * synthetic PQRST ECG waveform at a configurable heart rate.
 *
 * Usage:
 *   node simulator.js [bpm] [wsUrl]
 *   node simulator.js 90
 *   node simulator.js 72 ws://localhost:8080/ws
 */
const WebSocket = require('ws');

const targetBpm = Number(process.argv[2]) || 75;
const url = process.argv[3] || 'ws://localhost:8080/ws';

const SAMPLE_RATE_HZ = 250;
const DT = 1 / SAMPLE_RATE_HZ;

// Gaussian "bumps" that approximate a PQRST complex over one normalized
// cardiac cycle (phase 0..1). Not clinically accurate, just realistic-looking.
const WAVES = [
  { center: 0.16, width: 0.025, amp: 0.15 }, // P
  { center: 0.32, width: 0.008, amp: -0.15 }, // Q
  { center: 0.35, width: 0.008, amp: 1.0 }, // R
  { center: 0.38, width: 0.008, amp: -0.25 }, // S
  { center: 0.55, width: 0.045, amp: 0.35 }, // T
];

function ecgValueAtPhase(phase) {
  let v = 0;
  for (const w of WAVES) {
    const d = phase - w.center;
    v += w.amp * Math.exp(-(d * d) / (2 * w.width * w.width));
  }
  return v;
}

let phase = 0;
let bpm = targetBpm;
const ADC_MID = 2048; // ESP32 12-bit ADC midpoint
const ADC_SCALE = 700;

function connect() {
  const ws = new WebSocket(url);
  const start = Date.now();
  // Windows/Node timer resolution can't reliably fire setInterval every 4ms
  // (it often coalesces to ~15-16ms ticks). To still emit real 250Hz data,
  // each tick computes how much simulated time has actually elapsed and
  // generates/sends a catch-up batch of samples instead of exactly one.
  let samplesEmitted = 0;

  ws.on('open', () => {
    console.log(`[simulator] connected to ${url}, streaming ~${targetBpm} BPM at ${SAMPLE_RATE_HZ}Hz`);
    setInterval(() => {
      const elapsedMs = Date.now() - start;
      const samplesDue = Math.floor(elapsedMs / (1000 / SAMPLE_RATE_HZ)) - samplesEmitted;
      if (samplesDue <= 0 || ws.readyState !== WebSocket.OPEN) return;

      for (let i = 0; i < samplesDue; i++) {
        samplesEmitted++;
        const sampleTimeMs = samplesEmitted * (1000 / SAMPLE_RATE_HZ);

        // Slight natural heart-rate variability (+-2 bpm)
        bpm = targetBpm + (Math.random() - 0.5) * 4;
        const cyclesPerSecond = bpm / 60;

        phase += cyclesPerSecond * DT;
        if (phase >= 1) phase -= 1;

        const clean = ecgValueAtPhase(phase);
        const noise = (Math.random() - 0.5) * 0.02;
        const baselineWander = Math.sin(sampleTimeMs / 3000) * 0.03;

        const value = Math.round(ADC_MID + (clean + noise + baselineWander) * ADC_SCALE);
        ws.send(JSON.stringify({ v: value, t: Math.round(sampleTimeMs) }));
      }
    }, 4);
  });

  ws.on('close', () => {
    console.log('[simulator] disconnected, retrying in 2s...');
    setTimeout(connect, 2000);
  });

  ws.on('error', (err) => {
    console.error('[simulator] error:', err.message);
  });
}

connect();
