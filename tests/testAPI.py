import pytest
import requests
import json
import click


def createUrl(base_url: str, path: str) -> str:
    base = base_url.rstrip("/")
    p = path if path.startswith("/") else "/" + path
    return base + '/api/v1' + p

def commonApiRequest(url, method, payload = None):
    print(url)
    resp = requests.request(method=method, url=url, json=payload)
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

########## DISPLAY GROUP ##########
@cli.group('display')
@click.pass_context
def display(ctx: click.Context):
    pass

@display.command()
@click.option('-c', '--checker-size', default=3, required=True, show_default=True, help='The checker pattern size as a power of 2 (so 3=8pixels)')
@click.pass_context
def check(ctx: click.Context, checker_size: int) -> None:
    url = createUrl(ctx.obj['url'], 'dispCheckPattern')
    commonApiRequest(url, 'POST', {'checkSize': checker_size})

if __name__ == "__main__":
    cli(obj={})