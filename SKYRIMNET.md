# SkyrimNet cinematic mode

This build is Cinematic Conversation Camera 1.5.5 with a SkyrimNet cinematic
mode added. It replaces Cinematic Conversation Camera: the DLL has the same
name (`SceneDirector.dll`), so install one or the other, not both. Everything
else in 1.5.5 works as it does in the original.

It is a fresh rewrite of the idea behind SkyrimNet CCC (which was built on
1.5.0), not a copy of its code. The Camera Editor window (F11) from SkyrimNet
CCC 0.7 is not included.

## Requirements

- Everything Cinematic Conversation Camera needs (SKSE, Address Library;
  SKSE Menu Framework for the in-game settings).
- SkyrimNet, with its public API v5 or newer. Without it the mode stays idle
  and the rest of the mod works normally.

## Keys

| Key | What it does |
| --- | --- |
| **F8** | SkyrimNet cinematic mode on or off. Remembered between sessions. |
| **F9** | Cycle the NPC shot style. While a SkyrimNet conversation is filmed, the camera cuts to it at once. |
| **F10** | Lock or unlock that shot style. |

All three can be changed on the **Keys** page (SKSE Menu Framework), or in
`SD.ini` under `[Direction]`: `iKeySkyrimNet`, `iKeySkyrimNetStyle`,
`iKeySkyrimNetLock` (DirectX scan codes; 66, 67 and 68 are F8, F9 and F10).
The usual **Next Angle** and **Who To Look At** keys work while a SkyrimNet
conversation is filmed.

## What it does

Turning the mode on doesn't take the camera. It waits for SkyrimNet dialogue.

For every SkyrimNet line, SkyrimNet says who is speaking and who they are
speaking to. That pair is filmed the way 1.5.5 films other people's
conversations, so a conversation between several people changes pairs as it
goes:

- Lydia to you
- Serana to Lydia
- Aela to Serana
- you to Aela

You are always the second person of a pair, so the reverse shots onto you are
the usual ones. Lines you type or speak into the microphone cut to you.

SkyrimNet reports a line as soon as it is written, often seconds before its
voice is ready. The camera waits for the speaker to actually be heard (a voice
playing on them, or their subtitle showing) before cutting to them, so it
doesn't cut to someone standing in silence while the last speaker finishes. A
line nobody is heard saying is skipped. If no voice has ever been detected
(for example if your voice setup plays sound in a way the camera can't see),
lines are used after the wait anyway, and the log says so.

The camera is handed back when:

- nobody has spoken for a while (15 seconds by default; the next line takes it
  again),
- you move (on by default; nothing starts filming while you move, either),
- combat starts, a menu takes the screen, or one of the two leaves,
- you start a conversation of your own through the dialogue menu, which
  always takes priority,
- you press F8.

Switching the mode off ends a SkyrimNet conversation at once: the bars
retract and your camera comes back.

It never takes the camera from your own dialogue-menu conversation, or from a
conversation you are filming with the Film Their Conversation key. A
conversation that Film When You Stand Still started on its own is taken over,
because SkyrimNet knows better who is talking to whom.

## Locked shot style

With the style locked (F10), every NPC speaker is filmed with the same
composition instead of the usual rotation. In a conversation between two NPCs,
the second one gets the matching reverse shot, so the two face each other
across the cut. Your own lines keep the usual coverage. If the style can't be
placed somewhere (a wall in the way), the camera picks as usual for that line
and tries the style again on the next one. **Keep Subject Visible** overrides
the lock, because it picks shots by what can be seen.

F9 cycles through the NPC angles that are switched on in the Shots page.

## Settings

On the **SkyrimNet** page of the in-game settings, or in `SD.ini` under
`[SkyrimNet]` (your changes go to `SD_user.ini` as usual):

| Setting | Default | |
| --- | --- | --- |
| `bCinematic` | 0 | The mode itself (F8). |
| `bLockShot` | 0 | Lock the shot style (F10). |
| `iLockedShot` | 1 | The style (F9). 1 is Close Up. |
| `iVoiceWait` | 800 | How long a line waits for its voice, in hundredths of a second. 0 cuts as soon as the line is written. |
| `iQuietRelease` | 1500 | Silence before the camera is handed back, in hundredths of a second. 0 never lets go. |
| `bReleaseOnMove` | 1 | Moving hands the camera back. |
| `iRange` | 2000 | How far from you a speaker may be, in world units. |
| `iSubtitleYOffset` | -30 | Moves the subtitle while filming. Negative is up. Not used with Subtitles In The Black Bar. |

## Safety

SkyrimNet itself isn't changed: no prompts, memories, saves, database or
settings. The mode only listens to SkyrimNet's `dialogue`,
`dialogue_player_text` and `dialogue_player_stt` events through
SkyrimNet's public API, resolved at runtime from `SkyrimNet.dll`. SkyrimNet
calls back on its own threads; those callbacks only read the event and queue
it, and everything that touches the game or the camera happens on the game's
main thread.

## Troubleshooting

The log is `Documents/My Games/Skyrim Special Edition/SKSE/SceneDirector.log`.
Lines about this mode start with `SkyrimNet`:

- `SkyrimNet API vN found; listening to 3 of 3 dialogue event types.` at
  startup means it's connected.
- `SkyrimNet dialogue: [speaker] -> [target]` for every line SkyrimNet sends.
- `SkyrimNet: line from X not filmed: ...` says why a line didn't start the
  camera.
- `no voice was heard ... cutting on the text instead` means voices aren't
  visible to the camera. Set `iVoiceWait=0` to cut straight away.

## Building

As in BUILDING.md. A GitHub Actions workflow (`.github/workflows/build.yml`)
builds the DLL on a Windows runner, runs the tests and uploads a ready-to-
install archive named `SkyrimNet_CCC`.

## License

GPL-3.0-or-later with the exceptions in EXCEPTIONS.md, like Cinematic
Conversation Camera. Changes from 1.5.5 are listed in the Git history of this
branch. The SkyrimNet event format is from SkyrimNet's MIT-licensed
`PublicAPI.h`; no SkyrimNet code is included.
