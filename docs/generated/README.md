# Generated Schemas

This directory contains data contract schemas — either auto-generated from code or hand-maintained to document the shape of key data types (BLE JSON payloads, session state, persisted preferences).

## Schemas

| Name | Source | Purpose |
|------|--------|---------|
| <!-- e.g., `set_sessions.schema.json` --> | <!-- e.g., `firmware/main/ble_service.c` --> | <!-- e.g., BLE v2 per-session payload --> |

## Keeping schemas in sync

When you add, rename, or change the shape of a data contract:
1. Update or regenerate the corresponding schema in this directory
2. Update this README table
3. Verify that consumers of the schema still work correctly — firmware parser, simulator parser, and daemon sender paths must all agree on the shape
