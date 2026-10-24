# SMS relay

This is a preliminary work on an new aquarium monitoring system, and this subsystem is in charge of warning in case 
of main power failure, in which case the home wifi is no longer available to send alerts.

It also monitors room temperature and humidity and can send alerts when configuratble thresholds are reached.

Other devices, each monitoring one aquarium, will ultimately also use this device to send alerts by SMS and push notifications (light issue, 
water level or temperature issue, ...)

Years ago I designed an [arduino based device wich was doing that](https://github.com/reivaxy/aquaMonitor) that sent 
text message for various alerts, including main power outage, and I noticed it would occasionaly receive text messages 
from the mobile provider that I would never read, so this new device will forward them to your own mobile, and then 
adds a few features. This device no longer works since 2G networks have been discontinued.

Using a LILYGO T-A7670E R2 board with a nano sim with its own phone number, this program will forward to your phone 
any SMS it receives, preceded by a message saying what number sent it.

<img width="601" height="610" alt="image" src="https://github.com/user-attachments/assets/b6f5bb17-422f-488b-9362-95136565b258" />

Also, if you send it a message starting by "for:" followed by another number then it will send the rest of the message 
to that number, which will then appear to have been sent by this device and not your phone.

But this only works for messages sent from *your* phone (configured as ROOT), to avoid other people sending messages.

This allows you to not miss any message in case your SMS enabled home automation system gets an unexpected message 
(from the provider, or because you are repurposing an old SIM card, or whatever mischief you are about to commit), 
and you can answer from your phone as if it were from the number it was sent to.

<img width="640" alt="20260704_224349" src="https://github.com/user-attachments/assets/f83f52b9-f4e3-4f4d-83e1-af1621c59c79" />

<img width="640" alt="20260704_224537" src="https://github.com/user-attachments/assets/70c265e7-7f4f-4f9d-907a-d6ea2a1e5ec1" />

<img width="640" alt="20260704_224424" src="https://github.com/user-attachments/assets/f2654b39-9969-45a1-8fff-09fa51c9b593" />

## Features

### Environmental Monitoring
- **Temperature/Humidity Monitoring**: Continuous monitoring of temperature and humidity levels with configurable 
high/low thresholds. Alerts are sent when values exceed configured limits
- **Battery Monitoring**: Tracks battery voltage/charge levels with alerts for low battery and near-empty states
- **Main Power Detection**: Detects main AC power failures and alerts all authorized numbers when the device loses
 mains power

### Alert System
- **Alert Codes**: Each alert is assigned a unique 3-digit code (000-999) for easy reference
- **Alert Acknowledgment**: Authorized users can acknowledge alerts by sending an SMS with `ACK <code>`. When one user 
acknowledges, all others receive a notification
- **Alert Muting**: Mute specific phone numbers to prevent them from receiving alerts while still allowing 
them to send commands
- **Automatic Resending**: Alerts automatically resend at configurable intervals (default: 5 minutes) until 
acknowledged by all authorized users
- **Alert Clearing**: Alerts can be manually cleared.

### Push Notifications (Pushover)
- **Pushover Integration**: In addition to SMS, alerts and notifications can be pushed to your phone through the
 [Pushover](https://pushover.net) service and App. Notifications are sent over the modem's 4G data connection, so they
 work even when the home wifi is down (e.g. during a main power failure)
 
 - **Alerts**: Every new alert, and every automatic resend of an unacknowledged alert, is also pushed, with the title
 followed by its 3-digit code and the cause in the message body
- **Notifications**: Informational messages (e.g. "Main power restored") are also pushed
- **NOTIF command**: Send a custom push notification with `NOTIF <message>`. The title shows who sent it (alias or number)
- **Optional**: Pushover is only used when both tokens are configured. Leave them empty to disable it, SMS alerts
 are not affected
- **Muting**: Muting a phone number only affects SMS, Pushover notifications are always sent when configured. They can be silent in the Pushover app on the receiver.

To set it up:
1. Create an account on [pushover.net](https://pushover.net) and note your **User Key**
2. Create an application on Pushover to get an **API Token**
3. Set `PUSHOVER_USER` and `PUSHOVER_TOKEN` using `CONFIG` (SMS or serial console) or the web interface
 (`WEB ON`). Tokens are masked when displayed in the web form

### SMS Forwarding
- **Automatic Forwarding**: All non-command incoming SMS messages are automatically forwarded to "root"" phone
 number, preceded by the sender's number
- **Message Relay**: Forward any message to another number by starting your SMS with `FOR:<number> <message>`.
 The message will be sent from the device's phone number, not your own. This only works from authorized admin
  number.

### Configuration Management
- **Persistent Settings**: All configuration parameters are stored in flash memory and survive power cycles
- **Configurable Thresholds**: Adjust and read temperature, humidity, battery, and power detection thresholds
 via SMS
- **Parameter Storage**: Settings include:
  - Temperature thresholds (TEMP_HIGH, TEMP_LOW, TEMP_OFFSET)
  - Humidity thresholds (HUMIDITY_HIGH, HUMIDITY_LOW), HUMIDITY_OFFSET
  - Battery thresholds (BAT_THRESHOLD, BAT_NEAR_EMPTY)
  - Power detection threshold (POWER_THRESHOLD)
  - Alert resend interval (RESEND_MINS)
  - Pushover credentials (PUSHOVER_USER, PUSHOVER_TOKEN)

### Phone Number Management
- **Authorized Numbers**: Manage a list of authorized phone numbers with different permission levels
- **Admin vs Read Permissions**: Restrict sensitive commands to admin-only users while allowing others to
 view status
- **Aliases**: Assign human-readable aliases to phone numbers for easier identification
- **Mute Control**: Admin users can mute/unmute any authorized number

### Serial Console Commands
Serial console now accepts all the messages that can be sent by SMS, it has ROOT privileges.

## SMS Commands

### General Commands (All Users)
- **`STATUS`** - Display current device status including battery and power information
- **`LEVELS`** - Show current sensor readings (temperature, humidity, battery level, power status) with threshold indicators
- **`LISTPHONES`** - List all authorized phone numbers with their permission levels and mute status
- **`LISTALERTS`** - Show all pending alerts with their 3-digit codes and acknowledgment status
- **`CLEAR`** - Clear all pending alerts
- **`ACK <code>`** - Acknowledge an alert by its 3-digit code
- **`CONFIG`** - View all current configuration parameters and their values
- **`HELP`** - Display available commands as well as config parameters
- **`MUTE ME`** - No longer send message to my number
- **`UNMUTE ME`** - Resume sending message to my number
- **`NOTIF <message>`** - Send a push notification through Pushover (requires Pushover to be configured)


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
    - `CONFIG PUSHOVER_USER <user key>` / `CONFIG PUSHOVER_TOKEN <api token>` - Set Pushover credentials

#### System Management
- **`OTA`** - Initiate Over-The-Air (OTA) firmware update mode
  - Connects to configured WiFi and starts a web server
  - Sends you the upload URL via SMS
  - Allows uploading new firmware without serial connection
  - Cannot be initiated if there are pending unacknowledged alerts
  
- **`RESTART`** - Restart the device
  
- **`WEB <ON|OFF>`** - Control the web configuration interface
  - **`WEB ON`** - Start web server for configuration via browser
    - Connects to home WiFi and sends you the access URL via SMS
    - Provides a secure web form to adjust all configuration parameters, including Pushover tokens
  - **`WEB OFF`** - Stop the web server and disconnect from WiFi

