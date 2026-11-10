"""Sidecar configuration with no project-specific secrets in source code."""

from __future__ import annotations

from dataclasses import dataclass
import os


@dataclass(frozen=True)
class AgentConfig:
    gateway_url: str = "http://127.0.0.1:27461"
    gateway_socket: str = ""
    gateway_token: str = ""
    model_provider: str = "deepseek"
    model_name: str = "deepseek-chat"
    fixture_mode: bool = False
    max_steps: int = 12
    request_timeout_seconds: float = 30.0


def load_config(environ: dict[str, str] | None = None) -> AgentConfig:
    env = os.environ if environ is None else environ
    return AgentConfig(
        gateway_url=env.get("PIXCAKE_GATEWAY_URL", AgentConfig.gateway_url).rstrip("/"),
        gateway_socket=env.get("PIXCAKE_GATEWAY_SOCKET", ""),
        gateway_token=env.get("PIXCAKE_GATEWAY_TOKEN", ""),
        model_provider=env.get("PIXCAKE_MODEL_PROVIDER", "deepseek"),
        model_name=env.get("PIXCAKE_MODEL_NAME", "deepseek-chat"),
        fixture_mode=env.get("PIXCAKE_AGENT_FIXTURE", "0").lower() in {"1", "true", "yes"},
        max_steps=max(1, min(int(env.get("PIXCAKE_AGENT_MAX_STEPS", "12")), 64)),
        request_timeout_seconds=max(0.5, min(float(env.get("PIXCAKE_GATEWAY_TIMEOUT", "30")), 300.0)),
    )
