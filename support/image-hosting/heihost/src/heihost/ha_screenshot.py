import argparse
import io
import time
import json

from selenium import webdriver
from selenium.webdriver.firefox.options import Options
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC

from PIL import Image

import numpy as np


class CaptureConfig:
    @property
    def page_load_timeout_sec(self):
        return self.page_load_timeout / 1000.0

    @property
    def render_delay_sec(self):
        return self.render_delay / 1000.0

    def __init__(self, ha_base_url: str, ha_screenshot_url: str, ha_access_token: str, language: str = 'en',
                 width: int = 1200, height: int = 825, page_load_timeout: int = 10000, render_delay: int = 2000):

        self.ha_base_url = ha_base_url
        self.ha_screenshot_url = ha_screenshot_url
        self.ha_access_token = ha_access_token
        self.language = language
        self.width = width
        self.height = height
        self.page_load_timeout = page_load_timeout
        self.render_delay = render_delay

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

    @staticmethod
    def from_args(args) -> 'CaptureConfig':
        return CaptureConfig(args.base_url, args.screenshot_url, args.access_token, args.language, args.width,
                             args.height, args.load_timeout, args.render_delay)


def convert_to_4bit_grayscale(image):
    grayscale = image.convert('L')
    np_image = np.array(grayscale)

    # Create a 4-bit (16 levels) grayscale palette
    palette = np.arange(0, 256, 256 // 16, dtype=np.uint8)

    # Map each pixel to the nearest palette color
    quantized = np.digitize(np_image, palette) - 1

    # Scale back to 0-255 range
    scaled = quantized * (255 // 15)
    return Image.fromarray(scaled.astype(np.uint8), mode='L')


def capture_screenshot(cfg: CaptureConfig, output_path: str = None):
    options = Options()
    options.add_argument("--headless")  # Run in headless mode (no GUI)

    # Initialize the WebDriver
    driver = webdriver.Firefox(options=options)

    try:
        driver.set_window_size(cfg.width + 100, cfg.height + 100)

        print(f'Navigating to {cfg.ha_base_url}')
        driver.get(cfg.ha_base_url)
        WebDriverWait(driver, cfg.page_load_timeout_sec).until(
            EC.presence_of_element_located((By.TAG_NAME, "body"))
        )

        # Prepare the authentication data
        hass_tokens = {
            "hassUrl": cfg.ha_base_url,
            "access_token": cfg.ha_access_token,
            "token_type": "Bearer"
        }

        # Set items in local storage
        driver.execute_script(f'''
            localStorage.setItem('hassTokens', '{json.dumps(hass_tokens)}');
            localStorage.setItem('selectedLanguage', '"{cfg.language}"');
        ''')

        # Remove conflicting slashes
        screenshot_url = f"{cfg.ha_base_url}/{cfg.ha_screenshot_url}"
        print(f'Navigating to {screenshot_url}')
        driver.get(screenshot_url)
        WebDriverWait(driver, cfg.page_load_timeout_sec).until(
            EC.presence_of_element_located((By.TAG_NAME, "body"))
        )

        # Set viewport size using JavaScript
        driver.execute_script(f'''
            window.scrollTo(0, 0);
            document.body.style.overflow = 'hidden';
            document.documentElement.style.overflow = 'hidden';
            document.body.style.width = '{cfg.width}px';
            document.body.style.height = '{cfg.height}px';
            document.documentElement.style.width = '{cfg.width}px';
            document.documentElement.style.height = '{cfg.height}px';
        ''')

        time.sleep(cfg.render_delay_sec)  # Wait for possible animations to complete
        screenshot = driver.get_full_page_screenshot_as_png()

        image = Image.open(io.BytesIO(screenshot))
        image = image.crop((0, 0, cfg.width, cfg.height))

        image = convert_to_4bit_grayscale(image)

        if output_path is not None:
            image.save(output_path)
            print(f"Screenshot saved to {output_path}")

        result = io.BytesIO()
        image.save(result, format='png')
        result.seek(0)

        return result

    finally:
        # Close the browser
        driver.quit()


def main():
    parser = argparse.ArgumentParser('Home Assistant screenshot fetcher')
    CaptureConfig.add_arguments(parser)

    parser.add_argument('--output', type=str, default=None, help='Path to save the resulting image')

    args = parser.parse_args()
    config = CaptureConfig.from_args(args)

    binary = capture_screenshot(config, args.output)
    print(f'Screenshot is {len(binary.read().hex())} bytes')


if __name__ == '__main__':
    main()
