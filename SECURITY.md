# Security policy

## Reporting a vulnerability

Report vulnerabilities privately through GitHub: open this repository's **Security** tab and choose **Report a vulnerability**. Do not open a public issue, pull request or discussion about a suspected vulnerability.

Include what you found, where it is (file, commit or component), how to reproduce it, and the impact you expect.

## Supported versions

ICS has no release yet. Security fixes land on `main` only.

## What stays out of this repository

This is a public repository built from publicly available information. Never commit:

- Controlled Unclassified Information (CUI), including Controlled Technical Information (CTI).
- Export-controlled data (ITAR or EAR).
- Classified, proprietary or vendor-restricted material, including SDKs, interface control documents and data received under an agreement.
- Credentials, tokens, keys, certificates or range network details.

If any of this is committed by mistake, do not try to fix it with another commit, because it stays in the history. Report it privately as described above so it can be purged.
