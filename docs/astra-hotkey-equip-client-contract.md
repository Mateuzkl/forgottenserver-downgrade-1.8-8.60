# Astra hotkey equip client contract

The server handles hotkey equip through opcode `0x77` only for authenticated Astra clients.

Payload:

```text
uint16 itemId
uint8 tier   # only when the server advertises item tier support for this item
```

Do not send item subtype/count in this opcode. If a client currently sends `itemId + subtype`, update it to omit `subtype`; otherwise, when tier support is enabled, the server can interpret that byte as `tier` and search for the wrong item.

Clients that need stack count, charges, duration, or other item state should use the existing item serialization/state features, not opcode `0x77`.
