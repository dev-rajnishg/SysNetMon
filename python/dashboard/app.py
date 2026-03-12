import os

from flask import Flask, jsonify, render_template, request

from services import AwsAlertPublisher, MonitorConnector


app = Flask(__name__)

connector = MonitorConnector(
    server_host=os.getenv("MONITOR_SERVER_HOST", "127.0.0.1"),
    server_port=int(os.getenv("MONITOR_SERVER_PORT", "9090")),
    publisher=AwsAlertPublisher(),
)
connector.start()


@app.route("/")
def index():
    return render_template("index.html")


@app.get("/api/state")
def state():
    return jsonify(connector.snapshot())


@app.post("/api/send-command")
def send_command():
    payload = request.get_json(silent=True) or {}
    text = str(payload.get("text", "")).strip()
    if not text:
        return jsonify({"ok": False, "message": "Command text is required"}), 400
    connector.send_command(text)
    return jsonify({"ok": True, "message": "Command sent", "text": text})


@app.post("/api/chat")
def send_chat():
    payload = request.get_json(silent=True) or {}
    sender = str(payload.get("from", "dashboard")).strip() or "dashboard"
    text = str(payload.get("text", "")).strip()
    if not text:
        return jsonify({"ok": False, "message": "Chat text is required"}), 400
    connector.send_chat(sender, text)
    return jsonify({"ok": True, "message": "Chat sent"})


@app.post("/api/upload-latest-alert")
def upload_latest_alert():
    return jsonify(connector.upload_latest_alert())


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=int(os.getenv("PORT", "5000")), debug=True)