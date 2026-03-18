DARK SOULS 2: SEAMLESS CO-OP MOD
================================

Play through Dark Souls 2: Scholar of the First Sin with friends
without getting disconnected at boss fights or deaths.


HOW TO INSTALL (recommended — automatic)
-----------------------------------------

1. Copy dinput8.dll and ds2_seamless_coop.ini into your game folder:
   C:\...\Dark Souls II Scholar of the First Sin\Game\

2. Launch Dark Souls 2 normally through Steam
   The mod loads automatically — no launcher needed.

3. Press INSERT in-game to open the co-op menu

4. HOST: Click "Host Session", set a password, click "Start Hosting"
   - Your IP addresses are shown on screen
   - Click "Copy" to copy your IP, then send it to your friend

5. JOIN: Click "Join Session", enter host's IP and password, click "Connect"

To uninstall: delete dinput8.dll from the game folder.


ALTERNATIVE (manual injection)
------------------------------

If you prefer not to put files in your game folder:
1. Launch Dark Souls 2 through Steam
2. Run PlayDS2WithMod.exe as Administrator
3. Press INSERT in-game


CONNECTING TO FRIENDS
---------------------

Same network (LAN / same house):
  Use the LAN IP shown in the host menu. No extra setup needed.

Over the internet:
  Option A - Port forwarding (best performance):
    Host forwards UDP port 27015 on their router.
    Joiners use the host's Public IP shown in the menu.

  Option B - Hamachi / ZeroTier / Radmin VPN (easiest):
    Everyone installs the same VPN tool and joins the same network.
    The mod auto-detects Hamachi IPs (25.x.x.x).
    Host shares their Hamachi IP instead.


WHAT THIS MOD DOES
------------------

- Blocks disconnect messages when bosses die or players die
- Keeps your co-op session alive through the entire game
- No more re-summoning after every boss fight
- In-game menu for hosting/joining (no config file editing)
- Shows connected players, HP, and session status


FILES
-----

dinput8.dll            - Drop in game folder for auto-load (recommended)
PlayDS2WithMod.exe     - Alternative: manual launcher (run as admin)
ds2_seamless_coop.dll  - Same mod, different name (for manual injection)
ds2_seamless_coop.ini  - Configuration (defaults are fine)
ds2_seamless_coop.log  - Created in game folder after first run


CONTROLS
--------

INSERT    Open/close the co-op menu


REQUIREMENTS
------------

- Dark Souls 2: Scholar of the First Sin (Steam)
- Windows 10/11
- Run launcher as Administrator


TROUBLESHOOTING
---------------

Mod doesn't load:
  Run PlayDS2WithMod.exe as Administrator.
  Check if antivirus blocked the DLL.
  Right-click DLL > Properties > Unblock (if shown).

Can't connect to friend:
  Make sure both players use the same password.
  Check that UDP port 27015 is forwarded (or use Hamachi).
  Try disabling Windows Firewall temporarily.

No overlay in game:
  Make sure you launched the game BEFORE running the launcher.
  Check ds2_seamless_coop.log in the game folder for errors.
