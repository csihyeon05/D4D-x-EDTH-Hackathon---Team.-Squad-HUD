"""Runtime configuration (environment variables)."""
import os


def _env(name: str, default: str) -> str:
    return os.environ.get(name, default)


# Data source: "sim" (built-in simulator) or "serial" (gateway ESP32 over USB)
SOURCE = _env("C2_SOURCE", "sim")
SERIAL_PORT = _env("C2_SERIAL_PORT", "/dev/ttyUSB0")  # Windows: COM3
SERIAL_BAUD = int(_env("C2_SERIAL_BAUD", "115200"))

# Local LLM. Default: Ollama native API with the `operator` model (kanana-2-3b-instruct,
# raw ChatML prompt). C2_LLM_API=openai uses any OpenAI-compatible server instead
# (base URL then ends in /v1, e.g. llama.cpp server, LM Studio).
LLM_API = _env("C2_LLM_API", "ollama")
LLM_BASE_URL = _env("C2_LLM_BASE_URL", "http://127.0.0.1:11434" if LLM_API == "ollama" else "http://127.0.0.1:8080/v1")
LLM_MODEL = _env("C2_LLM_MODEL", "operator")
LLM_TIMEOUT_S = float(_env("C2_LLM_TIMEOUT_S", "45"))
LLM_ENABLED = _env("C2_LLM_ENABLED", "1") == "1"

# /example page: persona-driven scenario replay (see app/example.py)
EXAMPLE_ORIGIN = _env("C2_EXAMPLE_ORIGIN", "")  # "lat,lon" of the AO centre; empty = live phone fix or default

# Simulator
SIM_SQUAD_SIZE = int(_env("C2_SIM_SQUAD_SIZE", "8"))
SIM_SPEED = float(_env("C2_SIM_SPEED", "1.0"))  # 1.0 = real time
SIM_SEED = int(_env("C2_SIM_SEED", "7"))

# Area of operations: square grid, columns A.., rows 1..
AO_SIZE_M = 300.0
GRID_N = 3  # 3x3 sectors (A1..C3), each 100 m

# Legacy simulation cadence; hardware now publishes live state once per second.
CHECKIN_PERIOD_S = float(_env("C2_CHECKIN_PERIOD_S", "120"))
CHECKIN_JITTER_S = float(_env("C2_CHECKIN_JITTER_S", "30"))
# Serial (live radio): a soldier with no packet for more than 10 s is shown as lost (UNKNOWN).
STALE_AFTER_S = float(_env("C2_STALE_AFTER_S",
                           "10" if SOURCE == "serial" else str(CHECKIN_PERIOD_S * 2 + CHECKIN_JITTER_S)))

# Squad correlation window
CORRELATION_WINDOW_S = 30.0
