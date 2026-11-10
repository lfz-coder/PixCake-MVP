"""Lazy model construction; importing the protocol never imports a provider SDK."""

from __future__ import annotations

from typing import Any

from ..config import AgentConfig


def create_model(config: AgentConfig) -> Any | None:
    """Create the configured chat model, or ``None`` for fixture/error mode."""

    if config.fixture_mode:
        return None
    if config.model_provider != "deepseek":
        raise ValueError(f"unsupported PIXCAKE_MODEL_PROVIDER: {config.model_provider}")
    try:
        from langchain_deepseek import ChatDeepSeek
    except ImportError as exc:  # pragma: no cover - deployment dependency
        raise RuntimeError("langchain-deepseek is required for the configured model") from exc
    return ChatDeepSeek(model=config.model_name, temperature=0)
