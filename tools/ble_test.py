#!/usr/bin/env python3
"""BLE test script for Clawd Tank LCD display firmware."""

import asyncio
import json
import sys
import bleak

SERVICE_UUID = "aecbefd9-98a2-4773-9fed-bb2166daa49a"
CHAR_UUID = "71ffb137-8b7a-47c9-9a7a-4b1b16662d9a"
DEVICE_NAME = "Clawd Tank"


async def scan():
    """Scan for the Clawd Tank device."""
    print("Scanning for Clawd Tank device...")
    devices = await bleak.BleakScanner.discover(timeout=5.0, return_adv=True)
    for d, adv in devices.values():
        if d.name and DEVICE_NAME.lower() in d.name.lower():
            print(f"  Found: {d.name} [{d.address}] RSSI={adv.rssi}")
            return d
    print("  Clawd Tank device not found!")
    return None


async def send_command(client, cmd: dict):
    """Send a JSON command to the notification characteristic."""
    payload = json.dumps(cmd)
    print(f"  >> {payload}")
    await client.write_gatt_char(CHAR_UUID, payload.encode("utf-8"))
    print(f"  OK")


async def main():
    device = await scan()
    if not device:
        sys.exit(1)

    print(f"\nConnecting to {device.name} [{device.address}]...")
    async with bleak.BleakClient(device.address) as client:
        print(f"  Connected! MTU={client.mtu_size}\n")

        # Test 1: Add a notification
        print("Test 1: Add notification (Slack message)")
        await send_command(client, {
            "action": "add",
            "id": "slack_1",
            "project": "Slack",
            "message": "New message from Alice"
        })
        await asyncio.sleep(2)

        # Test 2: Add another notification
        print("\nTest 2: Add notification (GitHub PR)")
        await send_command(client, {
            "action": "add",
            "id": "github_1",
            "project": "GitHub",
            "message": "PR #42 approved"
        })
        await asyncio.sleep(2)

        # Test 3: Add a third notification
        print("\nTest 3: Add notification (Email)")
        await send_command(client, {
            "action": "add",
            "id": "email_1",
            "project": "Email",
            "message": "Meeting at 3pm"
        })
        await asyncio.sleep(2)

        # Test 4: Dismiss one notification
        print("\nTest 4: Dismiss Slack notification")
        await send_command(client, {
            "action": "dismiss",
            "id": "slack_1"
        })
        await asyncio.sleep(2)

        # Test 5: Clear all
        print("\nTest 5: Clear all notifications")
        await send_command(client, {
            "action": "clear"
        })
        await asyncio.sleep(1)

        print("\nAll tests complete!")


if __name__ == "__main__":
    asyncio.run(main())
