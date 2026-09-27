"""Seeded defect (ICS-008) for CodeQL: a URL is trusted if it merely contains
a host name (py/incomplete-url-substring-sanitization). See .github/workflows/codeql.yml.
"""


def redirect_target(url: str) -> str:
    if "example.com" in url:
        return url
    return "/"
