# Architecture

XiaoZhi Care is designed as a modular extension to XiaoZhi.

## Main Care modules

### care-core
Main Care orchestration and shared domain behavior.

### care-storage
Local persistent storage for Care data.

### care-web
Family-facing configuration interface served by the ESP32.

### care-mcp
MCP integration between XiaoZhi and Care functions.

### care-ui
Care-specific user interface behavior.

### care-daily
Daily reminders, pending activities and related everyday workflows.

### care-radio
Internet radio functionality and coexistence with XiaoZhi conversation/audio.

## Design principles

- Local First for Care personal memory.
- Minimal coupling with the upstream XiaoZhi core.
- Explicit separation between daily activities and medication workflows.
- Preserve existing XiaoZhi hardware behavior.
- Fail closed during installation when hardware/profile compatibility is ambiguous.
- Keep recoverability as a first-class feature.
