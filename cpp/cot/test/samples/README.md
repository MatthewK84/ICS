# CoT sample feeds

Sample Cursor on Target payloads for the CoT adapter's tests and fuzz corpus ([ICS-022](https://github.com/MatthewK84/ICS/issues/22)). Each file is the payload of one UDP datagram.

They are synthetic. They were written for ICS from the public CoT event schema and public TAK documentation, and capture no real system, unit, person or place: every uid and callsign is made up, and the positions are around the SITL rig's made-up range origin, 40 N, 100 W.

| File | What it holds |
|---|---|
| `atak-sa.xml` | An ATAK-style self-position (SA) report, with the detail ATAK adds; `le` unknown |
| `uas-track.xml` | A UAS (`a-f-A-M-F-Q`) with a track, at CE90 2.146 m and LE90 3.29 m: one-sigma 1 m and 2 m |
| `hostile-estimate.xml` | A human estimate (`h-e`) of a hostile ground track, height unknown, from a sender whose clock is a day behind |
| `tak-server.xml` | A WinTAK-style report as a TAK Server relays it, with a UTC offset, a 6-digit fraction and server detail |
| `two-events.xml` | Two reports in one datagram |
| `chat-and-delete.xml` | A GeoChat message and a delete: events, but not positions |
| `not-events.xml` | Elements that are not CoT events |
| `malformed.xml` | A datagram cut short |
| `invalid-points.xml` | Events whose point is out of range, not a number, short an attribute or missing, or whose uid is empty |
| `bom.xml` | A UTF-8 byte order mark, CRLF line ends and a padded attribute |
