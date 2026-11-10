from dataclasses import dataclass
from enum import Enum
from typing import Any


class Code(str, Enum):
    INVALID_REQUEST = "invalid_request"
    INVALID_CURSOR = "invalid_cursor"
    UNAUTHORIZED = "unauthorized"
    FORBIDDEN = "forbidden"
    DIRECTORY_CHANGED = "directory_changed"
    CURSOR_EXPIRED = "cursor_expired"
    REQUEST_TOO_LARGE = "request_too_large"
    NOT_FOUND = "not_found"
    METHOD_NOT_ALLOWED = "method_not_allowed"
    STORAGE_UNAVAILABLE = "storage_unavailable"
    INTERNAL_ERROR = "internal_error"
    SMS_UNAVAILABLE = "sms_unavailable"
    CODE_INVALID = "code_invalid"
    CODE_EXPIRED = "code_expired"
    RATE_LIMITED = "rate_limited"
    CREDENTIALS_INVALID = "credentials_invalid"
    ACCOUNT_EXISTS = "account_exists"
    PASSWORD_WEAK = "password_weak"
    REVISION_CONFLICT = "revision_conflict"
    INCOMPLETE_UPLOAD = "incomplete_upload"
    TRANSFER_EXPIRED = "transfer_expired"
    QUOTA_EXCEEDED = "quota_exceeded"


ERRORS = {
    Code.INVALID_REQUEST: (400, "Invalid request."),
    Code.INVALID_CURSOR: (400, "Invalid directory cursor."),
    Code.UNAUTHORIZED: (401, "Authentication required."),
    Code.FORBIDDEN: (403, "Space access denied."),
    Code.DIRECTORY_CHANGED: (409, "Directory changed; restart from the first page."),
    Code.CURSOR_EXPIRED: (410, "Cursor expired; restart from the first page."),
    Code.REQUEST_TOO_LARGE: (413, "Request exceeds the allowed size."),
    Code.NOT_FOUND: (404, "Endpoint not found."),
    Code.METHOD_NOT_ALLOWED: (405, "Method not allowed."),
    Code.STORAGE_UNAVAILABLE: (503, "Storage temporarily unavailable."),
    Code.INTERNAL_ERROR: (500, "Internal service error."),
    Code.SMS_UNAVAILABLE: (503, "SMS delivery unavailable."),
    Code.CODE_INVALID: (400, "Verification code invalid or already used."),
    Code.CODE_EXPIRED: (400, "Verification code expired."),
    Code.RATE_LIMITED: (429, "Too many requests; try again later."),
    Code.CREDENTIALS_INVALID: (401, "Invalid credentials."),
    Code.ACCOUNT_EXISTS: (409, "Account already registered; sign in instead."),
    Code.PASSWORD_WEAK: (400, "Password does not meet the policy."),
    Code.REVISION_CONFLICT: (409, "Project revision changed; keep both or download the current revision."),
    Code.INCOMPLETE_UPLOAD: (409, "Upload has missing or corrupt chunks."),
    Code.TRANSFER_EXPIRED: (410, "Transfer expired; start again."),
    Code.QUOTA_EXCEEDED: (413, "Storage resource limit reached."),
}


@dataclass(frozen=True)
class Result:
    # Business failures cross the service/API boundary as explicit codes.
    value: Any = None
    error: Code | None = None
