# proto/third_party

Protobuf files copied unchanged from other projects. They keep their authors' package names, style and licences.

| Folder | What it is | Source | Licence | Used by |
|---|---|---|---|---|
| [`sapient_msg`](sapient_msg) | SAPIENT's BSI Flex 335 v2.0 messages, package `sapient_msg.bsi_flex_335_v2_0` | [dstl/sapient-proto-files](https://github.com/dstl/sapient-proto-files), commit `244a741c13bcee628f84944aef91e4a66854bd7d` | Apache-2.0, Crown copyright ([`LICENCE.txt`](sapient_msg/LICENCE.txt), [`LICENSE-Apache-2.0.txt`](sapient_msg/LICENSE-Apache-2.0.txt)) | The SAPIENT adapter, `cpp/sapient` ([ICS-024](https://github.com/MatthewK84/ICS/issues/24)) |

The `.proto` files and `LICENCE.txt` are byte-for-byte the upstream files at that commit, from the root of Dstl's repository, which their imports call `sapient_msg`. Only the text of the Apache License 2.0, which Dstl's notice refers to, was added.

## How they are checked

- **Lint.** `buf.yaml` lints them with buf's MINIMAL rules only, and `buf format` skips them; the ICS rules (STANDARD and COMMENTS) are for ICS's own contracts.
- **Generation.** Only C++ is generated, by [`buf.gen.third_party.yaml`](../buf.gen.third_party.yaml), into `cpp/proto/gen/sapient_msg`, where it builds into `ics::proto` like the ICS messages.
- **Integrity.** [`tools.txt`](tools.txt) pins each folder by commit and by the sha256 of its `.proto` files concatenated in path order. `check-proto.sh` refuses a folder that does not match, so an edit cannot slip in.
- **Breaking changes.** `buf breaking` covers them like ICS's own files.

## Update a copy

From the repository root, with buf and protoc from `proto/tools.txt` on the `PATH`:

```sh
git clone https://github.com/dstl/sapient-proto-files /tmp/sapient-proto-files
git -C /tmp/sapient-proto-files checkout <commit>
rm -r proto/third_party/sapient_msg/bsi_flex_335_v2_0 proto/third_party/sapient_msg/proto_options.proto
cp -r /tmp/sapient-proto-files/bsi_flex_335_v2_0 /tmp/sapient-proto-files/proto_options.proto \
  /tmp/sapient-proto-files/LICENCE.txt proto/third_party/sapient_msg/
(cd proto/third_party && find sapient_msg -name '*.proto' | LC_ALL=C sort | xargs cat | sha256sum)
```

Then:

1. Put the commit and the new sha256 in `tools.txt`, and update the dependency register's review.
2. Regenerate the C++ with the two commands in `buf.gen.third_party.yaml`.
3. Run `proto/check-proto.sh` and the `cpp/sapient` tests.

Copy only the version folders ICS reads (now `bsi_flex_335_v2_0`) and `proto_options.proto`, which they import.
