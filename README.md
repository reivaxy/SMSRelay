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
