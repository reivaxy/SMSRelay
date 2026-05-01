# SMS relay

Using a LILYGO T-A7670E R2 board with a nano sim with its own phone number, this program will forward to your phone any SMS it receives, preceded by a message saying what number sent it.

<img width="601" height="610" alt="image" src="https://github.com/user-attachments/assets/b6f5bb17-422f-488b-9362-95136565b258" />

Also, if you send it a message starting by "for:" followed by another number then it will send the rest of the message to that number. 

But this only works for messages sent from *your* phone, to avoid other people sending messages.

This allows you to not miss any message in case your SMS enabled home automation system gets an unexpected message (from
the provider, or because you are repurposing an old SIM card, or whatever mischief you are about to commit), and you can answer 
from your phone as if it were from the number it was sent to.
