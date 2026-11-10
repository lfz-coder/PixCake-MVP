import secrets
import threading
import unicodedata
from contextlib import contextmanager

from argon2 import PasswordHasher, extract_parameters
from argon2.exceptions import InvalidHashError, VerificationError
from argon2.profiles import RFC_9106_LOW_MEMORY


class PasswordPolicyError(ValueError):
    pass


class PasswordBusy(RuntimeError):
    pass


class Passwords:
    def __init__(self):
        self.hasher = PasswordHasher.from_parameters(RFC_9106_LOW_MEMORY)
        self.slots = threading.BoundedSemaphore(2)
        self.dummy = self.hasher.hash(secrets.token_urlsafe(32))

    @staticmethod
    def normalize(value: str) -> str:
        normalized = unicodedata.normalize("NFC", value)
        # A baseline local blocklist, not a claim of comprehensive breach checks.
        common = {"passwordpassword", "password123456789", "123456789012345", "1234567890123456",
                  "qwertyuiopasdfgh", "letmeinletmeinletmein", "111111111111111", "abcdefghijklmnop"}
        if (not 15 <= len(normalized) <= 128 or len(normalized.encode("utf-8")) > 512
                or any(unicodedata.category(c).startswith("C") for c in normalized)
                or normalized.casefold() in common or len(set(normalized)) < 4):
            raise PasswordPolicyError()
        return normalized

    @contextmanager
    def capacity(self):
        if not self.slots.acquire(blocking=False):
            raise PasswordBusy()
        try:
            yield
        finally:
            self.slots.release()

    def hash(self, value: str) -> str:
        value = self.normalize(value)
        with self.capacity():
            return self.hasher.hash(value)

    def verify(self, encoded: str | None, value: str) -> tuple[bool, str | None]:
        value = self.normalize(value)
        with self.capacity():
            try:
                parameters = extract_parameters(encoded or self.dummy)
                if (len(encoded or self.dummy) > 512 or parameters.memory_cost > 65536 or parameters.time_cost > 3
                        or parameters.parallelism > 4 or parameters.hash_len > 64 or parameters.salt_len > 64):
                    return False, None
                self.hasher.verify(encoded or self.dummy, value)
            except (VerificationError, InvalidHashError):
                return False, None
            if encoded is None:
                return False, None
            upgraded = self.hasher.hash(value) if self.hasher.check_needs_rehash(encoded) else None
            return True, upgraded
