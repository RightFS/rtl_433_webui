# rtl_433_webui

A web UI for [rtl_433](https://github.com/merbanan/rtl_433) — monitor, configure and visualise received RF data from your RTL-SDR dongle through a browser.

![Build](https://github.com/RightFS/rtl_433_webui/actions/workflows/build.yml/badge.svg)

---

## Features

- Real-time RF data stream via WebSocket
- Protocol selection and device configuration from the browser
- Signal charts powered by ECharts
- Single-binary deployment (frontend assets are embedded at compile time)

---

## Requirements

| Tool | Minimum version |
|------|----------------|
| CMake | 3.16 |
| C++ compiler | C++17 (GCC 9+ / Clang 10+) |
| Node.js | 18 LTS |
| npm | 9 |
| Python 3 | 3.8 |
| libusb | 1.0 |

---

## Building

### 1 – Clone with submodules

```bash
git clone --recurse-submodules https://github.com/RightFS/rtl_433_webui.git
cd rtl_433_webui
```

### 2 – Build the frontend

```bash
cd frontend
npm ci
npm run build
cd ..
```

### 3 – Configure and compile

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The resulting binary is at `build/server/rtl433_webui`.

---

## Configuration

Copy the example config and edit as needed:

```bash
cp config.example.json config.json
```

Key fields:

| Field | Default | Description |
|-------|---------|-------------|
| `device` | `"0"` | RTL-SDR device index or serial |
| `frequency` | `433920000` | Centre frequency in Hz |
| `sample_rate` | `250000` | Sample rate in Hz |
| `gain` | `"auto"` | Gain (`"auto"` or numeric dB value) |
| `protocols` | `[]` | rtl_433 protocol IDs to enable (empty = all) |
| `squelch` | `0` | Squelch level |
| `hop_interval` | `0` | Frequency hop interval in seconds (`0` = disabled) |
| `server_port` | `8080` | HTTP / WebSocket port |

---

## Running

```bash
./build/server/rtl433_webui            # uses config.json in the current directory
./build/server/rtl433_webui -c /path/to/config.json -p 9090
```

Then open `http://localhost:8080` in your browser.

Options:

| Flag | Description |
|------|-------------|
| `-c / --config` | Path to JSON config file (default: `config.json`) |
| `-p / --port` | HTTP port (default: `8080`) |
| `-h / --help` | Show help |

---

## Development

Start the Vite dev server with API proxy to the running backend:

```bash
cd frontend
npm run dev
```

The proxy forwards `/api` and `/ws` requests to `http://localhost:8080`.

---

## License

See individual component licences (rtl_433, Crow, nlohmann/json, Vue, Ant Design Vue).
