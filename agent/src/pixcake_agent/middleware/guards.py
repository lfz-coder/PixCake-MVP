"""Small deterministic guards used before invoking PixCake tools."""

from __future__ import annotations

from dataclasses import dataclass

from ..tools.definitions import TOOL_BY_NAME


def validate_tool_name(name: str) -> None:
    definition = TOOL_BY_NAME.get(name)
    if definition is None:
        raise ValueError(f"tool is not in the PixCake allowlist: {name}")
    if definition.risk == "blocked":
        raise PermissionError(f"tool is blocked: {name}")


@dataclass
class LoopBudget:
    maximum: int = 12
    steps: int = 0

    def take(self) -> None:
        self.steps += 1
        if self.steps > self.maximum:
            raise RuntimeError("assistant step budget exhausted")
