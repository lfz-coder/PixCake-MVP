"""PixCake's optional local natural-language editing sidecar."""

from .protocol import (
    AssistantRequest,
    CardRequest,
    EditPlan,
    Operation,
    ProtocolError,
    SnapshotRef,
)

__all__ = [
    "AssistantRequest",
    "CardRequest",
    "EditPlan",
    "Operation",
    "ProtocolError",
    "SnapshotRef",
]
