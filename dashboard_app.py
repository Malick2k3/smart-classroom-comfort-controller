from __future__ import annotations

import json
import os
import sqlite3
import subprocess
from datetime import datetime, timedelta, timezone
from pathlib import Path

from flask import Flask, jsonify, render_template, request
import paho.mqtt.client as mqtt


BASE_DIR = Path(__file__).resolve().parent
DB_PATH = BASE_DIR / "classroom.db"

MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.getenv("MQTT_USER", "")
MQTT_PASS = os.getenv("MQTT_PASS", "")
TELEMETRY_INTERVAL_MINUTES = 3 / 60
MOSQUITTO_PUB_EXE = os.getenv("MOSQUITTO_PUB_EXE", r"C:\Program Files\mosquitto\mosquitto_pub.exe")
ALERT_AFTER = timedelta(minutes=5)

TOPIC_TELEMETRY = "niang-lo-hanne-ndour/classroom/comfort/telemetry"
TOPIC_PROFILE_CONFIG = "niang-lo-hanne-ndour/classroom/comfort/config/system_mode"


app = Flask(__name__)
mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="smart-classroom-dashboard")
latest_snapshot: dict[str, object] = {
    "temperature": None,
    "humidity": None,
    "occupied": False,
    "fan_on": False,
    "comfort": "unknown",
    "state": "UNKNOWN",
    "threshold": 26.0,
    "system_mode": "real",
    "ts": None,
}
latest_alert: dict[str, object] = {
    "active": False,
    "message": "No active alerts",
    "started_at": None,
    "triggered_at": None,
    "resolved_at": None,
    "temperature": None,
    "threshold": None,
}
overheat_started_at: datetime | None = None
open_alert_id: int | None = None


def db_connection() -> sqlite3.Connection:
    connection = sqlite3.connect(DB_PATH)
    connection.row_factory = sqlite3.Row
    return connection


def publish_control_message(topic: str, payload: str, retain: bool = True) -> tuple[bool, str]:
    try:
        command = [
            MOSQUITTO_PUB_EXE,
            "-h", MQTT_HOST,
            "-p", str(MQTT_PORT),
            "-t", topic,
            "-m", payload,
        ]
        if MQTT_USER:
            command.extend(["-u", MQTT_USER, "-P", MQTT_PASS])
        if retain:
            command.append("-r")

        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode != 0:
            detail = (result.stderr or result.stdout or "mosquitto_pub failed").strip()
            return False, detail
        return True, "ok"
    except Exception as exc:
        return False, str(exc)


def init_db() -> None:
    with db_connection() as connection:
        connection.execute(
            """
            CREATE TABLE IF NOT EXISTS readings (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                ts TEXT NOT NULL,
                temperature REAL NOT NULL,
                humidity REAL NOT NULL,
                occupied INTEGER NOT NULL,
                fan_on INTEGER NOT NULL,
                comfort TEXT NOT NULL,
                state TEXT NOT NULL,
                threshold REAL NOT NULL,
                comfort_profile TEXT NOT NULL DEFAULT 'real'
            )
            """
        )
        columns = {
            row["name"]
            for row in connection.execute("PRAGMA table_info(readings)").fetchall()
        }
        if "comfort_profile" not in columns:
            connection.execute(
                "ALTER TABLE readings ADD COLUMN comfort_profile TEXT NOT NULL DEFAULT 'real'"
            )
        connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_readings_ts ON readings(ts)"
        )
        connection.execute(
            """
            CREATE TABLE IF NOT EXISTS alerts (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                started_at TEXT NOT NULL,
                triggered_at TEXT NOT NULL,
                resolved_at TEXT,
                temperature REAL NOT NULL,
                threshold REAL NOT NULL,
                message TEXT NOT NULL,
                active INTEGER NOT NULL DEFAULT 1
            )
            """
        )


def create_alert(started_at: datetime, triggered_at: datetime, temperature: float, threshold: float) -> None:
    global open_alert_id, latest_alert

    message = (
        f"Temperature stayed above threshold for 5+ minutes "
        f"({temperature:.1f} C >= {threshold:.1f} C)."
    )
    with db_connection() as connection:
        cursor = connection.execute(
            """
            INSERT INTO alerts (
                started_at, triggered_at, resolved_at, temperature, threshold, message, active
            ) VALUES (?, ?, NULL, ?, ?, ?, 1)
            """,
            (
                started_at.isoformat(),
                triggered_at.isoformat(),
                temperature,
                threshold,
                message,
            ),
        )
        open_alert_id = int(cursor.lastrowid)

    latest_alert = {
        "active": True,
        "message": message,
        "started_at": started_at.isoformat(),
        "triggered_at": triggered_at.isoformat(),
        "resolved_at": None,
        "temperature": temperature,
        "threshold": threshold,
    }


def resolve_alert(resolved_at: datetime) -> None:
    global open_alert_id, latest_alert

    if open_alert_id is None:
        latest_alert["active"] = False
        latest_alert["resolved_at"] = resolved_at.isoformat()
        return

    with db_connection() as connection:
        connection.execute(
            """
            UPDATE alerts
            SET resolved_at = ?, active = 0
            WHERE id = ?
            """,
            (resolved_at.isoformat(), open_alert_id),
        )

    latest_alert["active"] = False
    latest_alert["resolved_at"] = resolved_at.isoformat()
    open_alert_id = None


def evaluate_alerts(record: dict[str, object], timestamp: datetime) -> None:
    global overheat_started_at

    temperature = float(record["temperature"])
    threshold = float(record["threshold"])
    over_threshold = temperature >= threshold

    if over_threshold:
        if overheat_started_at is None:
            overheat_started_at = timestamp

        if (timestamp - overheat_started_at) >= ALERT_AFTER and open_alert_id is None:
            create_alert(overheat_started_at, timestamp, temperature, threshold)
        elif open_alert_id is not None:
            latest_alert["temperature"] = temperature
            latest_alert["threshold"] = threshold
    else:
        overheat_started_at = None
        if open_alert_id is not None:
            resolve_alert(timestamp)


def insert_reading(payload: dict[str, object]) -> None:
    timestamp_dt = datetime.now(timezone.utc)
    timestamp = timestamp_dt.isoformat()
    record = {
        "temperature": float(payload.get("temperature", 0.0)),
        "humidity": float(payload.get("humidity", 0.0)),
        "occupied": 1 if bool(payload.get("occupied", False)) else 0,
        "fan_on": 1 if bool(payload.get("fan_on", False)) else 0,
        "comfort": str(payload.get("comfort", "unknown")),
        "state": str(payload.get("state", "UNKNOWN")),
        "threshold": float(payload.get("threshold", 29.0)),
        "ts": timestamp,
    }

    with db_connection() as connection:
        connection.execute(
            """
            INSERT INTO readings (
                ts, temperature, humidity, occupied, fan_on, comfort, state, threshold
            ) VALUES (
                :ts, :temperature, :humidity, :occupied, :fan_on, :comfort, :state, :threshold
            )
            """,
            record,
        )

    latest_snapshot.update(record)
    latest_snapshot["system_mode"] = str(payload.get("system_mode", latest_snapshot.get("system_mode", "real")))
    evaluate_alerts(record, timestamp_dt)


def load_latest_snapshot() -> None:
    global open_alert_id

    with db_connection() as connection:
        row = connection.execute(
            """
            SELECT ts, temperature, humidity, occupied, fan_on, comfort, state, threshold
            FROM readings
            ORDER BY ts DESC
            LIMIT 1
            """
        ).fetchone()

    if row:
        latest_snapshot.update(dict(row))

    with db_connection() as connection:
        alert_row = connection.execute(
            """
            SELECT id, started_at, triggered_at, resolved_at, temperature, threshold, message, active
            FROM alerts
            ORDER BY triggered_at DESC
            LIMIT 1
            """
        ).fetchone()

    if alert_row:
        latest_alert.update(dict(alert_row))
        latest_alert["active"] = bool(alert_row["active"])
        open_alert_id = int(alert_row["id"]) if alert_row["active"] else None


def query_history(hours: int = 24) -> list[dict[str, object]]:
    cutoff = datetime.now(timezone.utc) - timedelta(hours=hours)
    with db_connection() as connection:
        rows = connection.execute(
            """
            SELECT ts, temperature, humidity, occupied, fan_on, comfort, state, threshold
            FROM readings
            WHERE ts >= ?
            ORDER BY ts ASC
            """,
            (cutoff.isoformat(),),
        ).fetchall()

    return [dict(row) for row in rows]


def query_recent(limit: int = 10) -> list[dict[str, object]]:
    with db_connection() as connection:
        rows = connection.execute(
            """
            SELECT ts, temperature, humidity, occupied, fan_on, comfort, state, threshold, comfort_profile
            FROM readings
            ORDER BY ts DESC
            LIMIT ?
            """,
            (limit,),
        ).fetchall()

    return [dict(row) for row in rows]


def query_recent_alerts(limit: int = 5) -> list[dict[str, object]]:
    with db_connection() as connection:
        rows = connection.execute(
            """
            SELECT started_at, triggered_at, resolved_at, temperature, threshold, message, active
            FROM alerts
            ORDER BY triggered_at DESC
            LIMIT ?
            """,
            (limit,),
        ).fetchall()

    return [dict(row) for row in rows]


def compute_summary(rows: list[dict[str, object]]) -> dict[str, float]:
    if not rows:
        return {
            "avg_temperature": 0.0,
            "avg_humidity": 0.0,
            "fan_runtime_minutes": 0.0,
            "occupancy_percent": 0.0,
            "energy_saved_minutes": 0.0,
        }

    avg_temperature = sum(row["temperature"] for row in rows) / len(rows)
    avg_humidity = sum(row["humidity"] for row in rows) / len(rows)
    occupied_count = sum(1 for row in rows if row["occupied"])
    fan_on_count = sum(1 for row in rows if row["fan_on"])
    empty_fan_off_count = sum(1 for row in rows if not row["occupied"] and not row["fan_on"])
    sample_minutes = TELEMETRY_INTERVAL_MINUTES

    return {
        "avg_temperature": round(avg_temperature, 2),
        "avg_humidity": round(avg_humidity, 2),
        "fan_runtime_minutes": round(fan_on_count * sample_minutes, 2),
        "occupancy_percent": round((occupied_count / len(rows)) * 100, 2),
        "energy_saved_minutes": round(empty_fan_off_count * sample_minutes, 2),
    }


def on_connect(client: mqtt.Client, _userdata, _flags, reason_code, _properties) -> None:
    print(f"[MQTT] Connected with reason code {reason_code}")
    client.subscribe(TOPIC_TELEMETRY)


def on_disconnect(_client: mqtt.Client, _userdata, reason_code, _properties=None) -> None:
    print(f"[MQTT] Disconnected with reason code {reason_code}")


def on_message(_client: mqtt.Client, _userdata, message: mqtt.MQTTMessage) -> None:
    try:
        payload = json.loads(message.payload.decode("utf-8"))
        insert_reading(payload)
        print(f"[MQTT] Logged reading at {latest_snapshot['ts']}")
    except (json.JSONDecodeError, UnicodeDecodeError, ValueError) as exc:
        print(f"[MQTT] Ignored invalid message: {exc}")


def start_mqtt() -> None:
    if MQTT_USER:
        mqtt_client.username_pw_set(MQTT_USER, MQTT_PASS)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_disconnect = on_disconnect
    mqtt_client.on_message = on_message
    mqtt_client.reconnect_delay_set(min_delay=1, max_delay=10)
    mqtt_client.connect_async(MQTT_HOST, MQTT_PORT, keepalive=60)
    mqtt_client.loop_start()


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/latest")
def api_latest():
    return jsonify(latest_snapshot)


@app.route("/api/history")
def api_history():
    hours = request.args.get("hours", default=24, type=int)
    hours = min(max(hours, 1), 24)
    return jsonify(query_history(hours=hours))


@app.route("/api/summary")
def api_summary():
    rows = query_history(hours=24)
    return jsonify(compute_summary(rows))


@app.route("/api/recent")
def api_recent():
    limit = request.args.get("limit", default=10, type=int)
    limit = min(max(limit, 1), 50)
    return jsonify(query_recent(limit=limit))


@app.route("/api/alerts")
def api_alerts():
    limit = request.args.get("limit", default=5, type=int)
    limit = min(max(limit, 1), 20)
    return jsonify({
        "latest": latest_alert,
        "recent": query_recent_alerts(limit=limit),
    })


@app.route("/api/threshold", methods=["POST"])
def api_threshold():
    return jsonify({
        "error": "Threshold is controlled by the hardware potentiometer in this version."
    }), 400


@app.route("/api/system-mode", methods=["POST"])
def api_system_mode():
    data = request.get_json(silent=True) or {}
    profile = str(data.get("mode", "")).strip()
    allowed = {"real", "demo_empty_cool", "demo_occupied_comfy", "demo_occupied_warm", "demo_occupied_hot"}

    if profile not in allowed:
        return jsonify({"error": "invalid system mode"}), 400

    ok, detail = publish_control_message(TOPIC_PROFILE_CONFIG, profile, retain=True)
    if not ok:
        return jsonify({"error": f"failed to publish system mode update to MQTT: {detail}"}), 502

    latest_snapshot["system_mode"] = profile
    return jsonify({"ok": True, "system_mode": profile})


def main() -> None:
    init_db()
    load_latest_snapshot()
    start_mqtt()
    app.run(host="0.0.0.0", port=5000, debug=False)


if __name__ == "__main__":
    main()
