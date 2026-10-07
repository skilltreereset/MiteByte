#pragma once
// Generated from mitebyte/src/scripts/*.txt by tools/build_helpers.py. Do not edit.
struct SeededScript { const char *name; const char *body; };
static const char FLEA_SCRIPT_0[] PROGMEM = R"FLEAPL(REM Checks that the selected layout matches the machine's keyboard.
REM Open a text editor BEFORE running this script.
REM These are the characters that move between AZERTY and QWERTY.
DELAY 500
STRINGLN --- MiteByte layout test ---
STRINGLN azertyuiop qwertyuiop
STRINGLN AZERTYUIOP QWERTYUIOP
STRINGLN 0123456789
STRINGLN @ # $ % & * ( ) - _ = +
STRINGLN / \ | [ ] { } < > ^ ~
STRINGLN ! ? : ; , . ' "
STRINGLN If this line reads correctly, the layout is right.
)FLEAPL";
static const char FLEA_SCRIPT_1[] PROGMEM = R"FLEAPL(REM Every command the interpreter understands.
REM Running this types the reference out, so open a text editor first.
DELAY 500
STRINGLN == MiteByte command reference ==
STRINGLN
STRINGLN REM text              a comment, does nothing
STRINGLN META windows|linux|macos   tags this script, never typed
STRINGLN STRING text           types the text
STRINGLN STRINGLN text         types the text, then Enter
STRINGLN DELAY 500             waits 500 ms
STRINGLN WAIT_FOR_HOST 5000    waits for the host to enumerate
STRINGLN DEFAULTDELAY 50       pause after every line
STRINGLN DEFAULTCHARDELAY 20   pause between characters
STRINGLN LAYOUT <code>          switches layout mid-script
STRINGLN   us fr de ch hu es it pt br se dk jp
STRINGLN REPEAT 3              replays the previous line 3 times
STRINGLN
STRINGLN Named keys:
STRINGLN   ENTER TAB ESC SPACE BACKSPACE DELETE INSERT
STRINGLN   HOME END PAGEUP PAGEDOWN UP DOWN LEFT RIGHT
STRINGLN   MENU CAPSLOCK PRINTSCREEN PAUSE F1 to F12
STRINGLN
STRINGLN Modifiers: CTRL SHIFT ALT GUI ALTGR
STRINGLN   GUI r / CTRL ALT DELETE / CTRL SHIFT ESC
STRINGLN
STRINGLN Only ASCII is typed. Accented characters are skipped
STRINGLN and reported in the run log.
)FLEAPL";
static const char FLEA_SCRIPT_2[] PROGMEM = R"FLEAPL(META windows
REM Windows: opens Notepad and writes a line of proof.
DELAY 300
GUI r
DELAY 600
STRING notepad
ENTER
DELAY 1200
STRINGLN Script executed from MiteByte.
STRING Test machine only.
)FLEAPL";
static const char FLEA_SCRIPT_3[] PROGMEM = R"FLEAPL(META windows
REM Windows: opens a page in the default browser via the Run dialog.
REM Also a quick layout check: a wrong layout mangles the slashes
REM and dots, and the address fails to resolve.
DELAY 300
GUI r
DELAY 600
STRINGLN https://example.com
)FLEAPL";
static const char FLEA_SCRIPT_4[] PROGMEM = R"FLEAPL(META windows
REM Windows: opens PowerShell and prints a line.
REM The classic smoke test: if this shows up, HID, timing and layout
REM are all working.
DELAY 300
GUI r
DELAY 600
STRING powershell
ENTER
REM PowerShell takes longer to appear than a text editor.
DELAY 2000
STRINGLN echo ok
)FLEAPL";
static const char FLEA_SCRIPT_5[] PROGMEM = R"FLEAPL(REM Checks that the selected layout matches the machine's keyboard.
REM Open a text editor BEFORE running this script.
REM These are the characters that move between AZERTY and QWERTY.
DELAY 500
STRINGLN --- MiteByte layout test ---
STRINGLN azertyuiop qwertyuiop
STRINGLN AZERTYUIOP QWERTYUIOP
STRINGLN 0123456789
STRINGLN @ # $ % & * ( ) - _ = +
STRINGLN / \ | [ ] { } < > ^ ~
STRINGLN ! ? : ; , . ' "
STRINGLN If this line reads correctly, the layout is right.
)FLEAPL";
static const char FLEA_SCRIPT_6[] PROGMEM = R"FLEAPL(META linux
REM Linux/GNOME: opens a terminal and prints a message.
REM CTRL ALT T is the GNOME shortcut; adjust for your desktop.
DELAY 300
CTRL ALT t
DELAY 1500
STRINGLN echo "MiteByte - HID test $(date)"
)FLEAPL";
static const char FLEA_SCRIPT_7[] PROGMEM = R"FLEAPL(META macos
REM macOS: opens TextEdit through Spotlight and types a line.
DELAY 300
GUI SPACE
DELAY 800
STRING TextEdit
DELAY 700
ENTER
DELAY 2000
STRINGLN Script executed from MiteByte.
)FLEAPL";
static const char FLEA_SCRIPT_8[] PROGMEM = R"FLEAPL(REM Shows the timing commands. Open a text editor first.
DELAY 500
STRINGLN -- default speed --
STRINGLN The quick brown fox jumps over the lazy dog.
DEFAULTCHARDELAY 60
STRINGLN -- slowed to 60 ms per character --
STRINGLN The quick brown fox jumps over the lazy dog.
DEFAULTCHARDELAY 0
STRINGLN -- as fast as the host accepts --
STRINGLN The quick brown fox jumps over the lazy dog.
STRINGLN -- REPEAT replays the previous line --
STRING .
REPEAT 30
ENTER
STRINGLN done
)FLEAPL";
static const char FLEA_SCRIPT_9[] PROGMEM = R"FLEAPL(REM Targets whose input mode is not Latin: Russian, Chinese, Korean,
REM Japanese kana. A layout table cannot help there, because with a
REM non-Latin input mode active no key produces an ASCII letter at all.
REM The fix is to switch the host back to Latin input first.
REM
REM GUI SPACE  cycles input languages on Windows 10/11, GNOME, macOS
REM ALT SHIFT  does the same on older Windows setups
REM SHIFT      toggles Chinese/English on most Pinyin IMEs
DELAY 300
GUI SPACE
DELAY 600
LAYOUT us
STRINGLN Latin input restored.
)FLEAPL";
static const SeededScript SEEDED_SCRIPTS[] = {
  {"00-test-layout.txt", FLEA_SCRIPT_0},
  {"01-command-reference.txt", FLEA_SCRIPT_1},
  {"10-windows-notepad.txt", FLEA_SCRIPT_2},
  {"11-windows-open-url.txt", FLEA_SCRIPT_3},
  {"12-windows-powershell.txt", FLEA_SCRIPT_4},
  {"14-windows-hotspot-uninstall.txt", FLEA_SCRIPT_5},
  {"20-linux-terminal.txt", FLEA_SCRIPT_6},
  {"30-macos-textedit.txt", FLEA_SCRIPT_7},
  {"40-timing-demo.txt", FLEA_SCRIPT_8},
  {"50-switch-input-language.txt", FLEA_SCRIPT_9},
};
