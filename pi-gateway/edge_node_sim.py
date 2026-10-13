#!/usr/bin/env python3
"""
SpeedX Pi edge-node simulator.

Emits Garmin-style NDJSON telemetry to the router ingest port.
Run 4-6 instances to emulate multiple Pi nodes.
"""

import argparse
import asyncio
import json
import math
import random
from datetime import datetime, timezone


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


class AthleteState:
    def __init__(self, athlete_id: int, team: int) -> None:
        self.id = athlete_id
        self.team = team
        self.t = random.random() * math.pi * 2
        self.dist_m = random.uniform(0, 50)
        self.sample = 0

    def tick(self) -> dict:
        self.t += 0.17
        hr = int(145 + 25 * math.sin(self.t) + random.uniform(-4, 4))
        speed_mps = max(2.6, 4.2 + 0.5 * math.sin(self.t / 2) + random.uniform(-0.2, 0.2))
        self.dist_m += speed_mps * 2
        self.sample += 1
        return {
            "t": "garmin",
            "id": self.id,
            "team": self.team,
            "heart_rate": hr,
            "pace_sec_per_mile": int(1609.344 / speed_mps),
            "distance_m": round(self.dist_m, 1),
            "run_cadence": int(170 + 8 * math.sin(self.t / 3) + random.uniform(-2, 2)),
            "speed_mps": round(speed_mps, 2),
            "effort_pct": max(35, min(98, int((hr / 190) * 100))),
            "sample": self.sample,
            "ts": iso_now(),
        }


async def run(args: argparse.Namespace) -> None:
    athletes = [AthleteState(args.start_id + i, args.team) for i in range(args.athletes)]
    while True:
        try:
            reader, writer = await asyncio.open_connection(args.router_host, args.router_port)
            print(f"[edge-{args.node}] connected to router {args.router_host}:{args.router_port}")
            while True:
                for a in athletes:
                    pkt = a.tick()
                    pkt["node_id"] = args.node
                    writer.write((json.dumps(pkt, separators=(",", ":")) + "\n").encode("utf-8"))
                await writer.drain()
                await asyncio.sleep(args.interval_s)
        except Exception as err:
            print(f"[edge-{args.node}] reconnecting after error: {err}")
            await asyncio.sleep(1.5)


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser()
    ap.add_argument("--node", type=int, required=True, help="Pi node id")
    ap.add_argument("--router-host", default="127.0.0.1")
    ap.add_argument("--router-port", type=int, default=9101)
    ap.add_argument("--athletes", type=int, default=6)
    ap.add_argument("--start-id", type=int, default=100)
    ap.add_argument("--team", type=int, default=1)
    ap.add_argument("--interval-s", type=float, default=2.0)
    return ap.parse_args()


if __name__ == "__main__":
    try:
        asyncio.run(run(parse_args()))
    except KeyboardInterrupt:
        pass
