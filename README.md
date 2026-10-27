# Zephyr Workspace Setup

This guide provides step-by-step instructions to initialize and configure a Zephyr workspace for development.

## Prerequisites

- Ensure you have `dependencies` installed. Refer to [Zephyr Project documentation](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).
- Python and `pip` must be installed on your system.
- A compatible shell environment (e.g., Bash).

## Setup Instructions

1. **Initialize the Zephyr Workspace**

   Navigate to the parent directory where you want to create the workspace and initialize it:

   ```bash
   python3 -m venv .venv
   source .venv/bin/activate
   pip install west
   git clone https://github.com/janchri/ws-zephyr.git
   west init -l ws-zephyr
   ```

2. **Navigate to the Workspace**

   Move into the newly created workspace directory:

   ```bash
   cd ws-zephyr
   ```

3. **Update the Workspace**

   Fetch the required Zephyr modules with a shallow clone for efficiency:

   ```bash
   west update -o=--depth=1 -n
   ```

4. **Export Zephyr Environment**

   Set up the Zephyr environment variables:

   ```bash
   west zephyr-export
   ```

5. **Install Python Dependencies**

   Install the required Python packages for Zephyr development:

   ```bash
   west packages pip --install
   ```

6. **Source the Zephyr Environment**

   Activate the Zephyr environment by sourcing the environment script:

   ```bash
   cd ..
   source zephyr/zephyr-env.sh
   ```

## Optional Steps

7. **Navigate to the Zephyr Directory**

   Move into the Zephyr directory for further development tasks:

   ```bash
   cd zephyr
   ```

8. **Install Zephyr SDK**

   If needed, install the Zephyr SDK:

   ```bash
   west sdk install
   ```

9. **Fetch ESPRESSIF HAL Blobs**

   If working with ESPRESSIF hardware, fetch the necessary binary blobs:

   ```bash
   west blobs fetch hal_espressif
   ```

## Next Steps

   **Compile a basic example for the esp32_devkitc board**

   ```bash
   west build -b esp32_devkitc/esp32/procpu zephyr/samples/basic/minimal/ -p auto
   ```

## What is in the workspace

- **twixt** ([janchri/twixt](https://github.com/janchri/twixt), in `modules/lib/twixt`): equal MCUs on one multi-master I2C bus, with its samples and tests.
- **applications/twixt-system**: all of twixt together, as a product: settings in NVS, littlefs, MCUmgr (fs, img), a Pico gateway.

   ```bash
   west twister -p native_sim -T modules/lib/twixt -T ws-zephyr/applications/twixt-system
   ```

## Coding style

C follows Zephyr's `.clang-format`, Python Zephyr's ruff rules (`.ruff.toml`). A pre-commit hook checks the files of every commit. Its hooks are local: they run the tools of `requirements.txt` (the clang-format and ruff versions of Zephyr's CI) from the active venv, so nothing is downloaded. Install them and the hook once per clone:

```bash
pip install -r ws-zephyr/requirements.txt
cd ws-zephyr && pre-commit install
```

`pre-commit run --all-files` runs the checks by hand. The hook fixes whitespace and formatting itself; add the fixed files and commit again.

## Troubleshooting

## License
