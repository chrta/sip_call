# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32 firmware that monitors a GPIO input (doorbell via opto-coupler) and initiates SIP (VoIP) calls to a configured number. Tested with AVM Fritzbox routers and FreeSWITCH. Requires ESP-IDF v6.0.

## Build Commands

### ESP32 Firmware

```bash
idf.py menuconfig          # Configure WiFi, SIP server, GPIO pins, timeouts
idf.py build               # Build firmware
idf.py flash monitor       # Flash and monitor serial output
idf.py set-target esp32c3  # Switch target SoC (esp32, esp32s2, esp32c3)
```

### Native PC Build (Linux, for development/testing without hardware)

```bash
mkdir build_gcc && cd build_gcc
cmake ../native -G Ninja -D CMAKE_CXX_FLAGS=-DASIO_STANDALONE
ninja
```

Requires: `asio-devel mbedtls-devel` (e.g. `sudo dnf install asio-devel mbedtls-devel`)

### Code Style

```bash
# Check formatting (uses clang-format-21)
ci/check_codestyle.sh

# Auto-format all source files
find . -regex '.*\.\(cpp\|cc\|cxx\|h\)' -exec clang-format -style=file -i {} \;
```

### Static Analysis

```bash
# Runs clang-tidy-21 over a Clang native build; exits non-zero on any finding
ci/static_code_analysis.sh
```

### CI Build Scripts

```bash
ci/build_for_esp32.sh   # Builds for esp32, esp32s2, esp32c3 with default + alternative configs
ci/build_for_pc.sh      # Builds with GCC and Clang (clang-21)
```

## Architecture

### Component Structure

**`components/sip_client/`** — Header-only SIP protocol library (the core of the project)
- `SipClient<SocketT, Md5T>` — public interface (sip_client.h); templated on socket and MD5 implementations for testability
- `SipClientInt` — internal state machine logic (sip_client_internal.h)
- `sip_states` — Boost.SML state machine definitions handling REGISTER/INVITE/BYE flows
- `Registration`, `Dialog` — aggregate structs holding auth state and per-call dialog state; `Dialog` is held in `std::optional` and only populated while a call is in progress
- `SipIdentifier` — Call-ID / From-tag / branch generator backed by `esp_random` on target and `std::mt19937` on native; also seeds the initial CSeq
- `AsioUdpClient` — ASIO-based UDP transport used on both ESP32 and native builds
- `MbedTlsMd5` — MD5 digest authentication via mbed TLS

**`components/web_server/`** — Optional HTTP + WebSocket server
- Template class `WebServer<SipClientT>` serving `index.html`, `/upload/firmware.bin` (OTA), and WebSocket events
- WebSocket broadcasts SIP client events as JSON to all connected browsers

**`main/`** — ESP32 application entry point
- `main.cpp` — WiFi init, GPIO setup, ASIO io_context loop on core 0, wires together all components
- `button_handler.h` — Boost.SML state machine with `idle` / `sRinging` / `sInCall` states; the ring-duration timeout only arms while in `sRinging`, so an established call is not auto-cancelled
- `sip_event_handler_log.h` / `sip_event_handler_button.h` — SIP event callbacks

**`native/`** — PC simulation build (keyboard input instead of GPIO, same SIP client)

### Event Flow

```
GPIO Interrupt → button_handler state machine
                        ↓ (ring/cancel events)
                 SipClient state machine (UDP/ASIO)
                        ↓ (SipClientEvent callbacks)
         sip_event_handler_log + sip_event_handler_button + web_server
```

### Key Configuration (menuconfig / `main/Kconfig.projbuild`)

- WiFi SSID/password
- SIP server IP (gateway auto-detect or fixed), port (default 5060), user/password
- Bell input GPIO (default 0 for ESP32, 9 for ESP32C3)
- Ring duration timeout (default 7s)
- Optional: HTTP server enable, mDNS, actuator GPIO output, power save mode

`sdkconfig.defaults` — baseline; `sdkconfig-ci-cfg-1.defaults` — alternative CI config with different feature flags.

### State Machine Pattern

Both the SIP protocol and button debouncing use **Boost.SML** (`components/sip_client/include/boost/sml.hpp`). Do not run clang-format on this vendored file.

## Code Standards

- C++20 throughout
- `.clang-format` (LLVM-based style) enforced in CI
- `.clang-tidy` sets `WarningsAsErrors: '*'` — every configured check fails the build; do not introduce new findings (and do not disable via `NOLINT` without a reason)
- The SIP client is header-only and templated — avoid adding dynamic allocation
- Native PC build must remain in sync with ESP32 build for development usability
