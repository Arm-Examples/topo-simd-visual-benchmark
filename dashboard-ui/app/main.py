from fastapi import FastAPI, Request
from fastapi.templating import Jinja2Templates
from fastapi.staticfiles import StaticFiles
from fastapi.responses import JSONResponse
import httpx
import os
import asyncio

app = FastAPI(title="SIMD Visual Benchmark Dashboard")

# Mount static files and templates
app.mount("/static", StaticFiles(directory="app/static"), name="static")
templates = Jinja2Templates(directory="app/templates")

# Backend service URLs from environment
SCALAR_URL = os.getenv("SCALAR_API_BASE_URL", "http://processor-scalar:8000")
NEON_URL = os.getenv("NEON_API_BASE_URL", "http://processor-neon:8000")
SVE_URL = os.getenv("SVE_API_BASE_URL", "http://processor-sve:8000")

TIMEOUT = float(os.getenv("PROCESSOR_CLIENT_TIMEOUT_S", "30"))
SVE_ENABLED = os.getenv("ENABLE_SVE", "yes").lower() not in {"0", "false", "no"}

BACKENDS = [
    ("scalar", SCALAR_URL),
    ("neon", NEON_URL),
    *([("sve", SVE_URL)] if SVE_ENABLED else []),
]
BACKEND_NAMES = [name for name, _ in BACKENDS]


@app.get("/")
async def dashboard(request: Request):
    """Render the main dashboard page"""
    context = {
        "request": request,
        "scalar_url": SCALAR_URL,
        "neon_url": NEON_URL,
        "sve_url": SVE_URL if SVE_ENABLED else None,
        "sve_enabled": SVE_ENABLED,
        "backends": BACKEND_NAMES,
    }
    return templates.TemplateResponse("dashboard.html", context)


@app.get("/health")
async def health():
    """Health check endpoint"""
    return {"status": "healthy"}


@app.get("/api/backends/status")
async def backends_status():
    """Check status of all backend services"""
    async def check_backend(name: str, url: str):
        try:
            async with httpx.AsyncClient(timeout=5.0) as client:
                response = await client.get(f"{url}/health")
                build_info = await client.get(f"{url}/build-info")
                return {
                    "name": name,
                    "url": url,
                    "status": "online" if response.status_code == 200 else "offline",
                    "build_info": build_info.json() if build_info.status_code == 200 else None
                }
        except Exception as e:
            return {
                "name": name,
                "url": url,
                "status": "offline",
                "error": str(e)
            }

    results = await asyncio.gather(
        *(check_backend(name, url) for name, url in BACKENDS)
    )

    return {"backends": list(results)}


@app.post("/api/process")
async def process_all(request: Request):
    """
    Process the same workload on all three backends in parallel
    and return comparative results
    """
    body = await request.json()

    width = body.get("width", 512)
    height = body.get("height", 512)
    blur_radius = body.get("blur_radius", 5)
    iterations = body.get("iterations", 10)

    payload = {
        "width": width,
        "height": height,
        "blur_radius": blur_radius,
        "iterations": iterations
    }

    async def process_backend(name: str, url: str):
        try:
            async with httpx.AsyncClient(timeout=TIMEOUT) as client:
                response = await client.post(f"{url}/process", json=payload)
                if response.status_code == 200:
                    data = response.json()
                    data["backend"] = name
                    data["status"] = "success"
                    return data
                else:
                    return {
                        "backend": name,
                        "status": "error",
                        "error": f"HTTP {response.status_code}"
                    }
        except Exception as e:
            return {
                "backend": name,
                "status": "error",
                "error": str(e)
            }

    results = await asyncio.gather(
        *(process_backend(name, url) for name, url in BACKENDS)
    )

    # Calculate speedups relative to scalar
    scalar_time = next((r["process_time_ms"] for r in results if r.get("backend") == "scalar" and r.get("status") == "success"), None)

    if scalar_time:
        for result in results:
            if result.get("status") == "success" and result.get("process_time_ms"):
                result["speedup"] = scalar_time / result["process_time_ms"]
            else:
                result["speedup"] = None

    return {"results": list(results)}


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8080)
