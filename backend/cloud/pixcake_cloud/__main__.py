import argparse
import json
import os
import sqlite3
import sys
from pathlib import Path

from .config import Settings
from .store import Store


def parser():
    cli = argparse.ArgumentParser(description="PixCake cloud directory service and trusted local administration")
    cli.add_argument("--db", type=Path, default=None, help="Server database, or PIXCAKE_CLOUD_DB")
    commands = cli.add_subparsers(dest="command", required=True)
    commands.add_parser("init")
    account = commands.add_parser("create-account")
    account.add_argument("--name", required=True)
    team = commands.add_parser("create-team")
    team.add_argument("--owner", required=True)
    team.add_argument("--name", required=True)
    grant = commands.add_parser("set-permissions")
    grant.add_argument("--account", required=True)
    grant.add_argument("--space", required=True)
    grant.add_argument("--permission", action="append", choices=("read", "write", "manage"), default=[])
    revoke = commands.add_parser("revoke-membership")
    revoke.add_argument("--account", required=True)
    revoke.add_argument("--space", required=True)
    project = commands.add_parser("create-project")
    project.add_argument("--space", required=True)
    project.add_argument("--name", required=True)
    session = commands.add_parser("issue-session")
    session.add_argument("--account", required=True)
    session.add_argument("--ttl", type=int, default=3600)
    session.add_argument("--output", type=Path, required=True, help="New private file; never prints the token")
    serve = commands.add_parser("serve")
    serve.add_argument("--port", type=int, default=8088)
    commands.add_parser("export-openapi")
    return cli


def main(argv=None):
    args = parser().parse_args(argv)
    path = args.db or os.environ.get("PIXCAKE_CLOUD_DB")
    if not path:
        print("A server database path is required (--db or PIXCAKE_CLOUD_DB).", file=sys.stderr)
        return 2
    os.umask(0o077)
    store = Store(Path(path))
    try:
        result = {"status": "ok"}
        if args.command == "init":
            store.initialize()
        elif args.command == "create-account":
            result = store.create_account(args.name)
        elif args.command == "create-team":
            result = {"space_id": store.create_team(args.owner, args.name)}
        elif args.command == "set-permissions":
            store.set_permissions(args.account, args.space, args.permission)
        elif args.command == "revoke-membership":
            store.revoke_membership(args.account, args.space)
        elif args.command == "create-project":
            result = {"project_id": store.create_project(args.space, args.name)}
        elif args.command == "issue-session":
            # Reserve the destination first: never overwrite or print a token.
            fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            try:
                with os.fdopen(fd, "w") as destination:
                    def deliver(token):
                        destination.write(token + "\n")
                        destination.flush()
                        os.fsync(destination.fileno())
                    store.issue_session(args.account, ttl_seconds=args.ttl, deliver=deliver)
            except BaseException:
                args.output.unlink(missing_ok=True)
                raise
            result = {"status": "session_written", "expires_in": args.ttl}
        elif args.command in ("serve", "export-openapi"):
            from .api import create_app
            settings = Settings.from_environment(store.database)
            app = create_app(settings)
            if args.command == "export-openapi":
                print(json.dumps(app.openapi(), ensure_ascii=False, indent=2, sort_keys=True))
                return 0
            if not 1 <= args.port <= 65535:
                raise ValueError("Invalid port.")
            import uvicorn
            uvicorn.run(app, host="127.0.0.1", port=args.port, workers=1, http="h11", loop="asyncio",
                        ws="none", proxy_headers=False, access_log=False, server_header=False,
                        limit_concurrency=64, timeout_keep_alive=5, h11_max_incomplete_event_size=32768)
            return 0
        print(json.dumps(result, ensure_ascii=False))
        return 0
    except (ValueError, RuntimeError, OSError, sqlite3.Error) as error:
        print(f"Cloud command failed ({type(error).__name__}); check configuration and arguments.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
