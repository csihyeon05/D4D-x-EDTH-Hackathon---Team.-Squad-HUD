"""Local LLM client: one call, JSON object out.

Default backend is Ollama's native /api/generate with the `operator` model
(kanana-2-3b-instruct weights, Modelfile TEMPLATE `{{ .Prompt }}`). Because the
template is raw, we build the ChatML prompt here and close the reasoning block
empty so the model answers directly. `format: "json"` makes Ollama constrain
decoding to valid JSON.

C2_LLM_API=openai sends the same messages to an OpenAI-compatible
/chat/completions endpoint instead.
"""
from __future__ import annotations

import json

import httpx

from . import config

IM_END = "<|im_end|>"


def chatml(system: str, user: str) -> str:
    return (f"<|im_start|>system\n{system}{IM_END}\n"
            f"<|im_start|>user\n{user}{IM_END}\n"
            "<|im_start|>assistant\n<think>\n\n</think>\n\n")


def parse_json_object(text: str) -> dict:
    """First JSON object in `text` (models sometimes wrap it in prose or code fences)."""
    text = text.strip()
    try:
        obj = json.loads(text)
    except json.JSONDecodeError:
        start, end = text.find("{"), text.rfind("}")
        if start < 0 or end <= start:
            raise ValueError("no JSON object in model output")
        obj = json.loads(text[start:end + 1])
    if not isinstance(obj, dict):
        raise ValueError("model output is not a JSON object")
    return obj


async def generate_json(system: str, user: str, *, max_tokens: int = 320,
                        temperature: float = 0.1, timeout_s: float | None = None) -> dict:
    """Return the parsed JSON object. Raises httpx errors or ValueError."""
    timeout = timeout_s or config.LLM_TIMEOUT_S
    async with httpx.AsyncClient(timeout=timeout) as client:
        if config.LLM_API == "openai":
            r = await client.post(f"{config.LLM_BASE_URL}/chat/completions", json={
                "model": config.LLM_MODEL, "temperature": temperature, "max_tokens": max_tokens,
                "response_format": {"type": "json_object"},
                "messages": [{"role": "system", "content": system}, {"role": "user", "content": user}],
            })
            r.raise_for_status()
            text = r.json()["choices"][0]["message"]["content"]
        else:
            r = await client.post(f"{config.LLM_BASE_URL}/api/generate", json={
                "model": config.LLM_MODEL, "prompt": chatml(system, user), "raw": True,
                "stream": False, "format": "json", "keep_alive": "30m",
                "options": {"temperature": temperature, "num_predict": max_tokens, "stop": [IM_END]},
            })
            r.raise_for_status()
            text = r.json()["response"]
    return parse_json_object(text)
