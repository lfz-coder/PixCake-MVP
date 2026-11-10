import os
from dataclasses import dataclass, field
from pathlib import Path


@dataclass(frozen=True)
class Settings:
    database: Path
    cursor_secret: str = field(repr=False)
    development: bool = False
    sms_provider: str = "none"

    def __post_init__(self):
        if self.sms_provider not in ("none", "mock") or (self.sms_provider == "mock" and not self.development):
            raise ValueError("Mock SMS requires explicit development mode.")
        if not 32 <= len(self.cursor_secret) <= 256 or not self.cursor_secret.isascii():
            raise ValueError("A 32–256 byte random cursor signing secret is required.")
        if any(ord(c) < 33 or ord(c) == 127 for c in self.cursor_secret):
            raise ValueError("Invalid cursor signing secret.")

    @classmethod
    def from_environment(cls, database=None):
        database = database or os.environ.get("PIXCAKE_CLOUD_DB")
        secret = os.environ.get("PIXCAKE_CLOUD_CURSOR_SECRET")
        if not database or not secret:
            raise ValueError("PIXCAKE_CLOUD_DB and PIXCAKE_CLOUD_CURSOR_SECRET are required.")
        mode = os.environ.get("PIXCAKE_CLOUD_MODE", "production")
        if mode not in ("production", "development"):
            raise ValueError("Invalid cloud mode.")
        return cls(Path(database).resolve(), secret, mode == "development", os.environ.get("PIXCAKE_SMS_PROVIDER", "none"))
