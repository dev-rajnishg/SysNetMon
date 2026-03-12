import json
import logging
import os
import socket
import threading
import time
from copy import deepcopy
from datetime import datetime, timezone

try:
    import boto3
except ImportError:  # pragma: no cover - optional at runtime
    boto3 = None


LOGGER = logging.getLogger(__name__)


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class AwsAlertPublisher:
    def __init__(self) -> None:
        self.bucket = os.getenv("ALERT_S3_BUCKET", "")
        self.topic_arn = os.getenv("ALERT_SNS_TOPIC_ARN", "")
        self.prefix = os.getenv("ALERT_S3_PREFIX", "alerts/")
        self.region = os.getenv("AWS_REGION", "ap-south-1")
        self.last_result = {
            "timestamp": utc_timestamp(),
            "uploaded": False,
            "message": "AWS upload idle",
        }

        if boto3 and (self.bucket or self.topic_arn):
            self.s3_client = boto3.client("s3", region_name=self.region)
            self.sns_client = boto3.client("sns", region_name=self.region) if self.topic_arn else None
        else:
            self.s3_client = None
            self.sns_client = None

    def upload_alert(self, alert: dict) -> dict:
        if not boto3:
            self.last_result = {
                "timestamp": utc_timestamp(),
                "uploaded": False,
                "message": "boto3 not installed in dashboard environment",
            }
            return self.last_result

        if not self.bucket and not self.topic_arn:
            self.last_result = {
                "timestamp": utc_timestamp(),
                "uploaded": False,
                "message": "Set ALERT_S3_BUCKET or ALERT_SNS_TOPIC_ARN to enable AWS uploads",
            }
            return self.last_result

        payload = json.dumps(alert, indent=2)
        result = {
            "timestamp": utc_timestamp(),
            "uploaded": True,
            "message": "Alert published",
            "s3_key": None,
        }

        try:
            if self.bucket and self.s3_client:
                host = alert.get("host", "unknown-host")
                key = f"{self.prefix}{host}-{int(time.time())}.json"
                self.s3_client.put_object(
                    Bucket=self.bucket,
                    Key=key,
                    Body=payload.encode("utf-8"),
                    ContentType="application/json",
                )
                result["s3_key"] = key

            if self.topic_arn and self.sns_client:
                self.sns_client.publish(
                    TopicArn=self.topic_arn,
                    Subject=f"SysNetMon alert for {alert.get('host', 'unknown-host')}",
                    Message=payload,
                )
        except Exception as exc:  # pragma: no cover - networked dependency
            LOGGER.exception("AWS upload failed")
            result = {
                "timestamp": utc_timestamp(),
                "uploaded": False,
                "message": str(exc),
            }

        self.last_result = result
        return result


class MonitorConnector:
    def __init__(self, server_host: str, server_port: int, publisher: AwsAlertPublisher) -> None:
        self.server_host = server_host
        self.server_port = server_port
        self.publisher = publisher
        self._lock = threading.Lock()
        self._socket = None
        self._thread = None
        self._running = False
        self.state = {
            "server": {"connected": False, "host": server_host, "port": server_port, "last_event": None},
            "metrics": {},
            "alerts": [],
            "chat": [],
            "rules": [],
            "aws": deepcopy(self.publisher.last_result),
        }

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._running = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        if self._socket:
            try:
                self._socket.close()
            except OSError:
                pass

    def snapshot(self) -> dict:
        with self._lock:
            snapshot = deepcopy(self.state)
        snapshot["generated_at"] = utc_timestamp()
        return snapshot

    def send_command(self, text: str) -> None:
        self._send({"type": "command", "text": text})

    def send_chat(self, sender: str, text: str) -> None:
        self._send({"type": "chat", "from": sender, "text": text})

    def upload_latest_alert(self) -> dict:
        with self._lock:
            latest = deepcopy(self.state["alerts"][-1]) if self.state["alerts"] else None
        if not latest:
            return {
                "timestamp": utc_timestamp(),
                "uploaded": False,
                "message": "No alerts available to upload",
            }
        result = self.publisher.upload_alert(latest)
        with self._lock:
            self.state["aws"] = deepcopy(result)
        return result

    def _send(self, message: dict) -> None:
        raw = json.dumps(message) + "\n"
        with self._lock:
            sock = self._socket
        if not sock:
            raise RuntimeError("Dashboard is not connected to the C++ server")
        sock.sendall(raw.encode("utf-8"))

    def _run(self) -> None:
        while self._running:
            try:
                with socket.create_connection((self.server_host, self.server_port), timeout=5) as sock:
                    sock.sendall(
                        (json.dumps({"type": "register", "role": "dashboard", "host": "flask-dashboard"}) + "\n").encode("utf-8")
                    )
                    sock.settimeout(2.0)
                    with self._lock:
                        self._socket = sock
                        self.state["server"]["connected"] = True
                    self._consume(sock)
            except Exception as exc:  # pragma: no cover - network behavior is environmental
                LOGGER.warning("Dashboard connector retrying: %s", exc)
                with self._lock:
                    self._socket = None
                    self.state["server"]["connected"] = False
                time.sleep(3)

    def _consume(self, sock: socket.socket) -> None:
        buffer = ""
        while self._running:
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    raise ConnectionResetError("server closed the connection")
                buffer += chunk.decode("utf-8")
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    if line.strip():
                        self._handle_event(json.loads(line))
            except socket.timeout:
                continue

    def _handle_event(self, event: dict) -> None:
        event_type = event.get("type")
        now = utc_timestamp()
        with self._lock:
            self.state["server"]["last_event"] = now
            if event_type == "snapshot":
                self.state["metrics"] = {item["host"]: item for item in event.get("metrics", [])}
            elif event_type == "metric":
                metric = event.get("data", {})
                if metric.get("host"):
                    self.state["metrics"][metric["host"]] = metric
            elif event_type == "alert":
                alert = event.get("data", {})
                self.state["alerts"].append(alert)
                self.state["alerts"] = self.state["alerts"][-50:]
                self.state["aws"] = deepcopy(self.publisher.upload_alert(alert))
            elif event_type == "chat":
                self.state["chat"].append(
                    {
                        "from": event.get("from", "unknown"),
                        "text": event.get("text", ""),
                        "timestamp": event.get("timestamp", now),
                    }
                )
                self.state["chat"] = self.state["chat"][-80:]
            elif event_type == "rules":
                self.state["rules"] = event.get("items", [])
            elif event_type in {"ack", "error"}:
                self.state["chat"].append(
                    {"from": event_type, "text": event.get("message", ""), "timestamp": now}
                )
                self.state["chat"] = self.state["chat"][-80:]