"""LangChain tool adapters for the PixCake local gateway."""

from __future__ import annotations

import json
from typing import Any

try:  # Keep protocol/fixture tests importable without LangChain installed.
    from langchain_core.tools import tool as _tool
except ImportError:  # pragma: no cover - dependency is installed by the sidecar package
    def _tool(function):  # type: ignore[no-untyped-def]
        return function

from ..gateway import PixCakeGateway
from ..protocol import EditPlan, ProtocolError, to_json


def build_tools(gateway: PixCakeGateway) -> list[Any]:
    """Build a fresh tool set bound to one C++ gateway connection."""

    @_tool
    async def get_edit_context(image_id: str, editing_instance_id: str = "") -> dict[str, Any]:
        """Read the current PixCake edit context without changing it."""
        reply = await gateway.call(
            "get_edit_context", {"image_id": image_id, "editing_instance_id": editing_instance_id}
        )
        return _reply(reply)

    @_tool
    async def get_selection_context(selection_id: str) -> dict[str, Any]:
        """Read the selected image/person/semantic target without changing it."""
        return _reply(await gateway.call("get_selection_context", {"selection_id": selection_id}))

    @_tool
    async def create_task_plan(summary: str, steps: list[dict[str, Any]]) -> dict[str, Any]:
        """Create a visible task checklist; this tool does not execute edits."""
        if not summary or not steps or len(steps) > 64:
            return {"ok": False, "code": "invalid_plan", "diagnostic": "summary and 1..64 steps are required"}
        return {"ok": True, "task_plan": {"summary": summary, "steps": steps}}

    @_tool
    async def build_edit_plan(plan: dict[str, Any]) -> dict[str, Any]:
        """Validate and normalize a versioned plan without changing PixCake state."""
        try:
            normalized = EditPlan.from_dict(plan)
        except (ProtocolError, TypeError, ValueError) as exc:
            return {"ok": False, "code": "invalid_plan", "diagnostic": str(exc)}
        return {"ok": True, "plan": json.loads(to_json(normalized))}

    @_tool
    async def preview_edit_plan(plan: dict[str, Any]) -> dict[str, Any]:
        """Ask C++ for a preview of a validated edit plan."""
        return _reply(await gateway.call("preview_edit_plan", plan))

    @_tool
    async def request_structured_card(card_type: str, fields: list[dict[str, Any]]) -> dict[str, Any]:
        """Ask the PixCake UI for a structured input or confirmation card."""
        return _reply(await gateway.call("request_card", {"card_type": card_type, "fields": fields}))

    @_tool
    async def commit_edit_plan(plan: dict[str, Any]) -> dict[str, Any]:
        """Request desktop confirmation for a previewed plan. A pending card has not executed."""
        return _reply(await gateway.call("commit_edit_plan", plan))

    @_tool
    async def reject_edit_plan(plan_id: str) -> dict[str, Any]:
        """Reject a pending plan without changing the formal edit document."""
        return _reply(await gateway.call("reject_edit_plan", {"plan_id": plan_id}))

    @_tool
    async def undo_edit(command_id: str) -> dict[str, Any]:
        """Ask the desktop user to confirm undoing the current assistant edit."""
        return _reply(await gateway.call("undo_edit", {"command_id": command_id}))

    @_tool
    async def run_generation(task: dict[str, Any]) -> dict[str, Any]:
        """Request a desktop cost-confirmation card. Pending approval is not a queued task."""
        return _reply(await gateway.call("run_generation", task))

    @_tool
    async def cancel_generation(task_id: str) -> dict[str, Any]:
        """Request cancellation; completed work and ledger charges follow C++ task policy."""
        return _reply(await gateway.call("cancel_generation", {"task_id": task_id}))

    @_tool
    async def create_snapshot(image_id: str, label: str = "") -> dict[str, Any]:
        """Save the current assistant result as a local effect snapshot."""
        return _reply(await gateway.call("create_snapshot", {"image_id": image_id, "label": label}))

    @_tool
    async def apply_snapshot(snapshot_id: str, image_id: str) -> dict[str, Any]:
        """Request desktop confirmation before restoring a snapshot."""
        return _reply(
            await gateway.call(
                "apply_snapshot", {"snapshot_id": snapshot_id, "image_id": image_id}
            )
        )

    @_tool
    async def get_approval_status(approval_id: str) -> dict[str, Any]:
        """Read a desktop decision/result. This tool cannot approve or execute an action."""
        return _reply(await gateway.call("get_approval_status", {"approval_id": approval_id}))

    @_tool
    async def list_task_history() -> dict[str, Any]:
        """Read durable local generation task history without starting work."""
        return _reply(await gateway.call("list_task_history", {}))

    return [
        get_edit_context,
        get_selection_context,
        create_task_plan,
        build_edit_plan,
        preview_edit_plan,
        request_structured_card,
        commit_edit_plan,
        reject_edit_plan,
        undo_edit,
        run_generation,
        cancel_generation,
        create_snapshot,
        apply_snapshot,
        list_task_history,
        get_approval_status,
    ]


def _reply(reply: Any) -> dict[str, Any]:
    return {
        "ok": bool(reply.ok),
        "code": "confirmation_required" if reply.payload.get("approval_status") == "pending" else reply.code,
        "payload": reply.payload,
        "diagnostic": reply.diagnostic,
    }
