# Named pipes

This ports Zeal 1.4.8's outbound named-pipe data protocol to RoF2 Gusto. While
in game, local readers can open `\\.\pipe\gusto_<game-process-ID>` for reading.
The default interval is 100 ms. There are up to four concurrent readers.
For tools hardcoded to Zeal, enable **Zeal endpoint compatibility** on the
Pipes page or run `/gusto pipes zeal on`. The additional
`\\.\pipe\zeal_<game-process-ID>` endpoint publishes the same data.
Both endpoints can be consumed at once; each supports up to four readers.

The **Pipes** page in `/gusto` controls pipe enablement, verbose output and
snapshot interval. Options persist in `[Pipes]` in the HUD INI. Commands:

| Command | Effect |
| --- | --- |
| `/gusto pipes status` | Endpoint, state, clients, interval, drops, last Win32 error |
| `/gusto pipes zeal on` / `off` | Open / close the additional Zeal-compatible endpoint |
| `/gusto pipes on` / `off` | Enable / close the pipe server |
| `/pipedelay 100` | Snapshot interval, 25–5000 ms |
| `/pipedelay 0` | Pause periodic snapshots, keeping chat/custom output |
| `/pipeverbose on` / `off` | Additional group/raid HP, class, level and zone fields |
| `/pipe text to send` | Publish a custom message |

For a basic reader, run `python tools/read_gusto_pipe.py <PID>` with Windows
Python in the same Windows/Wine environment as the game. It decodes both JSON
layers and prints each message. Reconnect after zoning if the pipe closes.

## Wire format

A byte-mode stream carries UTF-8 JSON objects, each followed by a newline.
Readers must handle fragmented reads and multiple records in one read. Each
object preserves Zeal's envelope and numeric `pipe_data_type` values:

```json
{"type":4,"data_len":16,"data":"{\"text\":\"hello\"}","character":"Aggatha"}
```

`data` is a JSON **string**, so parse the outer object and then parse its `data`.
`data_len` counts UTF-8 bytes in that decoded inner string. `character` is the
current observed player's name; the first snapshot initializes it.

| Type | Inner data |
| --- | --- |
| 0 log | `{ "type": chatColor, "text": chatText }` |
| 1 label | Array of `{ "type": eqType, "value": string, "meta": {} }` |
| 2 gauge | Array of `{ "type": eqType, "text": string, "value": 0..1000 }` |
| 3 player | Zone, location, heading, autoattack, spawn ID; optional target and pet |
| 4 custom | `{ "text": text }` |
| 5 raid | Roster with group number, name, class, level, rank; observed spawn details |
| 6 group | Observed group members with name, spawn ID, location and heading |

Player target fields include ID, display name, type, level, class and race.
`target_loc` is exported only within 250 units, as in the local Zeal code.
Target/pet IDs are absent when missing. Empty raid/group arrays clear departed
rosters. Offline or out-of-zone group entities without a spawn are omitted;
raid roster names remain, with unavailable spawn fields omitted. Group output
excludes self and covers the other observed members. Verbose HP values retain
native values; they are not inferred percentages.

Labels currently cover name/level/class (1–3), HP/mana/stamina percentages
(17–21), target (28–29), other group members (30–39), pet (68–69), HP/max (70),
mana/max (80), and mana/max separately (124–125). Gauges cover HP/mana/stamina
(1–3), target (6), group HP (11–15), and pet HP (16). Unknown values are omitted.
Zeal's buff, spell-recast, experience and server-tick extensions are not emitted;
they depend on client systems not ported here. `/pipe` sends literal text rather
than Zeal's percent-conversion expansion. No inbound game-command execution is
exposed by this outbound protocol.

## Transport and layouts

All game reads occur on the existing game callback. Win32 overlapped connect/
write operations are polled without waiting during normal operation. Buffers
stay alive until completion or cancelled I/O drains. Each reader is limited
to 64 queued messages / 256 KiB; lagging readers are disconnected so they can
reconnect and receive current snapshots. Individual records are capped at
64 KiB. Shutdown and zoning close connections and clear the character name.
There is no detached worker thread and no borrowed game pointer in transport.

The validated local MacroQuest emu-rof2 source supplies `CRaid` at `0xDD2690`,
used slots `+0x218`, members `+0x260`, stride `0x94`, names/level/class/rank/group
at `+0/+0x80/+0x81/+0x82/+0x83/+0x88`. Group uses `LocalPC +0x31CC`, members
`+4+4*i`, and member spawn `+0x28`. Player class/race are ActorClient
`+0xEA4 +0x14/+0x10`. Existing validated player/target/resource readers supply
other fields. Zeal's Titanium addresses are not reused.

The transport tests exercise real named pipes under Wine: concurrent readers,
large fragmented writes, disconnect/reconnect, bounded queues and cancellation.
Pure snapshot tests cover schema, optional fields, resources and target range.
Live EQ snapshots and a particular external consumer still need confirmation.

`ZealAlias` in `[Pipes]` persists endpoint compatibility. It defaults off; existing Gusto readers continue using their original endpoint. Tests verify actual identical Gusto/Zeal broadcasts, chat sanitization, alias toggles and shutdown.
