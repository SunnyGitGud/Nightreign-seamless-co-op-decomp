# Testing the reconstructed nrsc.dll (Linux, Steam shortcut + ME3)

1. Steam running and logged in. In the "Nightreign nrsc capture" shortcut's launch options, change the `-p` profile to
   `Z:<repo>/dist/nightreign-reconstruction-linux.me3` (a local ME3 profile that loads `dist/SeamlessCoop/nrsc.dll`) (keep a copy of the old line to switch back).
2. Launch. Check `dist/SeamlessCoop/nrsc_reconstruction.log`:
   - `signature table built`, a list of `installed hook#...`, `held back hook#7 / hook#10`, `installed session driver ...`
   - `session: started, own SteamID ...` once the game is running, then after ~20 s `session: auto-host (W): CreateLobby sent`
     and `steam: LobbyCreated ...` / `steam: LobbyEnter ...` / `session: lobby entered (net hooks attached)`.
   - A crash box "Seamless Coop 1.1.3 - Fatal Error" shows the failing step; the log ends with `FATAL: ...`.
3. Two players: the friend needs the same setup (same game build, ME3, this DLL + profile). The host waits for the auto-host
   lines, then invites the friend through the Steam overlay (shift+tab) or the friend uses "Join game" on the host. The friend's
   log should show `GameLobbyJoinRequested`, `LobbyEnter`, then `SessionRequest` on the host. Mixing the original nrsc.dll and
   this one is not supported.
4. Send back the log(s) from both machines, and any crash dump under `SeamlessCoop/crashdumps/` (the original's crashpad is not
   part of the reconstruction, so dumps would come from the game/Proton instead).

Known differences from the original: enemy scaling hooks #7/#10 held back (game scaling runs), no translated text override,
the lobby create/join flow is a standard Steam flow (W), auto-host after 20 s instead of the original's (unrecovered) trigger.
