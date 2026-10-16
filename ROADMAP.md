# Plan list

Ideas and planned work, not yet started. Nothing here is implemented.

## Probability note command
A phrase command that gives a row's note an xx% chance to play each time the playhead reaches it. When the roll fails, the row is skipped, the same as an empty row.

Open questions:
- Name: no existing command uses `PROB` or `CHNC` (`sources/Application/Instruments/CommandList.h`).
- Value scale: `00`-`FF` as 0-100%, or a hex percentage (`00`-`64`)?
- When the roll fails, are the row's other commands skipped as well, or only the note?
- Should it work in tables as well, for each table step?
- Random source: seeded per song, so a render can be repeated, or free-running? The player has no random source yet.
- It must work for every instrument type, so it belongs in the player (`Player::PhraseCommands`), not in an instrument's `ProcessCommand`.
