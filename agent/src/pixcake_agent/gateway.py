"""The narrow Python→C++ boundary used by PixCake tools.

Only command-shaped JSON crosses this boundary.  Provider payloads, raw image
bytes, and persistence details stay on their respective sides.
"""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
import hashlib
import json
import socket
import struct
from contextlib import contextmanager
from contextvars import ContextVar
from typing import Any, Iterator, Protocol

from .config import AgentConfig


_REQUEST_CONTEXT: ContextVar[dict[str, Any] | None] = ContextVar("pixcake_request_context", default=None)


@contextmanager
def request_context(request: dict[str, Any]) -> Iterator[None]:
    """Bind the validated user request to gateway calls in one graph run."""

    token = _REQUEST_CONTEXT.set(dict(request))
    try:
        yield
    finally:
        _REQUEST_CONTEXT.reset(token)


def current_request_context() -> dict[str, Any] | None:
    value = _REQUEST_CONTEXT.get()
    return dict(value) if value is not None else None


@dataclass(frozen=True)
class GatewayReply:
    ok: bool
    payload: dict[str, Any]
    code: str = "ok"
    diagnostic: str = ""


class PixCakeGateway(Protocol):
    async def call(self, command: str, payload: dict[str, Any]) -> GatewayReply:
        """Run one validated, provider-neutral PixCake command."""


class LocalUnixPixCakeGateway:
    """JSON command adapter over PixCake's bounded local Unix frames.

    The C++ side owns the socket listener and the edit document. A connection
    carries one request and one reply, so a crashed or stale peer cannot leave
    a pooled stream with an unknown message boundary. Blocking socket work is
    moved off the LangGraph event loop.
    """

    max_frame_bytes = 256 * 1024

    def __init__(self, config: AgentConfig) -> None:
        self.config = config

    async def call(self, command: str, payload: dict[str, Any]) -> GatewayReply:
        try:
            response = await asyncio.to_thread(self._round_trip, command, payload)
        except Exception as exc:  # transport errors become agent-visible failures
            return GatewayReply(False, {}, "gateway_unavailable", str(exc)[:512])
        if not isinstance(response, dict):
            return GatewayReply(False, {}, "invalid_response", "gateway response must be an object")
        return GatewayReply(
            bool(response.get("ok", False)),
            response.get("payload", {}) if isinstance(response.get("payload", {}), dict) else {},
            str(response.get("code", "ok")),
            str(response.get("diagnostic", "")),
        )

    def _round_trip(self, command: str, payload: dict[str, Any]) -> dict[str, Any]:
        if not self.config.gateway_socket:
            raise ValueError("PIXCAKE_GATEWAY_SOCKET is not configured")
        body = json.dumps(
            {
                "protocol_version": 1,
                "command": command,
                "request": current_request_context(),
                "payload": payload,
            },
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")
        if len(body) > self.max_frame_bytes:
            raise ValueError("gateway request is too large")
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as channel:
            channel.settimeout(self.config.request_timeout_seconds)
            channel.connect(self.config.gateway_socket)
            channel.sendall(struct.pack("<I", len(body)) + body)
            header = self._read_exact(channel, 4)
            (size,) = struct.unpack("<I", header)
            if size > self.max_frame_bytes:
                raise ValueError("gateway reply is too large")
            reply = self._read_exact(channel, size)
        decoded = json.loads(reply.decode("utf-8"))
        if not isinstance(decoded, dict):
            raise ValueError("gateway response must be an object")
        return decoded

    @staticmethod
    def _read_exact(channel: socket.socket, size: int) -> bytes:
        chunks: list[bytes] = []
        remaining = size
        while remaining:
            chunk = channel.recv(remaining)
            if not chunk:
                raise ConnectionError("gateway closed the socket before the frame completed")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)


class FixturePixCakeGateway:
    """Deterministic gateway used by tests and no-network development."""

    def __init__(self) -> None:
        self.revision = 0
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self.snapshots: dict[str, int] = {}
        self.tasks: dict[str, dict[str, Any]] = {}
        self.approvals: dict[str, tuple[str, dict[str, Any], GatewayReply]] = {}

    async def call(self, command: str, payload: dict[str, Any]) -> GatewayReply:
        self.calls.append((command, dict(payload)))
        if command == "get_approval_status":
            entry = self.approvals.get(str(payload.get("approval_id", "")))
            return entry[2] if entry else GatewayReply(False, {}, "state_conflict", "approval is unavailable")
        if command in {"commit_edit_plan", "undo_edit", "apply_snapshot", "run_generation"}:
            payload = json.loads(json.dumps({key: value for key, value in payload.items() if key != "user_confirmed"}))
            for approval_id, (saved_command, saved_payload, reply) in self.approvals.items():
                if saved_command == command and saved_payload == payload:
                    return reply
            approval_id = f"fixture-approval-{len(self.approvals) + 1}"
            reply = GatewayReply(False, {"approval_id": approval_id, "approval_status": "pending"},
                                 "confirmation_required", "desktop confirmation is required")
            self.approvals[approval_id] = (command, payload, reply)
            return reply
        return self._execute(command, payload)

    def resolve_approval(self, approval_id: str, approved: bool) -> GatewayReply:
        """Test-host action; deliberately absent from call() and the tool allowlist."""
        command, payload, previous = self.approvals[approval_id]
        if previous.payload["approval_status"] != "pending":
            return previous
        reply = self._execute(command, payload) if approved else GatewayReply(False, {}, "state_conflict", "rejected")
        reply.payload.update(approval_id=approval_id,
                             approval_status="approved" if approved and reply.ok else "failed" if approved else "rejected")
        self.approvals[approval_id] = (command, payload, reply)
        return reply

    def _execute(self, command: str, payload: dict[str, Any]) -> GatewayReply:
        if command == "get_edit_context":
            return GatewayReply(
                True,
                {
                    "revision": self.revision,
                    "approval_history": [
                        {**reply.payload, "status": reply.payload["approval_status"]}
                        for _, _, reply in list(self.approvals.values())[-16:][::-1]
                    ],
                    "capabilities": ["color.global", "portrait.skin"],
                    "capability_descriptors": [
                        {
                            "capability_id": "color.global",
                            "module_label": "color",
                            "label": "global color",
                            "can_use_in_preset": True,
                            "can_batch_sync": True,
                        },
                        {
                            "capability_id": "portrait.skin",
                            "module_label": "portrait",
                            "label": "skin adjustment",
                            "can_use_in_preset": True,
                            "can_batch_sync": True,
                        },
                    ],
                    "parameter_descriptors": [
                        {
                            "capability_id": "color.global",
                            "parameter_id": "exposure",
                            "minimum": -5.0,
                            "maximum": 5.0,
                            "step": 0.05,
                            "default_value": 0.0,
                            "step_confirmed": True,
                            "default_confirmed": True,
                            "choices": [],
                        }
                    ],
                },
            )
        if command == "get_selection_context":
            return GatewayReply(True, {"selection_id": payload.get("selection_id", ""), "kind": "fixture"})
        if command == "preview_edit_plan":
            plan_id = str(payload.get("plan_id", "plan"))
            preview_id = "preview-" + hashlib.sha256(plan_id.encode()).hexdigest()[:16]
            return GatewayReply(True, {"preview_id": preview_id, "revision": self.revision, "diagnostics": []})
        if command == "commit_edit_plan":
            base_revision = payload.get("base_revision")
            if base_revision != self.revision:
                return GatewayReply(False, {}, "state_conflict", "base revision is stale")
            self.revision += 1
            return GatewayReply(True, {"revision": self.revision, "snapshot_id": f"snapshot-{self.revision}"})
        if command == "reject_edit_plan":
            return GatewayReply(True, {"rejected": True})
        if command == "undo_edit":
            self.revision += 1
            return GatewayReply(True, {"revision": self.revision})
        if command == "request_card":
            return GatewayReply(True, {"card_id": payload.get("card_id", "card"), "status": "awaiting_input"})
        if command == "create_snapshot":
            snapshot_id = f"snapshot-{len(self.snapshots) + 1}"
            self.snapshots[snapshot_id] = self.revision
            return GatewayReply(True, {"snapshot_id": snapshot_id, "revision": self.revision})
        if command == "apply_snapshot":
            snapshot_id = str(payload.get("snapshot_id", ""))
            if snapshot_id not in self.snapshots:
                return GatewayReply(False, {}, "state_conflict", "snapshot is unavailable")
            self.revision += 1
            return GatewayReply(True, {"snapshot_id": snapshot_id, "revision": self.revision})
        if command == "list_task_history":
            return GatewayReply(True, {"task_history": list(self.tasks.values())})
        if command == "run_generation":
            task_id = str(payload.get("task_id", ""))
            if not task_id:
                return GatewayReply(False, {}, "state_conflict", "task id is required")
            task = self.tasks.setdefault(
                task_id,
                {
                    "task_id": task_id,
                    "image_id": str(payload.get("image_id", "")),
                    "tool_id": str(payload.get("tool_id", "")),
                    "status": "queued",
                    "source_revision": int(payload.get("source_revision", self.revision)),
                    "result_count": 0,
                },
            )
            return GatewayReply(True, {"task_id": task_id, "task_status": task["status"]})
        if command == "cancel_generation":
            task_id = str(payload.get("task_id", ""))
            for approval_id, (saved_command, saved_payload, reply) in self.approvals.items():
                if (saved_command == "run_generation" and saved_payload.get("task_id") == task_id
                        and reply.payload["approval_status"] in {"pending", "rejected", "expired"}):
                    self.resolve_approval(approval_id, False)
                    return GatewayReply(True, {"task_id": task_id, "task_status": "cancelled"})
            task = self.tasks.get(task_id)
            if task is None:
                return GatewayReply(False, {}, "state_conflict", "task is unavailable")
            if task["status"] in {"queued", "running", "cancelling"}:
                task["status"] = "cancelled"
            return GatewayReply(True, {"task_id": task_id, "task_status": task["status"]})
        return GatewayReply(False, {}, "unsupported", f"fixture command is not supported: {command}")


class LocalHttpPixCakeGateway:
    """HTTP adapter for the local C++ gateway; `httpx` is imported lazily."""

    def __init__(self, config: AgentConfig) -> None:
        self.config = config
        self._client: Any | None = None

    async def close(self) -> None:
        """Close the pooled local transport when the sidecar shuts down."""
        if self._client is not None:
            await self._client.aclose()
            self._client = None

    async def call(self, command: str, payload: dict[str, Any]) -> GatewayReply:
        try:
            import httpx
        except ImportError as exc:  # pragma: no cover - exercised in deployment
            return GatewayReply(False, {}, "dependency_missing", f"httpx is required: {exc}")

        body = {
            "protocol_version": 1,
            "command": command,
            "request": current_request_context(),
            "payload": payload,
        }
        headers = {"Content-Type": "application/json"}
        if self.config.gateway_token:
            headers["Authorization"] = f"Bearer {self.config.gateway_token}"
        try:
            if self._client is None:
                self._client = httpx.AsyncClient(timeout=self.config.request_timeout_seconds)
            response = await self._client.post(
                f"{self.config.gateway_url}/v1/assistant/commands", json=body, headers=headers
            )
            if response.status_code < 200 or response.status_code >= 300:
                return GatewayReply(False, {}, f"http_{response.status_code}", response.text[:512])
            data = response.json()
            if not isinstance(data, dict):
                return GatewayReply(False, {}, "invalid_response", "gateway response must be an object")
            return GatewayReply(
                bool(data.get("ok", False)),
                data.get("payload", {}) if isinstance(data.get("payload", {}), dict) else {},
                str(data.get("code", "ok")),
                str(data.get("diagnostic", "")),
            )
        except Exception as exc:  # network errors are returned to the agent loop
            return GatewayReply(False, {}, "gateway_unavailable", str(exc)[:512])


def gateway_from_config(config: AgentConfig) -> PixCakeGateway:
    if config.fixture_mode:
        return FixturePixCakeGateway()
    if config.gateway_socket:
        return LocalUnixPixCakeGateway(config)
    return LocalHttpPixCakeGateway(config)
