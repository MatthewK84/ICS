"""Shared GitHub REST helpers for the repository's automation scripts.

Standard library only: requests with bounded retry and exponential backoff,
pagination, and typed accessors that validate JSON responses.
"""

from __future__ import annotations

import json
import os
import time
import urllib.error
import urllib.request
from dataclasses import dataclass

API_URL: str = "https://api.github.com"
PAGE_SIZE: int = 100
MAX_PAGES: int = 50
MAX_ATTEMPTS: int = 4
BACKOFF_SECONDS: float = 2.0
REQUEST_TIMEOUT_SECONDS: float = 30.0
RETRYABLE_STATUSES: frozenset[int] = frozenset({429, 500, 502, 503, 504})
FORBIDDEN_STATUS: int = 403

JsonObject = dict[str, object]


class ScriptError(Exception):
    """Raised for any failure that should stop a script with a clear message."""


@dataclass(frozen=True)
class ApiConfig:
    token: str
    repo: str
    base_url: str = API_URL


def require_str(data: JsonObject, key: str, where: str) -> str:
    value: object = data.get(key)
    if not isinstance(value, str) or not value:
        raise ScriptError(f"{where}: '{key}' must be a non-empty string")
    return value


def require_int(data: JsonObject, key: str, where: str) -> int:
    value: object = data.get(key)
    if not isinstance(value, int) or isinstance(value, bool):
        raise ScriptError(f"{where}: '{key}' must be an integer")
    return value


def require_object(data: JsonObject, key: str, where: str) -> JsonObject:
    value: object = data.get(key)
    if not isinstance(value, dict):
        raise ScriptError(f"{where}: '{key}' must be an object")
    return value


def require_objects(data: JsonObject, key: str, where: str) -> list[JsonObject]:
    value: object = data.get(key)
    if not isinstance(value, list):
        raise ScriptError(f"{where}: '{key}' must be a list")
    items: list[JsonObject] = []
    for item in value:
        if not isinstance(item, dict):
            raise ScriptError(f"{where}: every entry in '{key}' must be an object")
        items.append(item)
    return items


def build_request(config: ApiConfig, method: str, path: str, payload: JsonObject | None) -> urllib.request.Request:
    body: bytes | None = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(f"{config.base_url}{path}", data=body, method=method)
    request.add_header("Accept", "application/vnd.github+json")
    request.add_header("Authorization", f"Bearer {config.token}")
    request.add_header("X-GitHub-Api-Version", "2022-11-28")
    if body is not None:
        request.add_header("Content-Type", "application/json")
    return request


def is_retryable(error: urllib.error.HTTPError) -> bool:
    if error.code in RETRYABLE_STATUSES:
        return True
    rate_limited: bool = error.headers.get("x-ratelimit-remaining") == "0"
    return error.code == FORBIDDEN_STATUS and (rate_limited or error.headers.get("retry-after") is not None)


def retry_delay(error: urllib.error.HTTPError | None, attempt: int) -> float:
    fallback: float = BACKOFF_SECONDS * (2**attempt)
    if error is None:
        return fallback
    header: str | None = error.headers.get("retry-after")
    if header is not None and header.isdigit():
        return max(float(header), fallback)
    return fallback


def send_once(request: urllib.request.Request) -> object:
    with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT_SECONDS) as response:
        raw: bytes = response.read()
    return json.loads(raw) if raw else None


def api_request(config: ApiConfig, method: str, path: str, payload: JsonObject | None = None) -> object:
    last_error: str = "no attempt made"
    for attempt in range(MAX_ATTEMPTS):
        http_error: urllib.error.HTTPError | None = None
        try:
            return send_once(build_request(config, method, path, payload))
        except urllib.error.HTTPError as error:
            if not is_retryable(error):
                detail: str = error.read().decode("utf-8", errors="replace")
                raise ScriptError(f"{method} {path} failed with {error.code}: {detail}") from error
            http_error = error
            last_error = f"HTTP {error.code}"
        except urllib.error.URLError as error:
            last_error = f"network error: {error.reason}"
        if attempt + 1 < MAX_ATTEMPTS:
            time.sleep(retry_delay(http_error, attempt))
    raise ScriptError(f"{method} {path} failed after {MAX_ATTEMPTS} attempts: {last_error}")


def fetch_all(config: ApiConfig, path: str) -> list[JsonObject]:
    items: list[JsonObject] = []
    for page in range(1, MAX_PAGES + 1):
        separator: str = "&" if "?" in path else "?"
        result: object = api_request(config, "GET", f"{path}{separator}per_page={PAGE_SIZE}&page={page}")
        if not isinstance(result, list):
            raise ScriptError(f"GET {path}: expected a list")
        items.extend(entry for entry in result if isinstance(entry, dict))
        if len(result) < PAGE_SIZE:
            return items
    raise ScriptError(f"GET {path}: more than {MAX_PAGES} pages")


def read_config() -> ApiConfig:
    token: str = os.environ.get("GITHUB_TOKEN", "")
    repo: str = os.environ.get("GITHUB_REPOSITORY", "")
    if not token or not repo:
        raise ScriptError("GITHUB_TOKEN and GITHUB_REPOSITORY must be set")
    return ApiConfig(token, repo)
