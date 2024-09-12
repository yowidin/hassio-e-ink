# app.py
from flask import Flask, send_from_directory, request
import json

app = Flask(__name__)

HOSTING = True


def as_json(obj):
    return json.dumps(obj)


@app.route('/')
def root():
    return send_from_directory('static', 'index.html')


@app.route('/<path:filename>')
def static_files(filename):
    return send_from_directory('static', filename)


@app.route('/status', methods=['POST'])
def status():
    return as_json({
        'mac_address': '00:11:22:33:44:55',
        'is_hosting': HOSTING,
    })


@app.route('/networks', methods=['POST'])
def networks():
    network_list = [
        {
            'ssid': 'Dummy #1',
            'mac': '00:11:22:33:44:55',
            'channel': '1',
            'rssi': '-52',
            'band': '2.4 GHz',
            'security': 'OPEN',
            'mfp': 'Disabled',
        },
        {
            'ssid': 'Long Dummy #2',
            'mac': '00:11:22:11:44:55',
            'channel': '7',
            'rssi': '-55',
            'band': '5 GHz',
            'security': 'WPA-PSK',
            'mfp': 'Disabled',
        },
        {
            'ssid': 'Dummy #3',
            'mac': 'BE:EF:22:11:44:55',
            'channel': '2',
            'rssi': '-65',
            'band': '[unknown]',
            'security': 'WEP',
            'mfp': 'Disabled',
        }
    ]
    return as_json({'networks': network_list})


@app.route('/wifi-config', methods=['POST'])
def wifi_config():
    config = request.get_json()
    if 'name' not in config or len(config['name']) == 0:
        return '{"result": "error", "message": "Missing network name"}'

    print(f"Config: {config}")
    return '{"result": "ok", "message": "OK"}'


@app.route('/image-server-config', methods=['POST'])
def image_server_config():
    config = request.get_json()

    # Address
    if 'address' not in config:
        return '{"result": "error", "message": "Missing server address"}'

    address = config['address']
    if len(address) == 0:
        return '{"result": "error", "message": "Bad server address"}'

    # Port
    if 'port' not in config:
        return '{"result": "error", "message": "Missing server port"}'

    port = int(config['port'])
    if port < 0 or port > 65535:
        return '{"result": "error", "message": "Bad server port"}'

    # Interval
    if 'interval' not in config:
        return '{"result": "error", "message": "Missing refresh interval"}'

    interval = int(config['interval'])
    if interval < 120 or interval > 3600:
        return '{"result": "error", "message": "Bad refresh interval"}'

    print(f"Config: {config}")
    return '{"result": "ok", "message": "OK"}'
