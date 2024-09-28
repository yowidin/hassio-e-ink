import argparse
import asyncio
import json
import io
import os

from selenium import webdriver
from selenium.webdriver.firefox.options import Options
from selenium.webdriver.support import expected_conditions
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.common.by import By

from PIL import Image

from heihost.log import Log


class CaptureConfig:
    @property
    def page_load_timeout_sec(self):
        return self.page_load_timeout / 1000.0

    @property
    def render_delay_sec(self):
        return self.render_delay / 1000.0

    def __init__(self, ha_base_url: str, ha_screenshot_url: str, ha_access_token: str, language: str = 'en',
                 width: int = 1200, height: int = 825, page_load_timeout: int = 10000, render_delay: int = 2000,
                 capture_interval: int = None, output_dir: str = None):

        self.ha_base_url = ha_base_url
        self.ha_screenshot_url = ha_screenshot_url
        self.ha_access_token = ha_access_token
        self.language = language
        self.width = width
        self.height = height
        self.page_load_timeout = page_load_timeout
        self.render_delay = render_delay
        self.capture_interval = capture_interval
        self.output_dir = output_dir

        if self.width % 2 != 0:
            raise ValueError("Image width must be even to combine 4-bit values")

        # Remove conflicting slashes
        while self.ha_base_url.endswith('/'):
            self.ha_base_url = self.ha_base_url[:-1]

        while self.ha_screenshot_url.startswith('/'):
            self.ha_screenshot_url = self.ha_screenshot_url[1:]

    @staticmethod
    def add_arguments(parser: argparse.ArgumentParser):
        parser.add_argument('--base-url', required=True, type=str, help='Base URL of the Home Assistant instance')
        parser.add_argument('--screenshot-url', required=True, type=str, help='Relative URL to take the screenshot of')
        parser.add_argument('--access-token', required=True, type=str,
                            help='Long-lived access token from Home Assistant')
        parser.add_argument('--language', default='en', type=str, help='Language to set in the Home Assistant')
        parser.add_argument('--width', default=1200, type=int, help='Screenshot width')
        parser.add_argument('--height', default=825, type=int, help='Screenshot height')
        parser.add_argument('--load-timeout', default=10000, type=int,
                            help='How long to wait until the page loading is done (ms)')
        parser.add_argument('--render-delay', default=2000, type=int,
                            help='How long to wait between navigating to the HA page and taking the screenshot (ms)')
        parser.add_argument('--capture-interval', type=int,
                            help='Optional capture interval (sec). A single screenshot will be captured if not set.')
        parser.add_argument('--output-dir', type=str, help='Directory to store the captured images')

    @staticmethod
    def from_args(args) -> 'CaptureConfig':
        return CaptureConfig(args.base_url, args.screenshot_url, args.access_token, args.language, args.width,
                             args.height, args.load_timeout, args.render_delay, args.capture_interval, args.output_dir)


class ImageCapture:
    def __init__(self, config: CaptureConfig):
        self.config = config
        self.screenshot_url = f'{self.config.ha_base_url}/{self.config.ha_screenshot_url}'
        self.latest_screenshot = None
        self.total_screenshots = 0

        self.firefox_options = Options()
        self.firefox_options.add_argument("--headless")
        self.firefox_options.set_preference('ui.systemUsesDarkTheme', 0)

        self.driver = None

    @property
    def hass_tokens(self):
        return {"hassUrl": self.config.ha_base_url, "access_token": self.config.ha_access_token, "token_type": "Bearer"}

    async def _navigate(self, url: str):
        Log.debug(f'Navigating to {url}')
        await asyncio.to_thread(self.driver.get, url)
        await asyncio.to_thread(WebDriverWait(self.driver, self.config.page_load_timeout_sec).until,
                                expected_conditions.presence_of_element_located((By.TAG_NAME, "body")))

    async def _execute_script(self, script: str):
        await asyncio.to_thread(self.driver.execute_script, script)

    async def _authenticate(self):
        await self._navigate(self.config.ha_base_url)
        await self._execute_script(f'''
            localStorage.setItem('hassTokens', '{json.dumps(self.hass_tokens)}');
            localStorage.setItem('selectedLanguage', '{json.dumps(self.config.language)}');
        ''')

    async def _set_light_theme(self):
        await self._execute_script('''
                const setLightMode = window.matchMedia('(prefers-color-scheme: light)').matches;
                Object.defineProperty(window, 'matchMedia', {
                    value: (query) => {
                        return {
                            matches: query === '(prefers-color-scheme: light)',
                            media: query,
                            onchange: null,
                            addListener: () => {},
                            removeListener: () => {},
                        };
                    }
                });''')

    async def _setup_viewport(self):
        await self._execute_script(f'''
            window.scrollTo(0, 0);
            document.body.style.overflow = 'hidden';
            document.documentElement.style.overflow = 'hidden';
            document.body.style.width = '{self.config.width}px';
            document.body.style.height = '{self.config.height}px';
            document.documentElement.style.width = '{self.config.width}px';
            document.documentElement.style.height = '{self.config.height}px';
        ''')

        # Wait for possible animations to complete
        await asyncio.sleep(self.config.render_delay_sec)

    async def _setup_screenshot_page(self):
        await self._navigate(self.screenshot_url)
        await self._setup_viewport()
        await self._set_light_theme()

    def _close_driver(self):
        if self.driver is not None:
            self.driver.quit()
            self.driver = None

    async def _initial_setup(self):
        try:
            self.driver = webdriver.Firefox(options=self.firefox_options)
            self.driver.set_window_size(self.config.width + 100, self.config.height + 100)
            await self._authenticate()
            await self._setup_screenshot_page()
        except Exception as e:
            Log.error(f'Capture setup failed: {e}')
            self._close_driver()

    async def _store_current_image(self):
        if self.config.output_dir is None:
            return

        output_path = os.path.join(self.config.output_dir, f'image_{self.total_screenshots:03}.png')
        self.total_screenshots += 1

        await asyncio.to_thread(self.latest_screenshot.save, output_path)
        Log.info(f'Image stored: {output_path}')

    async def _grab_screenshot(self):
        if self.driver is None:
            # Setup failed as well: nothing to do here
            Log.info(f'No driver available, skipping')

        try:
            screenshot = await asyncio.to_thread(self.driver.get_full_page_screenshot_as_png)
            image = Image.open(io.BytesIO(screenshot))

            self.latest_screenshot = await asyncio.to_thread(image.crop, (0, 0, self.config.width, self.config.height))

            await self._store_current_image()

        except Exception as e:
            Log.error(f'Screen capture failed: {e}')
            self._close_driver()

    async def _capture_once(self):
        if self.driver is None:
            await self._initial_setup()

        await self._grab_screenshot()

    async def run(self):
        if self.config.capture_interval is None:
            return await self._capture_once()

        try:
            while True:
                await self._capture_once()
                await asyncio.sleep(self.config.capture_interval)
        except Exception as e:
            Log.error(f'Image capture exception: {e}')
            self._close_driver()


async def main():
    parser = argparse.ArgumentParser('Home Assistant Image Capture')
    Log.add_args(parser)

    CaptureConfig.add_arguments(parser)

    args = parser.parse_args()
    Log.setup(args)

    config = CaptureConfig.from_args(args)
    capture = ImageCapture(config)

    await capture.run()


def sync_main():
    asyncio.run(main())


if __name__ == '__main__':
    sync_main()
