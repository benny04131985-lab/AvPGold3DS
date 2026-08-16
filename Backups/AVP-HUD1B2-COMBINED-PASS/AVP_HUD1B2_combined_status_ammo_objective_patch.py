from pathlib import Path
import re
import shutil

hud_path = Path('src/avp/hud.c')
hud_text = hud_path.read_text(encoding='utf-8')

marker = 'AVP-HUD1B2 combined Marine status, ammo, and objective capture'

if marker in hud_text:
    raise SystemExit('ERROR: AVP-HUD1B2 combined patch is already present.')

required = (
    'extern void AvP3DS_BeginTrackerCapture(void);',
    'extern void AvP3DS_EndTrackerCapture(void);',
    'DoMotionTracker();',
    'DisplayHealthAndArmour();',
    'DisplayMarinesAmmo();',
    'GADGET_Render();',
)

for token in required:
    if token not in hud_text:
        raise SystemExit(
            f'ERROR: expected tracker-only baseline token was not found: {token}'
        )

if hud_text.count('AvP3DS_BeginTrackerCapture();') != 1:
    raise SystemExit(
        'ERROR: expected exactly one existing tracker capture block before patching.'
    )

backup_path = hud_path.with_name(
    hud_path.name + '.pre-AVP-HUD1B2-COMBINED.bak'
)

if not backup_path.exists():
    shutil.copy2(hud_path, backup_path)
    print(f'Created backup: {backup_path}')
else:
    print(f'Backup already exists: {backup_path}')

# Capture the Marine health/armor and ammo rendering together. The functions
# still run once on the normal top-screen path; their completed GPU batches are
# merely retained for lower-screen replay by the already-proven HUD1B1 system.
status_pattern = re.compile(
    r'(?P<indent>[ \t]*)CheckWireFrameMode\(0\);\s*'
    r'//flash health if invulnerable\s*'
    r'if\(\(playerStatusPtr->invulnerabilityTimer/12000 %2\)==0\)\s*'
    r'\{\s*'
    r'DisplayHealthAndArmour\(\);\s*'
    r'\}\s*'
    r'DisplayMarinesAmmo\(\);\s*'
    r'DrawMarineSights\(\);',
    re.MULTILINE,
)

status_replacement = r'''\g<indent>CheckWireFrameMode(0);

#ifdef __3DS__
\g<indent>/*
\g<indent> * AVP-HUD1B2 combined Marine status, ammo, and objective capture.
\g<indent> * Capture render output only; gameplay/HUD logic still executes once.
\g<indent> */
\g<indent>AvP3DS_BeginTrackerCapture();
#endif

\g<indent>//flash health if invulnerable
\g<indent>if((playerStatusPtr->invulnerabilityTimer/12000 %2)==0)
\g<indent>{
\g<indent>    DisplayHealthAndArmour();
\g<indent>}

\g<indent>DisplayMarinesAmmo();

#ifdef __3DS__
\g<indent>AvP3DS_EndTrackerCapture();
#endif

\g<indent>DrawMarineSights();'''

hud_text, replacement_count = status_pattern.subn(
    status_replacement,
    hud_text,
    count=1,
)

if replacement_count != 1:
    raise SystemExit(
        'ERROR: the Marine health/armor/ammo block was not found exactly once.'
    )

# The upper mission/objective message is rendered by the normal HUD gadget
# layer. Patch only the final, non-observer GADGET_Render() call in MaintainHUD.
gadget_block = '''\t\t#if 1||!PREDATOR_DEMO
\t\tGADGET_Render();
\t\t#endif'''

last_gadget = hud_text.rfind(gadget_block)

if last_gadget < 0:
    raise SystemExit(
        'ERROR: final normal-play GADGET_Render() block was not found.'
    )

objective_replacement = '''\t\t#if 1||!PREDATOR_DEMO
#ifdef __3DS__
\t\tif (AvP.PlayerType == I_Marine)
\t\t\tAvP3DS_BeginTrackerCapture();
#endif

\t\tGADGET_Render();

#ifdef __3DS__
\t\tif (AvP.PlayerType == I_Marine)
\t\t\tAvP3DS_EndTrackerCapture();
#endif
\t\t#endif'''

hud_text = (
    hud_text[:last_gadget]
    + objective_replacement
    + hud_text[last_gadget + len(gadget_block):]
)

# Final guards: tracker + status/ammo + gadget layer = three capture regions.
if hud_text.count('AvP3DS_BeginTrackerCapture();') != 3:
    raise SystemExit(
        'ERROR: expected three capture starts after patching.'
    )

if hud_text.count('AvP3DS_EndTrackerCapture();') != 3:
    raise SystemExit(
        'ERROR: expected three capture ends after patching.'
    )

hud_path.write_text(hud_text, encoding='utf-8')

print('AVP-HUD1B2 combined status/ammo/objective patch applied.')
