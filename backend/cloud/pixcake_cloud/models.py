from typing import Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field

from .results import Code

Identifier = Annotated[str, Field(pattern=r"^[a-z]+_[0-9a-f]{32}$", max_length=64)]
Permission = Literal["read", "write", "manage"]
UnsignedInt64 = Annotated[int, Field(ge=0, le=9223372036854775807, strict=True)]


class Model(BaseModel):
    model_config = ConfigDict(extra="forbid")


class Space(Model):
    id: Identifier
    name: Annotated[str, Field(min_length=1, max_length=1024)]
    kind: Literal["personal", "team"]
    permissions: list[Permission] = Field(max_length=3)


class Project(Model):
    id: Identifier
    space_id: Identifier
    name: Annotated[str, Field(min_length=1, max_length=1024)]
    revision: UnsignedInt64
    updated_at: UnsignedInt64
    size_bytes: UnsignedInt64


class SpacesPage(Model):
    api_version: Literal[1] = 1
    items: list[Space] = Field(max_length=100)
    next_cursor: str = Field(max_length=4096)


class ProjectsPage(Model):
    api_version: Literal[1] = 1
    items: list[Project] = Field(max_length=100)
    next_cursor: str = Field(max_length=4096)


class Account(Model):
    api_version: Literal[1] = 1
    account_id: Identifier
    session_expires_at: UnsignedInt64


class Health(Model):
    api_version: Literal[1] = 1
    status: Literal["ok"] = "ok"


class ErrorDetail(Model):
    code: Code
    message: str


class ErrorResponse(Model):
    api_version: Literal[1] = 1
    error: ErrorDetail
