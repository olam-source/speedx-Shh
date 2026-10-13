#!/usr/bin/env python3
"""
SpeedX router aggregator.

Ingests NDJSON telemetry from Pi edge nodes and broadcasts to coach clients.

Ports:
  - ingest: accepts edge-node writes (default 9101)
  - coach:  accepts coach-app reads/writes (default 9100)
"""

import argparse
import asyncio
import json
from datetime import datetime, timezone


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


class Aggregator:
    def __init__(self) -> None:
        self.coach_writers: set[asyncio.StreamWriter] = set()

    async def _broadcast(self, payload: dict) -> None:
        if "t" not in payload:
            payload["t"] = "garmin"
        payload["router_ts"] = iso_now()
        line = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        dead: list[asyncio.StreamWriter] = []
        for w in self.coach_writers:
            try:
                w.write(line)
                await w.drain()
            except Exception:
                dead.append(w)
        for w in dead:
            self.coach_writers.discard(w)
            w.close()

    async def handle_ingest(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        peer = writer.get_extra_info("peername")
        print(f"[ingest] connected: {peer}")
        try:
            while True:
                raw = await reader.readline()
                if not raw:
                    break
                line = raw.decode("utf-8", errors="ignore").strip()
                if not line:
                    continue
                try:
                    payload = json.loads(line)
                except json.JSONDecodeError:
                    continue
                await self._broadcast(payload)
        finally:
            writer.close()
            await writer.wait_closed()
            print(f"[ingest] disconnected: {peer}")

    async def handle_coach(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        peer = writer.get_extra_info("peername")
        self.coach_writers.add(writer)
        print(f"[coach] connected: {peer} ({len(self.coach_writers)} clients)")
        try:
            while True:
                raw = await reader.readline()
                if not raw:
                    break
                line = raw.decode("utf-8", errors="ignore").strip()
                if not line:
                    continue
                print(f"[coach-cmd] {line}")
        finally:
            self.coach_writers.discard(writer)
            writer.close()
            await writer.wait_closed()
            print(f"[coach] disconnected: {peer} ({len(self.coach_writers)} clients)")


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--ingest-port", type=int, default=9101)
    ap.add_argument("--coach-port", type=int, default=9100)
    args = ap.parse_args()

    agg = Aggregator()
    ingest_server = await asyncio.start_server(agg.handle_ingest, args.host, args.ingest_port)
    coach_server = await asyncio.start_server(agg.handle_coach, args.host, args.coach_port)

    print(f"[router] ingest tcp://{args.host}:{args.ingest_port}")
    print(f"[router] coach  tcp://{args.host}:{args.coach_port}")
    async with ingest_server, coach_server:
        await asyncio.gather(ingest_server.serve_forever(), coach_server.serve_forever())


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
