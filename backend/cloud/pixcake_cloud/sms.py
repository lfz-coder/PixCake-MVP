"""Delivery adapters: OTP issuance/verification belongs to AuthService, not an SDK."""
from typing import Callable, Protocol


class SmsSender(Protocol):
    def send(self, phone: str, code: str, purpose: str, challenge_id: str) -> None:
        """Deliver with a <=3s SDK timeout; raise on failure, never log secrets.

        challenge_id is the provider idempotency key. Adapters map purpose to
        approved templates and translate SDK failures; they never issue tokens.
        """


class DevelopmentSmsSender:
    def send(self, phone: str, code: str, purpose: str, challenge_id: str) -> None:
        # Deliberately no real delivery. Only the development API may return
        # this challenge's code to the development desktop for auto-fill.
        pass


class SdkSmsSender:
    """Inject a configured vendor adapter callable without changing auth logic."""
    def __init__(self, deliver: Callable[[str, str, str, str], None]):
        self.deliver = deliver

    def send(self, phone: str, code: str, purpose: str, challenge_id: str) -> None:
        self.deliver(phone, code, purpose, challenge_id)
