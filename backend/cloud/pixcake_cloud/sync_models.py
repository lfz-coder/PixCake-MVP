from typing import Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field, model_validator

CHUNK_BYTES = 1024 * 1024
MAX_BYTES = 8 * 1024 * 1024 * 1024
Key = Annotated[str, Field(pattern=r"^f[0-9]{8}$")]
Digest = Annotated[str, Field(pattern=r"^[0-9a-f]{64}$")]


class Strict(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)


class File(Strict):
    key: Key
    kind: Literal["original", "edit", "layer", "pixel"]
    size: Annotated[int, Field(ge=1, le=MAX_BYTES)]
    chunks: Annotated[list[Digest], Field(min_length=1, max_length=8192)]

    @model_validator(mode="after")
    def sizes(self):
        if len(self.chunks) != (self.size + CHUNK_BYTES - 1) // CHUNK_BYTES:
            raise ValueError("Chunk count")
        if self.kind in ("edit", "layer") and self.size > 16 * CHUNK_BYTES:
            raise ValueError("Document size")
        return self


class Item(Strict):
    edit: Annotated[str, Field(pattern=r"^(f[0-9]{8})?$")]
    layer: Annotated[str, Field(pattern=r"^(f[0-9]{8})?$")]


class Photo(Strict):
    source: Key
    name: Annotated[str, Field(min_length=1, max_length=1024)]
    rating: Annotated[int, Field(ge=0, le=5)]
    flag: Literal["", "选取", "排除"]
    color: Literal["", "红", "黄", "绿", "蓝", "紫"]
    items: Annotated[list[Item], Field(min_length=1, max_length=100)]


class Manifest(Strict):
    format: Literal[1]
    name: Annotated[str, Field(min_length=1, max_length=1024)]
    files: Annotated[list[File], Field(min_length=1, max_length=8000)]
    photos: Annotated[list[Photo], Field(min_length=1, max_length=2000)]

    @model_validator(mode="before")
    @classmethod
    def format_type(cls, value):
        if isinstance(value, dict) and type(value.get("format")) is not int:
            raise ValueError("Integer format required")
        return value

    @model_validator(mode="after")
    def references(self):
        from .store import validate_name
        validate_name(self.name)
        files = {f.key: f for f in self.files}
        if len(files) != len(self.files) or sum(f.size for f in self.files) > MAX_BYTES:
            raise ValueError("Resources")
        if sum(len(p.items) for p in self.photos) > 8000:
            raise ValueError("Item count")
        seen = set()
        for photo in self.photos:
            validate_name(photo.name)
            if "/" in photo.name or "\\" in photo.name or photo.name in (".", ".."):
                raise ValueError("File name")
            for key, kind in [(photo.source, "original")] + [
                    (key, kind) for item in photo.items for key, kind in ((item.edit, "edit"), (item.layer, "layer")) if key]:
                if key in seen or key not in files or files[key].kind != kind:
                    raise ValueError("File reference")
                seen.add(key)
        if any(f.key not in seen and f.kind != "pixel" for f in self.files):
            raise ValueError("Unreferenced file")
        chunk_sizes(self)  # A digest must have one exact length throughout the manifest.
        return self


def chunk_sizes(manifest):
    sizes = {}
    for file in manifest.files:
        for index, digest in enumerate(file.chunks):
            size = min(CHUNK_BYTES, file.size - index * CHUNK_BYTES)
            if digest in sizes and sizes[digest] != size:
                raise ValueError("Chunk length disagreement")
            sizes[digest] = size
    return sizes


class BeginUpload(Strict):
    operation_id: Digest
    space_id: Annotated[str, Field(pattern=r"^sp_[0-9a-f]{32}$")]
    project_id: Annotated[str, Field(pattern=r"^(prj_[0-9a-f]{32})?$")]
    base_revision: Annotated[int, Field(ge=0, le=2**63 - 2)]
    manifest: Manifest

    @model_validator(mode="after")
    def new_revision(self):
        if not self.project_id and self.base_revision != 0:
            raise ValueError("New project revision")
        return self


class Empty(Strict):
    pass


class UploadStarted(Strict):
    api_version: Literal[1]
    upload_id: Annotated[str, Field(pattern=r"^up_[0-9a-f]{32}$")]
    project_id: Annotated[str, Field(pattern=r"^prj_[0-9a-f]{32}$")]
    revision: Annotated[int, Field(ge=0)]
    missing: Annotated[list[Digest], Field(max_length=16384)]


class UploadCommitted(Strict):
    api_version: Literal[1]
    project_id: Annotated[str, Field(pattern=r"^prj_[0-9a-f]{32}$")]
    revision: Annotated[int, Field(ge=1)]


class DownloadManifest(UploadCommitted):
    space_id: Annotated[str, Field(pattern=r"^sp_[0-9a-f]{32}$")]
    manifest: Manifest
