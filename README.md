# Aetris
Multiplayer tetris. May still have some bugs. Play at your own risk.

## Building
### Linux
Run `./build.sh`, creates the executable `aetris_linux`
### Windows
Look into the `./build.sh` file and find the `cl` command to build. Open the visual studio x64 command line and run the command.

## Playing
You can create a server by pushing the button "Create server". For the client, you must run the program with the IP address and port of the distant server as arguments. Exemple: `./aetris_linux 10.27.96.3 6966`. The port by default is `6966`.
