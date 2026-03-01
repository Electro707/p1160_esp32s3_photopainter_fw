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

@display.command('checker')
@click.option('-c', '--checker-size', default=3, required=True, show_default=True, help='The checker pattern size as a power of 2 (so 3=8pixels)')
@click.pass_context
def checkerPattern(ctx: click.Context, checker_size: int) -> None:
    """
    Sets the display framebuffer to a checker pattern. Used for testing
    """
    url = createUrl(ctx.obj['url'], 'disp/setCheckPattern')
    commonApiRequest(url, 'POST', {'checkSize': checker_size})

@display.command()
@click.argument('image', type=Path)
@click.pass_context
def setFb(ctx: click.Context, image: Path) -> None:
    """
    Uploads the image given to the device's framebuffer
    """
    rawFb = imageProcessor.createFBFromImage(image)
    url = createUrl(ctx.obj['url'], 'disp/setFb')
    commonApiRequest(url, 'POST', rawFb)

@display.command(name='update')
@click.pass_context
def updateDisplay(ctx: click.Context) -> None:
    """
    Updates the display from the internal framebuffer
    """
    url = createUrl(ctx.obj['url'], 'disp/update')
    commonApiRequest(url, 'POST')

@display.command(name='getAvailable')
@click.pass_context
def getAvailableImg(ctx: click.Context) -> None:
    """
    Gets all available images from the device
    """
    url = createUrl(ctx.obj['url'], 'img/available')
    commonApiRequest(url, 'GET')

@display.command(name='uploadImage')
@click.argument('image', type=Path)
@click.argument('name', type=str)
@click.pass_context
def uploadImage(ctx: click.Context, image: Path, name: str) -> None:
    """
    Uploads the image IMAGE given to the device and saves it as a NAME
    """
    rawFb = imageProcessor.createFBFromImage(image)
    url = createUrl(ctx.obj['url'], 'img/upload')
    commonApiRequest(url, 'POST', rawFb)

    url = createUrl(ctx.obj['url'], 'img/save')
    commonApiRequest(url, 'POST', {'name': name})

@display.command(name='loadImage')
@click.argument('name', type=str)
@click.pass_context
def loadImage(ctx: click.Context, name: str) -> None:
    """
    Loads an image on-device with the name NAME
    """
    url = createUrl(ctx.obj['url'], 'img/load')
    commonApiRequest(url, 'POST', {'name': name})

@display.command(name='deleteImage')
@click.argument('name', type=str)
@click.pass_context
def delImage(ctx: click.Context, name: str) -> None:
    """
    Deletes an image on-device with the name NAME
    """
    url = createUrl(ctx.obj['url'], 'img/delete')
    commonApiRequest(url, 'POST', {'name': name})

if __name__ == "__main__":
    cli(obj={})