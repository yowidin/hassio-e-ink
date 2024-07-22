# E-Ink display for Home Assistant 

## Initial setup

Follow the [Getting Started](https://docs.zephyrproject.org/latest/getting_started/index.html) guide to set up your
development environment

## Checking out the code

Run the following inside your Zephyr environment:

```bash
# Checkout the code
west init -m git@github.com:yowidin/hassio-e-ink hassio-e-ink

# Fetch the Zephyr modules
cd hassio-e-ink
west update
```

Note: this will take a while.


## Fetch ESP32 RF libraries

```bash
west blobs fetch hal_espressif
```