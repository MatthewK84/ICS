"""Unit tests for gh_api.py (stdlib unittest, no network)."""

from __future__ import annotations

import email.message
import io
import time
import unittest
import urllib.error
from unittest import mock

import gh_api as gh


def http_error(code: int, headers: dict[str, str] | None = None) -> urllib.error.HTTPError:
    message = email.message.Message()
    for key, value in (headers or {}).items():
        message[key] = value
    return urllib.error.HTTPError("https://api.github.com/x", code, "error", message, io.BytesIO(b"{}"))


class RetryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.config = gh.ApiConfig("token", "owner/repo")
        patcher = mock.patch.object(time, "sleep")
        self.sleep = patcher.start()
        self.addCleanup(patcher.stop)

    def test_classifies_retryable_errors(self) -> None:
        self.assertTrue(gh.is_retryable(http_error(502)))
        self.assertTrue(gh.is_retryable(http_error(403, {"retry-after": "5"})))
        self.assertFalse(gh.is_retryable(http_error(403)))
        self.assertFalse(gh.is_retryable(http_error(422)))

    def test_retries_then_succeeds(self) -> None:
        with mock.patch.object(gh, "send_once", side_effect=[http_error(503), {"ok": True}]):
            self.assertEqual(gh.api_request(self.config, "GET", "/x"), {"ok": True})
        self.assertEqual(self.sleep.call_count, 1)

    def test_gives_up_after_bounded_attempts(self) -> None:
        failures: list[urllib.error.HTTPError] = [http_error(500)] * gh.MAX_ATTEMPTS
        with mock.patch.object(gh, "send_once", side_effect=failures), self.assertRaises(gh.ScriptError):
            gh.api_request(self.config, "GET", "/x")
        self.assertEqual(self.sleep.call_count, gh.MAX_ATTEMPTS - 1)

    def test_fails_fast_on_client_error(self) -> None:
        with mock.patch.object(gh, "send_once", side_effect=[http_error(422)]), self.assertRaises(gh.ScriptError):
            gh.api_request(self.config, "POST", "/x", {"title": "t"})
        self.sleep.assert_not_called()

    def test_fetch_all_follows_pages(self) -> None:
        full_page: list[gh.JsonObject] = [{"n": index} for index in range(gh.PAGE_SIZE)]
        with mock.patch.object(gh, "api_request", side_effect=[full_page, [{"n": -1}]]) as request:
            items: list[gh.JsonObject] = gh.fetch_all(self.config, "/items?state=all")
        self.assertEqual(len(items), gh.PAGE_SIZE + 1)
        self.assertIn("state=all&per_page=100&page=2", request.call_args_list[1].args[2])


class AccessorTests(unittest.TestCase):
    def test_require_helpers_validate_types(self) -> None:
        data: gh.JsonObject = {"name": "x", "count": 3, "flag": True, "child": {"a": 1}, "items": [{"b": 2}]}
        self.assertEqual(gh.require_str(data, "name", "t"), "x")
        self.assertEqual(gh.require_int(data, "count", "t"), 3)
        self.assertEqual(gh.require_object(data, "child", "t"), {"a": 1})
        self.assertEqual(gh.require_objects(data, "items", "t"), [{"b": 2}])
        with self.assertRaises(gh.ScriptError):
            gh.require_int(data, "flag", "t")
        with self.assertRaises(gh.ScriptError):
            gh.require_object(data, "name", "t")
        with self.assertRaises(gh.ScriptError):
            gh.require_objects({"items": [1]}, "items", "t")

    def test_read_config_needs_token_and_repo(self) -> None:
        with mock.patch.dict("os.environ", {"GITHUB_TOKEN": "", "GITHUB_REPOSITORY": "o/r"}), self.assertRaises(gh.ScriptError):
            gh.read_config()
        with mock.patch.dict("os.environ", {"GITHUB_TOKEN": "t", "GITHUB_REPOSITORY": "o/r"}):
            self.assertEqual(gh.read_config(), gh.ApiConfig("t", "o/r"))

    def test_build_request_sets_headers_and_body(self) -> None:
        request = gh.build_request(gh.ApiConfig("t", "o/r"), "POST", "/x", {"a": 1})
        self.assertEqual(request.full_url, "https://api.github.com/x")
        self.assertEqual(request.get_header("Authorization"), "Bearer t")
        self.assertEqual(request.get_header("Content-type"), "application/json")
        self.assertEqual(request.data, b'{"a": 1}')

    def test_retry_delay_honours_retry_after(self) -> None:
        self.assertEqual(gh.retry_delay(None, 1), gh.BACKOFF_SECONDS * 2)
        self.assertEqual(gh.retry_delay(http_error(403, {"retry-after": "30"}), 0), 30.0)


if __name__ == "__main__":
    unittest.main()
