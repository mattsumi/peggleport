PegglePort for Windows
======================

Unzip this folder anywhere you like and double-click peggleport.exe. There is no installer and nothing else to install.

You need your own copy of Peggle Deluxe, Peggle Nights, or both. PegglePort finds installed copies, including Steam installs, and collects the required files into its own gamefiles folder. If it cannot find a game, a folder picker asks for the folder that holds main.pak. Once collection is complete, the original install and Steam are not needed to play.

Steam-enabled builds include steam_api64.dll and steam_appid.txt. Keep them beside peggleport.exe. The development App ID is 480, so Steam may show Spacewar rather than PegglePort. This is not a separate Steam store release. Steam is optional; single-player and the public multiplayer lobby work without a Steam client. Discord activity is also optional and off by default.

Windows may show "Windows protected your PC" the first time because the program is not signed. Click "More info", then "Run anyway".

pp_modhost.exe runs native mod plugins in a restricted process. Keep it next to peggleport.exe.

pp_lobbyd.exe is only for people who want to run their own online lobby. FAQ.md and the faq folder explain everything else.

Your profiles, high scores, settings, replays and mods are stored in %APPDATA%\PopCap Games\Peggle and %APPDATA%\PopCap Games\PeggleNights.
