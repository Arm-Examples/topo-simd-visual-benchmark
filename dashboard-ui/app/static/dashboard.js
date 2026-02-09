const DASHBOARD_CONFIG = window.dashboardConfig || {
    backends: ['scalar', 'neon', 'sve'],
    sveEnabled: true,
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

        const statusHtml = data.backends.map(backend => {
            const color = backend.status === 'online' ? 'var(--sve-color)' : 'var(--scalar-color)';
            return `<div class="backend-status-item">
                <span style="color: ${color}">●</span>
                <span>${backend.name}: ${backend.status}</span>
            </div>`;
        }).join('');

        document.getElementById('backend-status').innerHTML = statusHtml;

        // Update build info if available
        data.backends.forEach(backend => {
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

function setStatus(text, active = false) {
    const statusText = document.getElementById('status-text');
    const statusDot = document.querySelector('.status-dot');

    statusText.textContent = text;

    if (active) {
        statusDot.classList.add('active');
    } else {
        statusDot.classList.remove('active');
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
            renderImage(backend, data.image_data, data.width, data.height);
        }
    } else {
        timeEl.textContent = 'Error';
        fpsEl.textContent = '—';
        speedupEl.textContent = '—';
    }
}

function renderImage(backend, base64Data, width, height) {
    const canvas = document.getElementById(`${backend}-canvas`);
    if (!canvas) {
        return;
    }

    const ctx = canvas.getContext('2d');

    // Decode base64
    try {
        const binaryString = atob(base64Data);
        const len = binaryString.length;
        const bytes = new Uint8Array(len);
        for (let i = 0; i < len; i++) {
            bytes[i] = binaryString.charCodeAt(i);
        }

        // Always resize canvas to ensure it's correct
        canvas.width = width;
        canvas.height = height;

        // Clear canvas first
        ctx.clearRect(0, 0, width, height);

        // Create ImageData and render
        const imageData = ctx.createImageData(width, height);

        // Check if we have enough data
        const expectedBytes = width * height * 3;
        if (bytes.length < expectedBytes) {
            console.error(`Not enough data: got ${bytes.length}, need ${expectedBytes}`);
            return;
        }

        for (let i = 0; i < width * height; i++) {
            const srcIdx = i * 3;
            const dstIdx = i * 4;
            imageData.data[dstIdx + 0] = bytes[srcIdx + 0]; // R
            imageData.data[dstIdx + 1] = bytes[srcIdx + 1]; // G
            imageData.data[dstIdx + 2] = bytes[srcIdx + 2]; // B
            imageData.data[dstIdx + 3] = 255; // A
        }

        ctx.putImageData(imageData, 0, 0);
    } catch (error) {
        console.error(`Error rendering ${backend}:`, error);
    }
}

async function runBenchmark() {
    const imageSize = parseInt(document.getElementById('image-size').value);
    const blurRadius = parseInt(document.getElementById('blur-radius').value);
    const iterations = parseInt(document.getElementById('iterations').value);

    setStatus('Running benchmark...', true);

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

        setStatus('Benchmark complete', false);
    } catch (error) {
        console.error('Benchmark failed:', error);
        setStatus(`Error: ${error.message}`, false);

        BACKENDS.forEach(backend => showLoading(backend, false));
    }
}

function toggleContinuousMode() {
    const button = document.getElementById('continuous-mode');

    if (continuousMode) {
        // Stop continuous mode
        continuousMode = false;
        clearInterval(continuousInterval);
        button.textContent = 'Start Continuous Mode';
        button.classList.remove('active');
        setStatus('Ready', false);
    } else {
        // Start continuous mode
        continuousMode = true;
        button.textContent = 'Stop Continuous Mode';
        button.classList.add('active');

        // Run immediately
        runBenchmark();

        // Then run every 5 seconds
        continuousInterval = setInterval(runBenchmark, 5000);
    }
}

// Periodically check backend status
setInterval(checkBackendStatus, 30000);
