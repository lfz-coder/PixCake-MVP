"""Versioned, provider-neutral protocol objects for the PixCake sidecar.

The sidecar exchanges references and plans, never raw source pixels.  This
module intentionally uses only the Python standard library so protocol tests
can run without a model, LangChain, or network access.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
import json
import re
from typing import Any, Mapping

PROTOCOL_VERSION = 1
_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,255}$")
_MAX_TEXT = 16 * 1024
_MAX_OPERATIONS = 64
_MAX_PARAMETERS = 32 * 1024


class ProtocolError(ValueError):
    """Raised when an untrusted sidecar message violates the protocol."""


def _text(value: Any, name: str, *, required: bool = True, limit: int = _MAX_TEXT) -> str:
    if not isinstance(value, str):
        if not required and value is None:
            return ""
        raise ProtocolError(f"{name} must be a string")
    if required and not value:
        raise ProtocolError(f"{name} must not be empty")
    if len(value) > limit or "\x00" in value:
        raise ProtocolError(f"{name} is too long or contains NUL")
    return value


def _id(value: Any, name: str, *, required: bool = True) -> str:
    result = _text(value, name, required=required, limit=256)
    if result and not _ID_RE.fullmatch(result):
        raise ProtocolError(f"{name} has an invalid identifier")
    return result


def _mapping(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, Mapping):
        raise ProtocolError(f"{name} must be an object")
    result = dict(value)
    encoded = json.dumps(result, ensure_ascii=False, separators=(",", ":"))
    if len(encoded.encode("utf-8")) > _MAX_PARAMETERS:
        raise ProtocolError(f"{name} is too large")
    return result


@dataclass(frozen=True)
class ImageContext:
    image_id: str
    project_id: str = ""
    source_revision: int = 0
    editing_instance_id: str = ""
    preview_ref: str = ""

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> "ImageContext":
        return cls(
            image_id=_id(value.get("image_id"), "image_id"),
            project_id=_id(value.get("project_id", ""), "project_id", required=False),
            source_revision=_revision(value.get("source_revision", 0), "source_revision"),
            editing_instance_id=_id(value.get("editing_instance_id", ""), "editing_instance_id", required=False),
            preview_ref=_text(value.get("preview_ref", ""), "preview_ref", required=False, limit=4096),
        )


@dataclass(frozen=True)
class SelectionContext:
    selection_id: str
    kind: str = "unknown"
    label: str = ""

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> "SelectionContext":
        return cls(
            selection_id=_id(value.get("selection_id"), "selection_id"),
            kind=_text(value.get("kind", "unknown"), "selection.kind", limit=128),
            label=_text(value.get("label", ""), "selection.label", required=False, limit=512),
        )


@dataclass(frozen=True)
class AssistantRequest:
    request_id: str
    thread_id: str
    message: str
    image: ImageContext | None = None
    selection: SelectionContext | None = None
    base_revision: int = 0
    mode: str = "assistant"
    protocol_version: int = PROTOCOL_VERSION
    preferences: Mapping[str, Any] = field(default_factory=dict)

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> "AssistantRequest":
        if not isinstance(value, Mapping):
            raise ProtocolError("assistant request must be an object")
        version = _protocol_version(value.get("protocol_version", PROTOCOL_VERSION))
        image_value = value.get("image")
        selection_value = value.get("selection")
        if image_value is not None and not isinstance(image_value, Mapping):
            raise ProtocolError("image must be an object or null")
        if selection_value is not None and not isinstance(selection_value, Mapping):
            raise ProtocolError("selection must be an object or null")
        return cls(
            request_id=_id(value.get("request_id"), "request_id"),
            thread_id=_id(value.get("thread_id"), "thread_id"),
            message=_text(value.get("message"), "message", limit=_MAX_TEXT),
            image=ImageContext.from_dict(image_value) if isinstance(image_value, Mapping) else None,
            selection=SelectionContext.from_dict(selection_value) if isinstance(selection_value, Mapping) else None,
            base_revision=_revision(value.get("base_revision", 0), "base_revision"),
            mode=_text(value.get("mode", "assistant"), "mode", limit=64),
            protocol_version=version,
            preferences=_mapping(value.get("preferences", {}), "preferences"),
        )


@dataclass(frozen=True)
class Operation:
    operation_id: str
    tool_id: str
    parameters: Mapping[str, Any] = field(default_factory=dict)
    target_selection_id: str = ""
    requires_confirmation: bool = False
    reversible: bool = True

    @classmethod
    def from_dict(cls, value: Mapping[str, Any], index: int = 0) -> "Operation":
        if not isinstance(value, Mapping):
            raise ProtocolError(f"operations[{index}] must be an object")
        return cls(
            operation_id=_id(value.get("operation_id"), f"operations[{index}].operation_id"),
            tool_id=_id(value.get("tool_id"), f"operations[{index}].tool_id"),
            parameters=_mapping(value.get("parameters", {}), f"operations[{index}].parameters"),
            target_selection_id=_id(
                value.get("target_selection_id", ""),
                f"operations[{index}].target_selection_id",
                required=False,
            ),
            requires_confirmation=bool(value.get("requires_confirmation", False)),
            reversible=bool(value.get("reversible", True)),
        )


@dataclass(frozen=True)
class EditPlan:
    plan_id: str
    request_id: str
    base_revision: int
    operations: tuple[Operation, ...]
    summary: str
    requires_confirmation: bool = True
    estimated_points: int = 0
    protocol_version: int = PROTOCOL_VERSION

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> "EditPlan":
        if not isinstance(value, Mapping):
            raise ProtocolError("edit plan must be an object")
        raw_operations = value.get("operations", [])
        if not isinstance(raw_operations, list) or not raw_operations or len(raw_operations) > _MAX_OPERATIONS:
            raise ProtocolError("edit plan must contain 1..64 operations")
        estimated_points = value.get("estimated_points", 0)
        if type(estimated_points) is not int or estimated_points < 0 or estimated_points > 10**9:
            raise ProtocolError("estimated_points is invalid")
        protocol_version = _protocol_version(value.get("protocol_version", PROTOCOL_VERSION))
        return cls(
            plan_id=_id(value.get("plan_id"), "plan_id"),
            request_id=_id(value.get("request_id"), "request_id"),
            base_revision=_revision(value.get("base_revision", 0), "base_revision"),
            operations=tuple(Operation.from_dict(item, index) for index, item in enumerate(raw_operations)),
            summary=_text(value.get("summary"), "summary", limit=4096),
            requires_confirmation=bool(value.get("requires_confirmation", True)),
            estimated_points=estimated_points,
            protocol_version=protocol_version,
        )


@dataclass(frozen=True)
class CardRequest:
    card_id: str
    card_type: str
    fields: tuple[Mapping[str, Any], ...] = ()
    confirmation_required: bool = True

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> "CardRequest":
        raw_fields = value.get("fields", [])
        if not isinstance(raw_fields, list) or len(raw_fields) > 64:
            raise ProtocolError("card fields are invalid")
        return cls(
            card_id=_id(value.get("card_id"), "card_id"),
            card_type=_text(value.get("card_type"), "card_type", limit=128),
            fields=tuple(_mapping(item, "card.field") for item in raw_fields),
            confirmation_required=bool(value.get("confirmation_required", True)),
        )


@dataclass(frozen=True)
class SnapshotRef:
    snapshot_id: str
    image_id: str
    revision: int
    preview_ref: str = ""
    label: str = ""

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> "SnapshotRef":
        return cls(
            snapshot_id=_id(value.get("snapshot_id"), "snapshot_id"),
            image_id=_id(value.get("image_id"), "image_id"),
            revision=_revision(value.get("revision", 0), "revision"),
            preview_ref=_text(value.get("preview_ref", ""), "preview_ref", required=False),
            label=_text(value.get("label", ""), "label", required=False, limit=512),
        )


def _revision(value: Any, name: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0 or value > 2**63 - 1:
        raise ProtocolError(f"{name} must be a non-negative integer")
    return value


def _protocol_version(value: Any) -> int:
    if type(value) is not int or value != PROTOCOL_VERSION:
        raise ProtocolError(f"unsupported protocol_version: {value}")
    return value


def to_json(value: Any) -> str:
    """Serialize a protocol dataclass without exposing implementation types."""

    return json.dumps(asdict(value), ensure_ascii=False, separators=(",", ":"))
