from typing import Annotated, Literal

from pydantic import Field

from .models import Identifier, Model, UnsignedInt64

Phone = Annotated[str, Field(pattern=r"^1[3-9][0-9]{9}$", strict=True, max_length=11)]
Password = Annotated[str, Field(min_length=1, max_length=512, strict=True, repr=False)]
Token = Annotated[str, Field(pattern=r"^[A-Za-z0-9_-]{43}$", strict=True, repr=False)]


class AuthConfig(Model):
    api_version: Literal[1] = 1
    sms_enabled: bool
    development: bool
    password_min_length: Literal[15] = 15
    password_max_length: Literal[128] = 128


class CodeRequest(Model):
    phone: Phone
    purpose: Literal["login", "register", "reset"]


class Challenge(Model):
    api_version: Literal[1] = 1
    challenge_id: Annotated[str, Field(pattern=r"^otp_[0-9a-f]{32}$")]
    expires_at: UnsignedInt64
    resend_after: Literal[60] = 60
    development_code: Annotated[str, Field(pattern=r"^(?:[0-9]{6})?$", repr=False)]


class CodeLogin(Model):
    phone: Phone
    challenge_id: Annotated[str, Field(pattern=r"^otp_[0-9a-f]{32}$", strict=True)]
    code: Annotated[str, Field(pattern=r"^[0-9]{6}$", strict=True, repr=False)]


class CodePassword(CodeLogin):
    password: Password


class PasswordLogin(Model):
    phone: Phone
    password: Password


class Refresh(Model):
    refresh_token: Token


class Tokens(Model):
    api_version: Literal[1] = 1
    account_id: Identifier
    access_token: Token
    refresh_token: Token
    session_expires_at: UnsignedInt64
    refresh_expires_at: UnsignedInt64
