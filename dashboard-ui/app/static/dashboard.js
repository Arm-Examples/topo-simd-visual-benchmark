const DASHBOARD_CONFIG = window.dashboardConfig || {
    backends: ['scalar', 'neon', 'sve'],
};
const BACKENDS = DASHBOARD_CONFIG.backends;

// Dashboard state
let continuousMode = false;
let continuousInterval = null;

// Initialize on page load
document.addEventListener('DOMContentLoaded', () => {
    checkBackendStatus();
    setupEventListeners();
});

function setupEventListeners() {
    document.getElementById('run-benchmark').addEventListener('click', runBenchmark);
    document.getElementById('continuous-mode').addEventListener('click', toggleContinuousMode);
}

async function checkBackendStatus() {
    try {
        const response = await fetch('/api/backends/status');
        const data = await response.json();

        data.backends.forEach(backend => {
            const statusEl = document.getElementById(`${backend.name}-status`);
            if (statusEl) {
                const color = backend.status === 'online' ? 'var(--sve-color)' : 'var(--scalar-color)';
                statusEl.innerHTML = `<span style="color: ${color}">●</span> ${backend.status}`;
            }

            if (backend.build_info) {
                const buildInfoEl = document.getElementById(`${backend.name}-build-info`);
                if (buildInfoEl) {
                    buildInfoEl.innerHTML = `<code>${backend.build_info.build_march}</code>`;
                }
            }
        });
    } catch (error) {
        console.error('Failed to check backend status:', error);
    }
}


function showLoading(backend, show = true) {
    const loadingEl = document.getElementById(`${backend}-loading`);
    if (!loadingEl) {
        return;
    }
    if (show) {
        loadingEl.classList.add('active');
    } else {
        loadingEl.classList.remove('active');
    }
}

function updateProgress(backend, percent) {
    const progressEl = document.getElementById(`${backend}-progress`);
    if (!progressEl) {
        return;
    }
    progressEl.style.width = `${percent}%`;
}

function updateMetrics(backend, data) {
    const timeEl = document.getElementById(`${backend}-time`);
    const fpsEl = document.getElementById(`${backend}-fps`);
    const speedupEl = document.getElementById(`${backend}-speedup`);

    if (!timeEl || !fpsEl || !speedupEl) {
        return;
    }

    if (data.status === 'success') {
        const time = data.process_time_ms.toFixed(2);
        const fps = (1000 / data.process_time_ms).toFixed(1);
        const speedup = data.speedup ? data.speedup.toFixed(2) + 'x' : '—';

        timeEl.textContent = `${time} ms`;
        fpsEl.textContent = `${fps} FPS`;
        speedupEl.textContent = speedup;

        // Render image if available
        if (data.image_data) {
            renderImage(backend, data.image_data, data.width, data.height, data.original_image_data);
        }
    } else {
        timeEl.textContent = 'Error';
        fpsEl.textContent = '—';
        speedupEl.textContent = '—';
    }
}

function decodeRGBBase64(base64Data, width, height) {
    const binaryString = atob(base64Data);
    const len = binaryString.length;
    const bytes = new Uint8Array(len);
    for (let i = 0; i < len; i++) {
        bytes[i] = binaryString.charCodeAt(i);
    }

    const expectedBytes = width * height * 3;
    if (bytes.length < expectedBytes) {
        console.error(`Not enough data: got ${bytes.length}, need ${expectedBytes}`);
        return null;
    }

    const imageData = new ImageData(width, height);
    for (let i = 0; i < width * height; i++) {
        const srcIdx = i * 3;
        const dstIdx = i * 4;
        imageData.data[dstIdx + 0] = bytes[srcIdx + 0];
        imageData.data[dstIdx + 1] = bytes[srcIdx + 1];
        imageData.data[dstIdx + 2] = bytes[srcIdx + 2];
        imageData.data[dstIdx + 3] = 255;
    }
    return imageData;
}

function renderImage(backend, base64Data, width, height, originalBase64) {
    const canvas = document.getElementById(`${backend}-canvas`);
    if (!canvas) return;

    const ctx = canvas.getContext('2d');

    try {
        const imageData = decodeRGBBase64(base64Data, width, height);
        if (!imageData) return;

        canvas.width = width;
        canvas.height = height;
        ctx.putImageData(imageData, 0, 0);

        if (originalBase64) {
            const origData = decodeRGBBase64(originalBase64, width, height);
            if (!origData) return;

            const origCanvas = document.createElement('canvas');
            origCanvas.width = width;
            origCanvas.height = height;
            origCanvas.getContext('2d').putImageData(origData, 0, 0);

            const cssW = canvas.clientWidth || width;
            const scale = width / cssW;
            const pipCss = 80;
            const padCss = 6;
            const borderCss = 2;

            const pipW = Math.round(pipCss * scale);
            const pipH = Math.round(pipCss * scale);
            const pad  = Math.round(padCss * scale);
            const border = Math.round(borderCss * scale);

            const pipX = width  - pipW - pad;
            const pipY = height - pipH - pad;

            ctx.fillStyle = 'rgba(0, 0, 0, 0.6)';
            ctx.fillRect(pipX - border, pipY - border, pipW + border * 2, pipH + border * 2);
            ctx.drawImage(origCanvas, pipX, pipY, pipW, pipH);
        }
    } catch (error) {
        console.error(`Error rendering ${backend}:`, error);
    }
}

async function runBenchmark() {
    const imageSize = parseInt(document.getElementById('image-size').value);
    const blurRadius = parseInt(document.getElementById('blur-radius').value);
    const iterations = parseInt(document.getElementById('iterations').value);

    // Show loading indicators
    BACKENDS.forEach(backend => showLoading(backend, true));

    // Reset progress bars
    BACKENDS.forEach(backend => updateProgress(backend, 0));

    try {
        const response = await fetch('/api/process', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                width: imageSize,
                height: imageSize,
                blur_radius: blurRadius,
                iterations: iterations
            })
        });

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        const data = await response.json();

        // Update results for each backend
        data.results.forEach(result => {
            const backend = result.backend;
            showLoading(backend, false);
            updateProgress(backend, 100);
            updateMetrics(backend, result);
        });

    } catch (error) {
        console.error('Benchmark failed:', error);

        BACKENDS.forEach(backend => showLoading(backend, false));
    }
}

function toggleContinuousMode() {
    const button = document.getElementById('continuous-mode');

    if (continuousMode) {
        // Stop continuous mode
        continuousMode = false;
        clearInterval(continuousInterval);
        button.textContent = 'Continuous';
        button.classList.remove('active');
    } else {
        // Start continuous mode
        continuousMode = true;
        button.textContent = 'Stop';
        button.classList.add('active');

        // Run immediately
        runBenchmark();

        // Then run every 5 seconds
        continuousInterval = setInterval(runBenchmark, 5000);
    }
}

// Periodically check backend status
setInterval(checkBackendStatus, 30000);
