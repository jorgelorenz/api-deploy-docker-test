import json
import subprocess

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field

app = FastAPI(title="Monte Carlo Simulation API")

NATIVE_BINARY = "/usr/local/bin/simulator"

MAX_SIMULATIONS = 10_000_000


class SimulationRequest(BaseModel):
    S0: float = Field(..., gt=0, description="Initial asset price")
    dates: list[float] = Field(
        ..., min_length=1, description="Observation times in years"
    )
    volatility: float = Field(..., ge=0, description="Annualized volatility")
    r: float = Field(..., description="Risk-free interest rate")
    q: float = Field(..., description="Continuous dividend yield")
    N: int = Field(..., ge=1, le=MAX_SIMULATIONS, description="Number of simulations")


def _run_native(*args: str, stdin_data: str | None = None) -> str:
    """Run the native binary and return its stdout."""
    try:
        result = subprocess.run(
            [NATIVE_BINARY, *args],
            input=stdin_data,
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as exc:
        raise HTTPException(
            status_code=500,
            detail=f"Native binary failed: {exc.stderr.strip()}",
        )
    except FileNotFoundError:
        raise HTTPException(status_code=500, detail="Native binary not found")
    return result.stdout


@app.get("/health")
def health():
    raw = _run_native("--health")
    return json.loads(raw)


@app.post("/simulations")
def simulations(req: SimulationRequest):
    payload = req.model_dump_json()
    raw = _run_native(stdin_data=payload)
    return json.loads(raw)
