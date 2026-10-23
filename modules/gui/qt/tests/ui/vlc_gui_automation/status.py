import urllib.request
import urllib.error
import urllib.parse
import xml.etree.ElementTree as ET
import socket
import logging


def find_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
    return port


class HttpStatus:
 
    def __init__(self, port, password="vlc"):
        self.base_url = f"http://127.0.0.1:{port}/requests/status.xml"
        self.password = password
        password_mgr = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        password_mgr.add_password(None, self.base_url, "", self.password)
        handler = urllib.request.HTTPBasicAuthHandler(password_mgr)
        self.opener = urllib.request.build_opener(handler)

    def _fetch_status(self):
        try:
            with self.opener.open(self.base_url, timeout=2) as response:
                return ET.parse(response).getroot()
        except urllib.error.URLError as e:
            logging.debug(f"HTTP request failed: {e}")
            return None
        except ET.ParseError as e:
            logging.debug(f"XML parse failed: {e}")
            return None
    
    def play_file(self, filepath: str):
        url = f"{self.base_url}?command=in_play&input={urllib.parse.quote(filepath)}"
        try:
            with self.opener.open(url, timeout=5) as response:
                return response.read()
        except urllib.error.URLError as e:
            logging.debug(f"HTTP in_play failed: {e}")
            return None

    def send_command(self, command, **params):
        query = urllib.parse.urlencode({"command": command, **params})
        url = f"{self.base_url}?{query}"
        try:
            with self.opener.open(url, timeout=5) as response:
                return response.read()
        except urllib.error.URLError as e:
            logging.debug(f"HTTP command failed: {e}")
            return None
    
    @staticmethod
    def _get_text(root, tag):
        if root is None:
            return None
        elem = root.find(tag)
        return elem.text if elem is not None else None

    def get_state(self):
        return self._get_text(self._fetch_status(), "state")

    def get_volume(self):
        val = self._get_text(self._fetch_status(), "volume")
        return int(val) if val else None

    def get_time(self):
        val = self._get_text(self._fetch_status(), "time")
        return int(float(val)) if val else None

    def get_position(self):
        val = self._get_text(self._fetch_status(), "position")
        if val is None:
            return None
        parsed = float(val)
        return parsed if parsed >= 0 else None

    def get_current_file_id(self):
        val = self._get_text(self._fetch_status(), "currentplid")
        return int(val) if val else None

    def get_current_filename(self):
        root = self._fetch_status()
        if root is None:
            return None
        information = root.find("information")
        if information is None:
            return None
        for category in information.findall("category"):
            if category.get("name") == "meta":
                for info in category.findall("info"):
                    if info.get("name") == "filename":
                        return info.text
        return None

    def get_loop(self):
        val = self._get_text(self._fetch_status(), "loop")
        return val == "true" if val else None

    def get_random(self):
        val = self._get_text(self._fetch_status(), "random")
        return val == "true" if val else None

    def get_rate(self):
        val = self._get_text(self._fetch_status(), "rate")
        return float(val) if val else None
    
    def get_fullscreen(self):
        val = self._get_text(self._fetch_status(), "fullscreen")
        if val is None:
            return None
        return 1 if val == "true" else 0