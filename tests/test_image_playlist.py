import requests
import json

HOSTNAME = 'http://p1160.local'

def createUrl(path: str) -> str:
    p = path if path.startswith("/") else "/" + path
    return HOSTNAME + '/api/v1' + p

def commonApiRequest(url, method, payload = None):
    print(url)
    if type(payload) is dict:
        payload = json.dumps(payload, separators=(",", ":")).encode()
        print(payload)
    resp = requests.request(method=method, url=url, data=payload)
    print(f"Return code: {resp.status_code}")
    print(f"Content: {json.dumps(resp.json(), indent=2, sort_keys=False)}")



def test_gen1():
    commonApiRequest(createUrl('mode'), 'POST', {'mode': 'standby'})

    commonApiRequest(createUrl('img/playlist/add'), 'POST', {'name': 'DSC9060'})
    commonApiRequest(createUrl('img/playlist/add'), 'POST', {'name': 'DSC2765'})
    commonApiRequest(createUrl('img/playlist/add'), 'POST', {'name': 'DSC3330'})

    commonApiRequest(createUrl('img/playlist/del'), 'POST', {'name': 'DSC9060'})

    commonApiRequest(createUrl('img/playlist/get'), 'GET')

    commonApiRequest(createUrl('mode'), 'POST', {'mode': 'playlist', 'playlist': {'mode': 'select', 'duration': 30/60}})


def test_duplicate():
    commonApiRequest(createUrl('img/playlist/add'), 'POST', {'name': 'DSC9060'})
    commonApiRequest(createUrl('img/playlist/add'), 'POST', {'name': 'DSC9060'})


test_gen1()
# test_duplicate()