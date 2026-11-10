"""State shared by the PixCake assistant graph and its middleware."""

from typing import Any, NotRequired, TypedDict


class PixCakeAgentState(TypedDict, total=False):
    messages: list[Any]
    request: dict[str, Any]
    image_id: str
    editing_instance_id: str
    base_revision: int
    pending_plan: dict[str, Any]
    pending_card: dict[str, Any]
    preview_id: str
    active_snapshot_id: str
    pending_approval: bool
    approval_id: str
    step_count: int
    trace_records: list[dict[str, Any]]
    summary: str
    error: dict[str, Any]


__all__ = ["PixCakeAgentState"]
