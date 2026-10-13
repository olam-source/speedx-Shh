# SpeedX Pi Gateway Starter

Starter services for the new SpeedX architecture:

```text
Garmin watches -> 4-6 Pi edge nodes -> router aggregator -> SpeedX Coach app
```

## Files

1. `router_aggregator.py`  
   Accepts edge-node telemetry on `:9101`, broadcasts to coach clients on `:9100`.
2. `edge_node_sim.py`  
   Simulates a Pi node sending Garmin-style NDJSON telemetry.

## Quick start

1. Start router aggregator:

```bash
python3 router_aggregator.py --host 0.0.0.0 --ingest-port 9101 --coach-port 9100
```

2. Start 4-6 edge node sims (separate terminals):

```bash
python3 edge_node_sim.py --node 1 --start-id 100 --athletes 8 --team 1
python3 edge_node_sim.py --node 2 --start-id 200 --athletes 8 --team 1
python3 edge_node_sim.py --node 3 --start-id 300 --athletes 8 --team 2
python3 edge_node_sim.py --node 4 --start-id 400 --athletes 8 --team 2
```

3. In SpeedX Coach app, connect router feed to:

```text
host: <router-ip>
port: 9100
```

The coach app accepts Garmin-style lines (`t=garmin`) and displays athlete stats/effort live.
