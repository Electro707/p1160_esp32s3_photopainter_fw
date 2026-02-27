import pytest
import requests
import json
import click
from pathlib import Path
import imageProcessor

def createUrl(base_url: str, path: str) -> str:
    base = base_url.rstrip("/")
    p = path if path.startswith("/") else "/" + path
    return base + '/api/v1' + p

def commonApiRequest(url, method, payload = None):
    print(url)
    if type(payload) is dict:
        payload = json.dumps(payload, separators=(",", ":")).encode()
    resp = requests.request(method=method, url=url, data=payload)
    print(f"Return code: {resp.status_code}")
    print(f"Content: {json.dumps(resp.json(), indent=2, sort_keys=False)}")


########## TOP LEVEL GROUP ##########
@click.group
@click.option('--base-url', default='192.168.4.1', show_default=True, help='The hostname of the esp32')
@click.pass_context
def cli(ctx: click.Context, base_url: str):
    ctx.obj['url'] = 'http://' + base_url


@cli.command()
@click.pass_context
def version(ctx: click.Context) -> None:
    url = createUrl(ctx.obj['url'], 'version')
    commonApiRequest(url, 'GET')

@cli.command()
@click.pass_context
def coffee(ctx: click.Context) -> None:
    url = createUrl(ctx.obj['url'], 'coffee')
    commonApiRequest(url, 'GET')

########## WIFI GROUP ##########
@cli.group('wifi')
@click.pass_context
def wifi(ctx: click.Context):
    pass

@wifi.command()
@click.pass_context
def getInfo(ctx: click.Context) -> None:
    url = createUrl(ctx.obj['url'], 'wifiInfo')
    commonApiRequest(url, 'GET')

@wifi.command()
@click.argument('wifi-ssid')
@click.argument('wifi-pass')
@click.pass_context
def setInfo(ctx: click.Context, wifi_ssid, wifi_pass) -> None:
    url = createUrl(ctx.obj['url'], 'wifiInfo')
    payload = {'ssid': wifi_ssid, 'pass': wifi_pass}
    commonApiRequest(url, 'POST', payload)

########## DISPLAY AND IMAGE GROUP ##########
@cli.group('display')
@click.pass_context
def display(ctx: click.Context):
    pass

@display.command()
@click.option('-c', '--checker-size', default=3, required=True, show_default=True, help='The checker pattern size as a power of 2 (so 3=8pixels)')
@click.pass_context
def checkerPattern(ctx: click.Context, checker_size: int) -> None:
    url = createUrl(ctx.obj['url'], 'dispCheckPattern')
    commonApiRequest(url, 'POST', {'checkSize': checker_size})

@display.command()
@click.option('-i', '--image', type=Path, required=True, show_default=True, help='The image to load. Could be anything PIL supports')
@click.pass_context
def setFb(ctx: click.Context, image: Path) -> None:
    rawFb = imageProcessor.createFBFromImage(image)
    url = createUrl(ctx.obj['url'], 'setDisplayFb')
    commonApiRequest(url, 'POST', rawFb)

@display.command()
@click.pass_context
def updateDisplay(ctx: click.Context) -> None:
    url = createUrl(ctx.obj['url'], 'updateDisplay')
    commonApiRequest(url, 'POST')

@display.command()
@click.pass_context
def getAvailableImg(ctx: click.Context) -> None:
    url = createUrl(ctx.obj['url'], 'img/available')
    commonApiRequest(url, 'GET')

@display.command()
@click.option('-i', '--image', type=str, required=True, show_default=True, help='The image name to save on the device')
@click.pass_context
def saveImage(ctx: click.Context, image: str) -> None:
    url = createUrl(ctx.obj['url'], 'img/save')
    commonApiRequest(url, 'POST', {'imgName': image})

@display.command()
@click.option('-i', '--image', type=str, required=True, show_default=True, help='The image name to save on the device')
@click.pass_context
def loadImage(ctx: click.Context, image: str) -> None:
    url = createUrl(ctx.obj['url'], 'img/load')
    commonApiRequest(url, 'POST', {'imgName': image})

if __name__ == "__main__":
    cli(obj={})