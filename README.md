## Fightcade-fbneo with Controller Rebinds In-Game (AI Generated)
Additions to this fork were assisted by Claude, i apologize if this causes any frustration.

This is intended for offline use only, and not to be used for Fightcade Online.

These changes aim to improve adjusting your controls in FBNeo by implementing an overlay menu that allows both Player 1 and Player 2 to bind their controls (Player 1 first, then Player 2) and return to the game once completed. This should hopefully remove any reliance on mouse and keyboard for general play (but should still be considered for additional keybinding setup such as Reset/Diagnostics).

Holding Start+Select will notify you that you are about to launch the overlay, Player 1 or 2 can hold a button for two seconds to complete their bindings again, and pressing Start+Select will close the overlay.

![image](https://github.com/Filthy-Acts/fightcade-fbneo-rebindtest/blob/master/example.png)

## Installation Instructions
Similar to the instructions below, but you will also need to copy the "ui" folder from your FightCade FBNeo installation location to the folder where you would like to run this version.

**Simple Overall Instructions:**

1: Install FightCade

2: Test that the standard FBNeo works with your legally acquired dump of your game of choice

3: In your FightCade install location, go to /emulator/fbneo and copy the entire folder to a new location

4: Download the binary from releases (Windows only at the moment)

5: Replace the fcadefbneo.exe file with the one from this repo

6: Run the executable and load your game through Game > Load Game


## 
## Fightcade-fbneo
Fightcade emulator built with FBNeo and GGPO
Public Release: https://www.fightcade.com

## FinalBurn Neo
Official Forum: https://neo-source.com

## How to Compile
NOTE: You will need at least VS2015 to build FightcadeFBNeo.

**1)** Download, install and run Visual Studio IDE Community Edition from https://visualstudio.microsoft.com/.

**2)** Download and install the 2010 DirectX SDK from https://www.microsoft.com/en-gb/download/details.aspx?id=6812). Make sure that you install it to the default location. The installer may also error out at the end, you can ignore it.

**3)** Go to '*projectfiles\visualstudio-2015*' and open '*fbneo_vs2015.sln*' in Visual Studio.

**4)** Select development with C++ if you are asked.

**5)** If you are asked to change the C++ toolchain, download and install the one it is asking for.

**6)** Install the PERL interpreter for Windows. We reccomend ActivePerl (https://www.activestate.com/products/activeperl/downloads/).
Make sure that you have enabled the checkbox to add PERL to the PATH variable list.

**7)** Install NASM for the assembly files from https://www.nasm.us/.
You need to put NASM.EXE somewhere in your PATH. You can put it in your Windows directory (IE: *'C:\Windows*'), but I do not recommend this for a number of reasons.

**8)** Open a command line shell and run the '*games.bat*' script. This will populate the games list, do this if you add a new game and want it to appear on the internal list and test it.

**9)** Make sure the build settings are set to '*Release, x86*'. Debug, x86 configuration also builds correctly, but is intended for developers and debugging sessions.

**10)** Build using '*Build / Build Solution*' Menu (or using keyboard shortcuts, depending on your setup, F6, F7 or Ctrl+Shift+B).

**11)** Install Fightcade from https://www.fightcade.com/.

**12)** Copy all the DLL's from the Fightcade FBNeo path (IE: '*emulator\fbneo*') to the '*build*' directory where '*fcadefbneo.exe*' was just compiled to.

**13)** Run '*fcadefbneo.exe*' that was compiled in the 'build' directory, or you can use '*Debug*' it from Visual Studio, it will launch it from that folder (debug version is '*fcadefbneod.exe*'

**14)** If you want to test a new detector, put it in '*build\detector*'.
