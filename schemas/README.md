# schemas

The run-record JSON Schema, keyed to the C4 MOP and KPP IDs, and its mapping to the DroneScore import.

**Language:** JSON Schema. **Lead role:** Architect.

Every run record the pipeline writes must validate against this schema; the C++ record module ([ICS-079](https://github.com/MatthewK84/ICS/issues/79)) and the DroneScore export ([ICS-080](https://github.com/MatthewK84/ICS/issues/80)) build on it.

First issue: [ICS-013](https://github.com/MatthewK84/ICS/issues/13).
