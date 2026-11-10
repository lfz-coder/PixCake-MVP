import time
from typing import Annotated, Callable

from fastapi import Depends, FastAPI, Query, Request, Response, Security
from fastapi.exceptions import RequestValidationError
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from starlette.exceptions import HTTPException

from .boundary import RequestBoundary, failure
from .config import Settings
from .cursors import CursorCodec
from .models import Account, ErrorResponse, Health, ProjectsPage, SpacesPage
from .results import Code
from .service import DirectoryService
from .store import Store
from .auth import AuthService
from .auth_models import AuthConfig, Challenge, CodeLogin, CodePassword, CodeRequest, PasswordLogin, Refresh, Tokens
from .sms import DevelopmentSmsSender, SmsSender
from .sync_models import BeginUpload, DownloadManifest, Empty, UploadCommitted, UploadStarted
from .sync_service import SyncService


def create_environment_app():
    return create_app(Settings.from_environment())


def create_app(settings: Settings, *, clock: Callable = time.time, sms_sender: SmsSender | None = None) -> FastAPI:
    store = Store(settings.database, clock=clock)
    store.check_ready()
    service = DirectoryService(store, CursorCodec(settings.cursor_secret, clock))
    sender = sms_sender if sms_sender is not None else DevelopmentSmsSender() if settings.sms_provider == "mock" else None
    auth_service = AuthService(store, settings, sender)
    sync_service = SyncService(store)
    app = FastAPI(title="PixCake Cloud Directory", version="1.0.0", docs_url=None,
                  redoc_url=None, openapi_url=None, redirect_slashes=False)
    app.add_middleware(RequestBoundary)
    bearer = HTTPBearer(auto_error=False)

    def token(credentials: Annotated[HTTPAuthorizationCredentials | None, Security(bearer)]) -> str:
        return credentials.credentials if credentials and credentials.scheme.lower() == "bearer" else ""

    Token = Annotated[str, Depends(token)]
    Limit = Annotated[int, Query(ge=1, le=100)]
    Cursor = Annotated[str, Query(max_length=4096)]
    errors = {status: {"model": ErrorResponse} for status in (400, 401, 403, 409, 410, 413, 429, 431, 500, 503)}

    def result(value):
        return failure(value.error) if value.error else value.value

    def source(request):
        # Never trust client-supplied forwarding headers for rate-limit identity.
        return request.client.host if request.client else "unknown"

    @app.get("/v1/auth/config", response_model=AuthConfig, responses=errors, operation_id="authConfiguration")
    def auth_config(request: Request):
        if not valid_query(request, set()):
            return failure(Code.INVALID_REQUEST)
        return auth_service.configuration()

    @app.post("/v1/auth/code", response_model=Challenge, responses=errors, operation_id="requestCode")
    def request_code(body: CodeRequest, request: Request):
        return result(auth_service.send_code(body.phone, body.purpose, source(request)))

    @app.post("/v1/auth/register", response_model=Tokens, responses=errors, operation_id="registerAccount")
    def register(body: CodePassword, request: Request):
        return result(auth_service.code_auth(body, "register", source(request)))

    @app.post("/v1/auth/sms/login", response_model=Tokens, responses=errors, operation_id="loginWithCode")
    def sms_login(body: CodeLogin, request: Request):
        return result(auth_service.code_auth(body, "login", source(request)))

    @app.post("/v1/auth/password/login", response_model=Tokens, responses=errors, operation_id="loginWithPassword")
    def password_login(body: PasswordLogin, request: Request):
        return result(auth_service.password_login(body.phone, body.password, source(request)))

    @app.post("/v1/auth/password/reset", response_model=Tokens, responses=errors, operation_id="resetPassword")
    def password_reset(body: CodePassword, request: Request):
        return result(auth_service.code_auth(body, "reset", source(request)))

    @app.post("/v1/auth/refresh", response_model=Tokens, responses=errors, operation_id="refreshSession")
    def refresh(body: Refresh, request: Request):
        return result(auth_service.refresh(body.refresh_token, source(request)))

    @app.post("/v1/auth/logout", status_code=204, responses=errors, operation_id="logoutSessionFamily")
    def auth_logout(body: Refresh):
        auth_service.logout(body.refresh_token)
        return Response(status_code=204)

    def valid_query(request: Request, allowed: set[str]) -> bool:
        pairs = request.query_params.multi_items()
        return (len(pairs) == len({k for k, _ in pairs}) and all(k in allowed for k, _ in pairs)
                and all(v.isascii() for k, v in pairs if k == "cursor"))

    @app.exception_handler(RequestValidationError)
    async def invalid_request(_request, _error):
        return failure(Code.INVALID_REQUEST)

    @app.exception_handler(HTTPException)
    async def http_error(_request, error):
        code = {404: Code.NOT_FOUND, 405: Code.METHOD_NOT_ALLOWED}.get(error.status_code, Code.INVALID_REQUEST)
        return failure(code)

    @app.get("/healthz", response_model=Health, operation_id="health")
    def health():
        return {"api_version": 1, "status": "ok"}

    @app.get("/v1/account", response_model=Account, responses=errors, operation_id="currentAccount")
    def account(request: Request, current_token: Token):
        if not valid_query(request, set()):
            return failure(Code.INVALID_REQUEST)
        result = service.account(current_token)
        return failure(result.error) if result.error else result.value

    @app.delete("/v1/session", status_code=204, responses=errors, operation_id="revokeSession")
    def logout(request: Request, current_token: Token):
        if not valid_query(request, set()):
            return failure(Code.INVALID_REQUEST)
        result = service.logout(current_token)
        return failure(result.error) if result.error else Response(status_code=204)

    @app.get("/v1/spaces", response_model=SpacesPage, responses=errors, operation_id="listSpaces")
    def spaces(request: Request, current_token: Token, limit: Limit = 100, cursor: Cursor = ""):
        if not valid_query(request, {"limit", "cursor"}):
            return failure(Code.INVALID_REQUEST)
        result = service.spaces(current_token, limit, cursor)
        return failure(result.error) if result.error else result.value

    @app.get("/v1/spaces/{space_id}/projects", response_model=ProjectsPage, responses=errors,
             operation_id="listProjects")
    def projects(request: Request, space_id: str, current_token: Token, limit: Limit = 100, cursor: Cursor = ""):
        if len(space_id) > 64 or not valid_query(request, {"limit", "cursor"}):
            return failure(Code.INVALID_REQUEST)
        result = service.projects(current_token, space_id, limit, cursor)
        return failure(result.error) if result.error else result.value

    @app.post("/v1/sync/uploads", response_model=UploadStarted, responses=errors, operation_id="beginProjectUpload")
    def begin_upload(body: BeginUpload, current_token: Token):
        return result(sync_service.begin(current_token, body))

    @app.post("/v1/sync/uploads/{upload_id}/chunks/{digest}", status_code=204, responses=errors,
              operation_id="uploadProjectChunk", openapi_extra={"requestBody": {"required": True, "content": {
                  "application/octet-stream": {"schema": {"type": "string", "format": "binary", "maxLength": 1048576}}}}})
    async def upload_chunk(request: Request, upload_id: str, digest: str, current_token: Token):
        from starlette.concurrency import run_in_threadpool
        data = await request.body()
        value = await run_in_threadpool(sync_service.put_chunk, current_token, upload_id, digest, data)
        return failure(value.error) if value.error else Response(status_code=204)

    @app.post("/v1/sync/uploads/{upload_id}/commit", response_model=UploadCommitted, responses=errors,
              operation_id="commitProjectUpload")
    def commit_upload(body: Empty, upload_id: str, current_token: Token):
        return result(sync_service.commit(current_token, upload_id))

    @app.delete("/v1/sync/uploads/{upload_id}", status_code=204, responses=errors, operation_id="abortProjectUpload")
    def abort_upload(upload_id: str, current_token: Token):
        value = sync_service.abort(current_token, upload_id)
        return failure(value.error) if value.error else Response(status_code=204)

    @app.get("/v1/sync/projects/{project_id}/revisions/{revision}", response_model=DownloadManifest, responses=errors,
             operation_id="downloadProjectManifest")
    def download_manifest(project_id: str, revision: int, current_token: Token):
        return result(sync_service.download(current_token, project_id, revision))

    @app.get("/v1/sync/projects/{project_id}/revisions/{revision}/chunks/{digest}", responses=errors,
             operation_id="downloadProjectChunk")
    def download_chunk(project_id: str, revision: int, digest: str, current_token: Token):
        value = sync_service.download(current_token, project_id, revision, digest)
        return failure(value.error) if value.error else Response(value.value, media_type="application/octet-stream")

    # Validation failures use the explicit 400 envelope, never FastAPI's input-
    # echoing 422 body. Keep the generated machine contract consistent with it.
    schema = app.openapi()
    for path in schema["paths"].values():
        for operation in path.values():
            operation.get("responses", {}).pop("422", None)
    return app
