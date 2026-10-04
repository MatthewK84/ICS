# SAPIENT sample messages

Sample messages for the SAPIENT adapter's tests ([ICS-024](https://github.com/MatthewK84/ICS/issues/24)), each one `SapientMessage` (BSI Flex 335 v2.0) in protobuf's JSON form. The tests frame each message as a node sends it over TCP, read them back from one stream in chunks of every size from 1 to 64 bytes, and adapt them (`../samples_test.cpp`). `../../fuzz/corpus` holds some of them, framed.

## `dstl/`: the ICD samples

The 83 sample messages of Dstl's [BSI Flex 335 v2.0 test harness](https://github.com/dstl/BSI-Flex-335-v2-Test-Harness) at commit `6a2999d70d761d224914ddd8fa3f663af5005be0`, copied unchanged, at their paths in the harness:

| Folder | Messages |
|---|---|
| `SapientServicesValidator.UnitTests/True` | The 38 messages the harness's validator accepts: one or more of every message type |
| `SapientAsmSimulator/SapientAsmSimulator` | The 19 messages its sensor-node simulator sends by default |
| `SapientDmmSimulator/SapientDmmSimulator` | The 24 messages its fusion-node simulator sends by default |
| `ReadSampleSapientMessage.UnitTests` | 2 alerts from its reader's tests |

They are Crown copyright, under the Apache License 2.0 ([`dstl/LICENCE.txt`](dstl/LICENCE.txt), [`dstl/LICENSE-Apache-2.0.txt`](dstl/LICENSE-Apache-2.0.txt)). [`tools.txt`](tools.txt) records the commit and the sha256 of the JSON files concatenated in path order:

```sh
(cd dstl && find . -name '*.json' | LC_ALL=C sort | xargs cat | sha256sum)
```

Every sample parses. Their five detection reports give no record: four put a latitude and longitude in a UTM easting and northing, outside zone 30U, and one gives a range and bearing.

## `synthetic/`: a session

A session written for ICS. It describes no real system, unit, person or place: the node, object and report IDs are made up, and the positions are around the SITL rig's made-up range origin, 40 N, 100 W.

| File | What it holds |
|---|---|
| `1-registration.json` | A radar node registering UTM zone 14S and velocities in km/h, with up rates in m/s |
| `2-detection-utm.json` | A detection in the registered zone at `golden/frames/utm-geodetic.csv`'s range-origin-area point, with errors, an ENU velocity and an id (`N123AB`) |
| `3-detection-adjacent-zone.json` | A detection in zone 13S, at 39.99 N, 102.01 W by GeographicLib's GeoConvert |
| `4-detection-degrees-geoid.json` | A detection in degrees with a height above mean sea level (WGS84_G) and errors in degrees |
| `5-detection-radians.json` | A detection in radians with no height |
| `6-detection-range-bearing.json` | A detection by range and bearing, which ICS does not convert |
| `7-status-report.json` | A status report, which gives no record |
