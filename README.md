# SMS relay

This is a preliminary work on an new aquarium monitoring system, and this subsystem is in charge of warning in case of main power failure, in which case the home wifi is no longer available to send alerts.

Years ago I designed an [arduino based device wich was doing that](https://github.com/reivaxy/aquaMonitor) that sent text message for various alerts, including main power outage, and I noticed it would occasionaly receive text messages from the mobile provider that I would never read, so this new device will forward them to your own mobile, and then adds a few features.

Using a LILYGO T-A7670E R2 board with a nano sim with its own phone number, this program will forward to your phone any SMS it receives, preceded by a message saying what number sent it.

<img width="601" height="610" alt="image" src="https://github.com/user-attachments/assets/b6f5bb17-422f-488b-9362-95136565b258" />

Also, if you send it a message starting by "for:" followed by another number then it will send the rest of the message to that number, which will then appear to have been sent by this device and not your phone.

But this only works for messages sent from *your* phone, to avoid other people sending messages.

This allows you to not miss any message in case your SMS enabled home automation system gets an unexpected message (from the provider, or because you are repurposing an old SIM card, or whatever mischief you are about to commit), and you can answer from your phone as if it were from the number it was sent to.

<img width="640" alt="20260704_224349" src="https://github.com/user-attachments/assets/f83f52b9-f4e3-4f4d-83e1-af1621c59c79" />

<img width="640" alt="20260704_224537" src="https://github.com/user-attachments/assets/70c265e7-7f4f-4f9d-907a-d6ea2a1e5ec1" />

<img width="640" alt="20260704_224424" src="https://github.com/user-attachments/assets/f2654b39-9969-45a1-8fff-09fa51c9b593" />

## Features

### Environmental Monitoring
- **Temperature/Humidity Monitoring**: Continuous monitoring of temperature and humidity levels with configurable high/low thresholds. Alerts are sent when values exceed configured limits
- **Battery Monitoring**: Tracks battery voltage/charge levels with alerts for low battery and near-empty states
- **Main Power Detection**: Detects main AC power failures and alerts all authorized numbers when the device loses mains power

### Alert System
- **Alert Codes**: Each alert is assigned a unique 3-digit code (000-999) for easy reference
- **Alert Acknowledgment**: Authorized users can acknowledge alerts by sending `ACK <code>`. When one user acknowledges, all others receive a notification
- **Alert Muting**: Mute specific phone numbers to prevent them from receiving alerts while still allowing them to send commands
- **Automatic Resending**: Alerts automatically resend at configurable intervals (default: 5 minutes) until acknowledged by all authorized users
- **Alert Clearing**: Alerts can be manually cleared.

### SMS Forwarding
- **Automatic Forwarding**: All non-command incoming SMS messages are automatically forwarded to "root"" phone number, preceded by the sender's number
- **Message Relay**: Forward any message to another number by starting your SMS with `FOR:<number> <message>`. The message will be sent from the device's phone number, not your own. This only works from authorized admin number.

### Configuration Management
- **Persistent Settings**: All configuration parameters are stored in flash memory and survive power cycles
- **Configurable Thresholds**: Adjust and read temperature, humidity, battery, and power detection thresholds via SMS
- **Parameter Storage**: Settings include:
  - Temperature thresholds (TEMP_HIGH, TEMP_LOW, TEMP_OFFSET)
  - Humidity thresholds (HUMIDITY_HIGH, HUMIDITY_LOW), HUMIDITY_OFFSET
  - Battery thresholds (BAT_THRESHOLD, BAT_NEAR_EMPTY)
  - Power detection threshold (POWER_THRESHOLD)
  - Alert resend interval (RESEND_MINS)

### Phone Number Management
- **Authorized Numbers**: Manage a list of authorized phone numbers with different permission levels
- **Admin vs Read Permissions**: Restrict sensitive commands to admin-only users while allowing others to view status
- **Aliases**: Assign human-readable aliases to phone numbers for easier identification
- **Mute Control**: Admin users can mute/unmute any authorized number

### Serial Console Commands
Access the device directly via serial monitor with commands like:
- `list` - List all stored SMS messages
- `read X` - Read a specific SMS by index
- `delete X` - Delete an SMS by index
- `forward X` - Forward an SMS to the configured target
- `status` - Display battery and power levels

## SMS Commands

### General Commands (All Users)
- **`STATUS`** - Display current device status including battery and power information
- **`LEVELS`** - Show current sensor readings (temperature, humidity, battery level, power status) with threshold indicators
- **`LISTPHONES`** - List all authorized phone numbers with their permission levels and mute status
- **`LISTALERTS`** - Show all pending alerts with their 3-digit codes and acknowledgment status
- **`CLEAR`** - Clear all pending alerts
- **`ACK <code>`** - Acknowledge an alert by its 3-digit code
- **`CONFIG`** - View all current configuration parameters and their values
- **`HELP`** - Display available commands

### Admin-Only Commands

#### Message Management
- **`LIST`** - List all stored SMS messages with sender and preview
- **`READ <index>`** - Read the full content of a message at the specified index
- **`DELETE <index>`** - Delete a message at the specified index
- **`FOR:<number> <message>`** - Forward a message to another number as if sent from the device

#### Phone Number Management
- **`ADDPHONE <number> <permission> [alias]`** - Add a new authorized phone number
  - `<number>`: Phone number in E.164 format (e.g., +1234567890)
  - `<permission>`: Either `admin` or `read`
  - `[alias]`: Optional human-readable alias (e.g., "Living Room" or "Bedroom")
  
- **`REMOVEPHONE <index|number>`** - Remove an authorized phone number by index or number

#### Alert Management
- **`MUTE <index|number|me>`** - Mute alerts for a phone number (still receives commands)
  - Use `me` to mute your own number
  
- **`UNMUTE <index|number|me>`** - Unmute alerts for a phone number
  - Use `me` to unmute your own number

#### Configuration Management
- **`CONFIG <parameter> <value>`** - Set a configuration parameter and save it to flash memory
  - Examples:
    - `CONFIG TEMP_HIGH 28.5` - Set maximum temperature threshold
    - `CONFIG BAT_THRESHOLD 2400` - Set battery warning level
    - `CONFIG RESEND_MINS 10` - Set alert resend interval to 10 minutes
