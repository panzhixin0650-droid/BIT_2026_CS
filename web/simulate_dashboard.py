#!/usr/bin/env python3
"""Generate a changing Dashboard V1 snapshot for local Web demonstrations."""

from __future__ import annotations

import argparse
import copy
import json
import math
import os
import random
import tempfile
import time
from datetime import date, datetime, timedelta, timezone
from pathlib import Path
from typing import Any
from zoneinfo import ZoneInfo


SCRIPT_DIR = Path(__file__).resolve().parent
REPOSITORY_ROOT = SCRIPT_DIR.parent
DEFAULT_SOURCE = REPOSITORY_ROOT / "contracts/examples/dashboard.sample.json"
DEFAULT_OUTPUT = SCRIPT_DIR / "dashboard.json"
BUSINESS_ZONE = ZoneInfo("Asia/Shanghai")


def positive_interval(value: str) -> float:
    interval = float(value)
    if interval < 0.2:
        raise argparse.ArgumentTypeError("刷新间隔不能小于 0.2 秒")
    return interval


def non_negative_steps(value: str) -> int:
    steps = int(value)
    if steps < 0:
        raise argparse.ArgumentTypeError("步数不能为负数")
    return steps


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="周期性生成 web/dashboard.json，模拟大屏实时数据变化。",
    )
    parser.add_argument(
        "--interval",
        type=positive_interval,
        default=2.0,
        help="两次快照之间的秒数，默认 2 秒。",
    )
    parser.add_argument(
        "--steps",
        type=non_negative_steps,
        default=0,
        help="生成次数；0 表示持续运行直到 Ctrl+C。",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=2026,
        help="随机种子，默认 2026，便于复现实验。",
    )
    parser.add_argument(
        "--source",
        type=Path,
        default=DEFAULT_SOURCE,
        help="初始 Dashboard V1 JSON。",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help="输出快照路径，默认 web/dashboard.json。",
    )
    return parser.parse_args()


class DashboardSimulator:
    def __init__(self, template: dict[str, Any], seed: int) -> None:
        self.dashboard = copy.deepcopy(template)
        self.random = random.Random(seed)
        self.tick = 0
        self.total_revenue_cents = int(template["summary"]["totalRevenueCents"])
        self._prepare_revenue_window()

    def _prepare_revenue_window(self) -> None:
        points = self.dashboard.get("revenuePoints", [])
        amounts = [max(0, int(point.get("revenueCents", 0))) for point in points]
        if not amounts:
            amounts = [0] * 30
        amounts = ([0] * 30 + amounts)[-30:]

        today = datetime.now(BUSINESS_ZONE).date()
        self.dashboard["revenuePoints"] = [
            {
                "date": (today - timedelta(days=29 - index)).isoformat(),
                "revenueCents": amount,
            }
            for index, amount in enumerate(amounts)
        ]

    def next_snapshot(self) -> dict[str, Any]:
        self.tick += 1
        now = datetime.now(timezone.utc).replace(microsecond=0)
        business_today = now.astimezone(BUSINESS_ZONE).date()

        summary = self.dashboard["summary"]
        points = self.dashboard["revenuePoints"]
        if points[-1]["date"] != business_today.isoformat():
            points.append({"date": business_today.isoformat(), "revenueCents": 0})
            del points[:-30]

        revenue_increment = self.random.randint(35, 260) if self.random.random() < 0.84 else 0
        points[-1]["revenueCents"] += revenue_increment
        self.total_revenue_cents += revenue_increment

        summary["todayRevenueCents"] = points[-1]["revenueCents"]
        summary["monthRevenueCents"] = sum(
            point["revenueCents"]
            for point in points
            if self._same_month(point["date"], business_today)
        )
        summary["totalRevenueCents"] = self.total_revenue_cents

        pile_count = max(1, int(summary["pileCount"]))
        wave = (math.sin(self.tick / 2.2) + 1) / 2
        in_use = round(pile_count * (0.18 + wave * 0.43))
        fault = 1 if self.tick % 13 in (0, 1) and pile_count - in_use > 1 else 0
        idle = max(0, pile_count - in_use - fault)
        self.dashboard["pileStates"] = {
            "idle": idle,
            "inUse": in_use,
            "fault": fault,
        }

        forecast_available = min(4, idle)
        forecast_load = round(16 + 38 * wave + self.random.uniform(-2.2, 2.2), 1)
        congestion = self._congestion_level(forecast_load, forecast_available)
        next_hour = now.replace(minute=0, second=0) + timedelta(hours=1)
        station_id = self._representative_station_id()
        self.dashboard["predictions"] = [
            {
                "stationId": station_id,
                "horizonHours": 1,
                "generatedAt": self._iso_utc(now),
                "source": "MOCK",
                "peakStartAt": None,
                "peakEndAt": None,
                "congestionLevel": congestion,
                "points": [
                    {
                        "time": self._iso_utc(next_hour),
                        "loadKw": forecast_load,
                        "availablePiles": forecast_available,
                        "congestionLevel": congestion,
                    }
                ],
            }
        ]
        self.dashboard["generatedAt"] = self._iso_utc(now)
        return copy.deepcopy(self.dashboard)

    def _representative_station_id(self) -> int:
        predictions = self.dashboard.get("predictions", [])
        if predictions:
            return max(1, int(predictions[0].get("stationId", 1)))
        return 1

    @staticmethod
    def _same_month(date_text: str, expected: date) -> bool:
        try:
            candidate = date.fromisoformat(date_text)
        except ValueError:
            return False
        return candidate.year == expected.year and candidate.month == expected.month

    @staticmethod
    def _congestion_level(load_kw: float, available_piles: int) -> str:
        if available_piles <= 1 or load_kw >= 48:
            return "HIGH"
        if available_piles <= 2 or load_kw >= 34:
            return "MEDIUM"
        return "LOW"

    @staticmethod
    def _iso_utc(value: datetime) -> str:
        return value.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")


def load_template(path: Path) -> dict[str, Any]:
    with path.expanduser().resolve().open("r", encoding="utf-8") as source:
        dashboard = json.load(source)
    required = {"schemaVersion", "summary", "pileStates", "revenuePoints", "predictions"}
    missing = sorted(required.difference(dashboard))
    if missing:
        raise ValueError(f"初始 JSON 缺少字段：{', '.join(missing)}")
    return dashboard


def write_atomically(path: Path, dashboard: dict[str, Any]) -> None:
    target = path.expanduser().resolve()
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=target.parent,
            prefix=f".{target.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary:
            temporary_name = temporary.name
            json.dump(dashboard, temporary, ensure_ascii=False, indent=2)
            temporary.write("\n")
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_name, target)
    finally:
        if temporary_name is not None:
            Path(temporary_name).unlink(missing_ok=True)


def print_snapshot(sequence: int, dashboard: dict[str, Any], output: Path) -> None:
    summary = dashboard["summary"]
    piles = dashboard["pileStates"]
    prediction = dashboard["predictions"][0]
    point = prediction["points"][0]
    print(
        f"[{sequence:04d}] {dashboard['generatedAt']} -> {output} | "
        f"今日营收 ¥{summary['todayRevenueCents'] / 100:.2f} | "
        f"闲置/在用/异常 {piles['idle']}/{piles['inUse']}/{piles['fault']} | "
        f"预测 {point['loadKw']:.1f} kW {prediction['congestionLevel']}",
        flush=True,
    )


def main() -> int:
    arguments = parse_arguments()
    template = load_template(arguments.source)
    simulator = DashboardSimulator(template, arguments.seed)
    output = arguments.output.expanduser().resolve()

    print(f"模拟器已启动：每 {arguments.interval:g} 秒更新 {output}", flush=True)
    print("按 Ctrl+C 停止；大屏会保留最后一份快照。", flush=True)
    sequence = 0
    try:
        while arguments.steps == 0 or sequence < arguments.steps:
            started_at = time.monotonic()
            sequence += 1
            dashboard = simulator.next_snapshot()
            write_atomically(output, dashboard)
            print_snapshot(sequence, dashboard, output)
            if arguments.steps and sequence >= arguments.steps:
                break
            elapsed = time.monotonic() - started_at
            time.sleep(max(0, arguments.interval - elapsed))
    except KeyboardInterrupt:
        print("\n模拟器已停止。", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
