import hashlib
import hmac
import secrets

from .passwords import PasswordBusy, PasswordPolicyError, Passwords
from .results import Code, Result
from .sms import DevelopmentSmsSender
from .store import new_id


def digest(token: str) -> str:
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


class AuthService:
    def __init__(self, store, settings, sender=None):
        self.store = store
        self.sender = sender
        self.development = settings.development and isinstance(sender, DevelopmentSmsSender)
        if isinstance(sender, DevelopmentSmsSender) and not settings.development:
            raise ValueError("Mock SMS is forbidden in production.")
        self.key = hmac.digest(settings.cursor_secret.encode(), b"pixcake.auth.v1", "sha256")
        self.passwords = Passwords()

    def configuration(self):
        return {"api_version": 1, "sms_enabled": self.sender is not None, "development": self.development,
                "password_min_length": 15, "password_max_length": 128}

    def _mac(self, *parts):
        return hmac.new(self.key, "\0".join(parts).encode("utf-8"), hashlib.sha256).hexdigest()

    def _rate(self, phone, ip, action):
        now = int(self.store.clock())
        duration, phone_max, ip_max = (3600, 5, 20) if action == "sms" else (600, 30, 120)
        with self.store.transaction(write=True) as db:
            db.execute("DELETE FROM auth_limits WHERE reset_at<=?", (now,))
            if db.execute("SELECT count(*) FROM auth_limits").fetchone()[0] >= 50000:
                return False
            buckets = [(self._mac("rate", action, "phone", phone), phone_max),
                       (self._mac("rate", action, "ip", ip), ip_max)]
            for bucket, maximum in buckets:
                row = db.execute("SELECT count FROM auth_limits WHERE bucket=?", (bucket,)).fetchone()
                if row and row[0] >= maximum:
                    return False
            for bucket, _ in buckets:
                db.execute("""INSERT INTO auth_limits VALUES(?,1,?)
                    ON CONFLICT(bucket) DO UPDATE SET count=count+1""", (bucket, now + duration))
        return True

    def send_code(self, phone, purpose, ip):
        if self.sender is None:
            return Result(error=Code.SMS_UNAVAILABLE)
        if not self._rate(phone, ip, "sms"):
            return Result(error=Code.RATE_LIMITED)
        now = int(self.store.clock())
        challenge = new_id("otp")
        code = f"{secrets.randbelow(1_000_000):06d}"
        with self.store.transaction(write=True) as db:
            previous = db.execute("SELECT sent_at FROM sms_challenges WHERE phone=? AND purpose=?", (phone, purpose)).fetchone()
            if previous and now - previous[0] < 60:
                return Result(error=Code.RATE_LIMITED)
            db.execute("DELETE FROM sms_challenges WHERE expires_at<? OR (phone=? AND purpose=?)", (now - 3600, phone, purpose))
            db.execute("INSERT INTO sms_challenges(id,phone,purpose,code_hash,expires_at,sent_at) VALUES(?,?,?,?,?,?)",
                       (challenge, phone, purpose, self._mac("code", challenge, phone, purpose, code), now + 300, now))
        try:
            self.sender.send(phone, code, purpose, challenge)
        except Exception:
            # Never persist or log a provider's error string: it can contain OTPs.
            with self.store.transaction(write=True) as db:
                db.execute("DELETE FROM sms_challenges WHERE id=?", (challenge,))
            return Result(error=Code.SMS_UNAVAILABLE)
        with self.store.transaction(write=True) as db:
            changed = db.execute("UPDATE sms_challenges SET delivered=1 WHERE id=?", (challenge,)).rowcount
            if not changed:
                return Result(error=Code.CODE_INVALID)
        return Result({"api_version": 1, "challenge_id": challenge, "expires_at": now + 300,
                       "resend_after": 60, "development_code": code if self.development else ""})

    def _consume(self, db, phone, purpose, challenge, code):
        row = db.execute("SELECT * FROM sms_challenges WHERE id=? AND phone=? AND purpose=?",
                         (challenge, phone, purpose)).fetchone()
        if row is None or not row["delivered"] or row["attempts"] >= 5:
            return Code.CODE_INVALID
        if row["expires_at"] <= int(self.store.clock()):
            return Code.CODE_EXPIRED
        db.execute("UPDATE sms_challenges SET attempts=attempts+1 WHERE id=?", (challenge,))
        if not hmac.compare_digest(row["code_hash"], self._mac("code", challenge, phone, purpose, code)):
            return Code.CODE_INVALID
        db.execute("DELETE FROM sms_challenges WHERE id=?", (challenge,))
        return None

    def _pair(self, db, account_id, family=None, refresh_expiry=None):
        now = int(self.store.clock())
        family = family or new_id("fam")
        refresh_expiry = refresh_expiry or now + 30 * 86400
        access, refresh = secrets.token_urlsafe(32), secrets.token_urlsafe(32)
        db.execute("DELETE FROM sessions WHERE expires_at<=?", (now,))
        db.execute("DELETE FROM refresh_sessions WHERE expires_at<=?", (now,))
        db.execute("INSERT INTO sessions(token_hash,account_id,expires_at,family_id) VALUES(?,?,?,?)",
                   (digest(access), account_id, min(now + 3600, refresh_expiry), family))
        db.execute("INSERT INTO refresh_sessions(token_hash,account_id,family_id,expires_at) VALUES(?,?,?,?)",
                   (digest(refresh), account_id, family, refresh_expiry))
        return Result({"api_version": 1, "account_id": account_id, "access_token": access, "refresh_token": refresh,
                       "session_expires_at": min(now + 3600, refresh_expiry), "refresh_expires_at": refresh_expiry})

    def code_auth(self, data, purpose, ip):
        if not self._rate(data.phone, ip, "auth"):
            return Result(error=Code.RATE_LIMITED)
        encoded = None
        if purpose != "login":
            try:
                encoded = self.passwords.hash(data.password)
            except PasswordPolicyError:
                return Result(error=Code.PASSWORD_WEAK)
            except PasswordBusy:
                return Result(error=Code.RATE_LIMITED)
        with self.store.transaction(write=True) as db:
            error = self._consume(db, data.phone, purpose, data.challenge_id, data.code)
            if error:
                return Result(error=error)
            row = db.execute("""SELECT c.account_id,a.enabled FROM credentials c
                JOIN accounts a ON a.id=c.account_id WHERE phone=?""", (data.phone,)).fetchone()
            if purpose == "register":
                if row:
                    return Result(error=Code.ACCOUNT_EXISTS)
                account_id = new_id("acc")
                db.execute("INSERT INTO accounts(id,name) VALUES(?,?)", (account_id, "用户 " + account_id[-6:]))
                self.store._create_space(db, account_id, "个人空间", "personal")
                db.execute("INSERT INTO credentials VALUES(?,?,?)", (data.phone, account_id, encoded))
            else:
                if not row or not row["enabled"]:
                    return Result(error=Code.CREDENTIALS_INVALID)
                account_id = row["account_id"]
                if purpose == "reset":
                    db.execute("UPDATE credentials SET password_hash=? WHERE phone=?", (encoded, data.phone))
                    db.execute("UPDATE sessions SET revoked=1 WHERE account_id=?", (account_id,))
                    db.execute("UPDATE refresh_sessions SET revoked=1 WHERE account_id=?", (account_id,))
            return self._pair(db, account_id)

    def password_login(self, phone, password, ip):
        if not self._rate(phone, ip, "auth"):
            return Result(error=Code.RATE_LIMITED)
        with self.store.transaction() as db:
            row = db.execute("SELECT account_id,password_hash FROM credentials WHERE phone=?", (phone,)).fetchone()
        try:
            valid, upgraded = self.passwords.verify(row["password_hash"] if row else None, password)
        except PasswordPolicyError:
            return Result(error=Code.CREDENTIALS_INVALID)
        except PasswordBusy:
            return Result(error=Code.RATE_LIMITED)
        if not valid or row is None:
            return Result(error=Code.CREDENTIALS_INVALID)
        with self.store.transaction(write=True) as db:
            current = db.execute("""SELECT c.password_hash,a.enabled FROM credentials c
                JOIN accounts a ON a.id=c.account_id WHERE c.phone=?""", (phone,)).fetchone()
            if not current or not current["enabled"] or current["password_hash"] != row["password_hash"]:
                return Result(error=Code.CREDENTIALS_INVALID)
            if upgraded:
                db.execute("UPDATE credentials SET password_hash=? WHERE phone=?", (upgraded, phone))
            return self._pair(db, row["account_id"])

    def refresh(self, token, ip):
        if not self._rate(digest(token), ip, "refresh"):
            return Result(error=Code.RATE_LIMITED)
        with self.store.transaction(write=True) as db:
            row = db.execute("""SELECT r.*,a.enabled FROM refresh_sessions r
                JOIN accounts a ON a.id=r.account_id WHERE token_hash=?""", (digest(token),)).fetchone()
            if not row or not row["enabled"] or row["revoked"] or row["expires_at"] <= int(self.store.clock()):
                return Result(error=Code.UNAUTHORIZED)
            if row["used"]:
                self.revoke_family(db, row["family_id"])
                return Result(error=Code.UNAUTHORIZED)
            db.execute("UPDATE refresh_sessions SET used=1 WHERE token_hash=?", (digest(token),))
            db.execute("UPDATE sessions SET revoked=1 WHERE family_id=?", (row["family_id"],))
            return self._pair(db, row["account_id"], row["family_id"], row["expires_at"])

    @staticmethod
    def revoke_family(db, family):
        db.execute("UPDATE sessions SET revoked=1 WHERE family_id=?", (family,))
        db.execute("UPDATE refresh_sessions SET revoked=1 WHERE family_id=?", (family,))

    def logout(self, token):
        # Idempotent, including expired access tokens: the refresh credential
        # proves ownership of the family and is never echoed to the caller.
        with self.store.transaction(write=True) as db:
            row = db.execute("SELECT family_id FROM refresh_sessions WHERE token_hash=?", (digest(token),)).fetchone()
            if row:
                self.revoke_family(db, row[0])
