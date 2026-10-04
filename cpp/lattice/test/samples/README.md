# Lattice sample stream

A sample of Lattice's REST entity stream for the Lattice adapter's tests and fuzz corpus ([ICS-023](https://github.com/MatthewK84/ICS/issues/23)): server-sent events, each holding one stream event as JSON.

It is synthetic. It was written for ICS from the field names of Lattice's public REST API, and describes no real system, unit, person or place: every entity ID and name is made up, and the positions are around the SITL rig's made-up range origin, 40 N, 100 W.

| Event | What it holds |
|---|---|
| Heartbeat | No entity |
| `example-asset-1`, PREEXISTING | A live asset with a full position, an ENU velocity, a diagonal covariance and a source time a second before receipt |
| `example-track-7`, UPDATE | A track with no height, a correlated covariance and a source time a day behind, its JSON split over several `data:` lines |
| `example-track-6`, DELETED | A deleted entity |
| `example-marker-2`, CREATED | An entity with no location |
| `not json` | Data that is not JSON |
