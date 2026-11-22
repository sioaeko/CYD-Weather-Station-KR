#!/usr/bin/env python3
"""설정 웹 페이지를 보드 없이 PC 에서 띄워 보는 가짜 기기 서버.

    python tools/web_mock.py                 # http://127.0.0.1:8765
    python tools/web_mock.py --ap            # Wi-Fi 설정 모드 흉내
    python tools/web_mock.py --locked 1234   # 관리 비밀번호가 걸린 기기 흉내

펌웨어(WebPortal.cpp)와 같은 /api 응답 모양을 돌려줍니다. 지역 이름 글꼴 확인은
CYD_Weather_KR/KoreanFonts.h 의 16px 글꼴에서 실제 글자 목록을 읽어 씁니다.
"""
import argparse
import json
import re
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent.parent
PAGE = ROOT / "web" / "index.html"


def load_presets():
    text = (ROOT / "CYD_Weather_KR" / "All_Settings.h").read_text(encoding="utf-8")
    return [list(m) for m in re.findall(r'\{"([^"]+)",\s*"([-\d.]+)",\s*"([-\d.]+)"\}', text)]


def load_glyphs():
    text = (ROOT / "CYD_Weather_KR" / "KoreanFonts.h").read_text(encoding="utf-8")
    body = text.split("KR_FONT_MEDIUM[] PROGMEM = {", 1)[1].split("};", 1)[0]
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", body))
    count = int.from_bytes(data[0:4], "big")
    return {int.from_bytes(data[24 + i * 28:28 + i * 28], "big") for i in range(count)}


PRESETS, GLYPHS = load_presets(), load_glyphs()
BOOT = time.time()


class Device:
    def __init__(self, ap, locked):
        self.ap = ap
        self.settings = dict(preset=0, customName="", customLat="", customLon="", onHour=8, offHour=23,
                             dayBrightness=255, nightBrightness=0, schedule=True, landscape=True, autoCycle=False, invertColors=True, flipped=False, hour12=False,
                             wifiSsid="" if ap else "우리집_2.4G", locked=bool(locked),
                             apiKeySet=not ap, apiKeyEnd="" if ap else "9f3c")
        self.admin = locked or ""
        self.refresh_until = 0
        self.scan_started = 0

    def place(self):
        s = self.settings
        if s["preset"] == -1:
            return s["customName"], s["customLat"], s["customLon"]
        return tuple(PRESETS[s["preset"]])

    def state(self):
        now = int(time.time())
        name, lat, lon = self.place()
        online = not self.ap
        refreshing = time.time() < self.refresh_until
        midnight = now - (now + 32400) % 86400
        return {
            "device": {"firmware": "KR 1.2 (mock)", "uptime": int(time.time() - BOOT) + 3 * 86400 + 18000, "heapKb": 97,
                       "host": "cyd-weather-kr.local", "online": online, "ip": "192.168.0.23" if online else "",
                       "ssid": self.settings["wifiSsid"] if online else "", "rssi": -58 if online else 0,
                       "ap": self.ap, "apName": "CYD-Weather-1A2B", "apIp": "192.168.4.1" if self.ap else "",
                       "time": now, "utcOffset": 32400},
            "weather": ({"place": name, "lat": lat, "lon": lon, "valid": True, "temp": 4.2, "feels": 1.3,
                         "condition": "구름 많음", "humidity": 58, "wind": 3.4, "windDir": "북서",
                         "sunrise": midnight + 7 * 3600 + 20 * 60, "sunset": midnight + 17 * 3600 + 15 * 60,
                         "updated": now - 240, "pm10": 42, "pm25": 23, "uv": 1.8, "dustGrade": 1, "pop": 20,
                         "status": "날씨 받는 중" if refreshing else "모든 정보가 최신입니다", "refreshing": refreshing,
                         "next": 0 if refreshing else 612, "http": {"current": 200, "forecast": 200, "air": 200}}
                        if online else
                        {"place": name, "lat": lat, "lon": lon, "valid": False, "status": "Wi-Fi 설정이 필요합니다",
                         "refreshing": False, "next": 0, "http": {"current": -100, "forecast": -100, "air": -100}}),
            "settings": dict(self.settings, locked=bool(self.admin)),
            "presets": PRESETS,
        }


class Handler(BaseHTTPRequestHandler):
    device: Device = None

    def log_message(self, *args):
        pass

    def send(self, code, obj=None, body=None, ctype="application/json; charset=utf-8"):
        data = body if body is not None else json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def error(self, code, message, field=None):
        self.send(code, {"error": "invalid", "message": message, **({"field": field} if field else {})})

    def authorized(self):
        if not self.device.admin or self.headers.get("X-Admin-Password") == self.device.admin:
            return True
        self.send(401, {"error": "auth", "message": "관리 비밀번호가 필요합니다"})
        return False

    def do_GET(self):
        url = urlparse(self.path)
        time.sleep(0.12)
        if url.path == "/":
            return self.send(200, body=PAGE.read_bytes(), ctype="text/html; charset=utf-8")
        if url.path == "/phone":  # 스크린샷용: 휴대폰 폭(390px) 틀 안에 설정 페이지
            height = parse_qs(url.query).get("h", ["1700"])[0]
            frame = f"<!doctype html><body style='margin:0;background:#0b0c0b'><iframe src='/' width='390' height='{int(height)}' style='border:0;display:block'></iframe>"
            return self.send(200, body=frame.encode(), ctype="text/html; charset=utf-8")
        if not url.path.startswith("/api/"):
            return self.error(404, "없는 주소입니다")
        if not self.authorized():
            return
        if url.path == "/api/state":
            return self.send(200, self.device.state())
        if url.path == "/api/glyphs":
            text = parse_qs(url.query).get("text", [""])[0]
            missing = "".join(dict.fromkeys(ch for ch in text if ch != " " and ord(ch) not in GLYPHS))
            return self.send(200, {"missing": missing})
        if url.path == "/api/scan":
            if not self.device.scan_started:
                self.device.scan_started = time.time()
                return self.send(200, {"scanning": True})
            if time.time() - self.device.scan_started < 2:
                return self.send(200, {"scanning": True})
            self.device.scan_started = 0
            return self.send(200, {"scanning": False, "networks": [
                {"ssid": "우리집_2.4G", "rssi": -48, "secure": True, "channel": 6},
                {"ssid": "iptime", "rssi": -63, "secure": True, "channel": 1},
                {"ssid": "KT_GiGA_2G_Wave2_7F3A", "rssi": -71, "secure": True, "channel": 11},
                {"ssid": "SK_WiFiGIGA1C2D", "rssi": -80, "secure": True, "channel": 3},
                {"ssid": "Free_CU_WiFi", "rssi": -86, "secure": False, "channel": 9}]})
        return self.error(404, "없는 주소입니다")

    def do_POST(self):
        url = urlparse(self.path)
        time.sleep(0.15)
        if not self.authorized():
            return
        length = int(self.headers.get("Content-Length") or 0)
        try:
            body = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            return self.error(400, "요청 형식이 올바르지 않습니다")
        d = self.device
        if url.path == "/api/settings":
            s = dict(d.settings)
            if "preset" in body:
                s["preset"] = int(body["preset"])
            if any(k in body for k in ("customName", "customLat", "customLon")):
                name = str(body.get("customName", "")).strip()
                if not name or len(name) > 10:
                    return self.error(422, "이름은 1~10자로 적어 주세요", "customName")
                try:
                    lat = float(body.get("customLat")); assert -90 <= lat <= 90
                except Exception:
                    return self.error(422, "위도는 -90~90 사이 숫자입니다 (예: 37.5665)", "customLat")
                try:
                    lon = float(body.get("customLon")); assert -180 <= lon <= 180
                except Exception:
                    return self.error(422, "경도는 -180~180 사이 숫자입니다 (예: 126.9780)", "customLon")
                s.update(customName=name, customLat=f"{lat:.4f}", customLon=f"{lon:.4f}", preset=-1)
            for key in ("onHour", "offHour", "dayBrightness", "nightBrightness", "schedule", "landscape", "autoCycle", "invertColors", "flipped", "hour12"):
                if key in body:
                    s[key] = body[key]
            if "apiKey" in body:
                k = str(body["apiKey"]).strip().lower()
                if not re.fullmatch(r"[0-9a-f]{32}", k):
                    return self.error(422, "API 키는 영문·숫자 32자리입니다", "apiKey")
                s.update(apiKeySet=True, apiKeyEnd=k[-4:])
            if "adminPassword" in body:
                p = body["adminPassword"]
                if p and len(p) < 4:
                    return self.error(422, "비밀번호는 4자 이상으로 정해 주세요", "adminPassword")
                d.admin = p
            d.settings = s
            return self.send(200, d.state())
        if url.path == "/api/refresh":
            d.refresh_until = time.time() + 4
            return self.send(200, {"started": True})
        if url.path == "/api/wifi":
            ssid, password = body.get("ssid", ""), body.get("password", "")
            if not ssid:
                return self.error(422, "Wi-Fi 이름을 입력해 주세요 (32자 이하)", "ssid")
            if password and len(password) < 8:
                return self.error(422, "Wi-Fi 비밀번호는 8~63자입니다", "password")
            d.settings["wifiSsid"] = ssid
            d.ap = False
            return self.send(200, {"restarting": True, "ssid": ssid})
        if url.path in ("/api/calibrate", "/api/reboot"):
            return self.send(200, {"started": True, "restarting": url.path.endswith("reboot")})
        return self.error(404, "없는 주소입니다")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--ap", action="store_true", help="Wi-Fi 설정 모드 흉내")
    parser.add_argument("--locked", default="", help="관리 비밀번호")
    args = parser.parse_args()
    Handler.device = Device(args.ap, args.locked)
    print(f"가짜 기기: http://127.0.0.1:{args.port}  (프리셋 {len(PRESETS)}개, 글꼴 글자 {len(GLYPHS)}개)")
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
