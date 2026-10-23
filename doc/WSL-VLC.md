
# How to Excute VLC on WSL2 with a GUI

Motivated to excute VLC, with video and audio support, on Ubuntu 20.04 distro running on WSL2, we made a brief tutorial with the steps used.

But first, we must install some dependencies, needed for the video support.

`sudo apt-get install qml-module-qtquick-controls qml-module-qtquick-controls2` \
`sudo apt-get install qml-module-qtqml-models2`

## Set up X11 Forwarding

As WSL doesn't have video support, we need to configure a X11 forwarding, setting some environment variables and using an X-Server on Windows.

On WSL, set some Environment variables:

`export DISPLAY=$(awk '/nameserver / {print $2; exit}' /etc/resolv.conf 2>/dev/null):0` \
`export LIBGL_ALWAYS_INDIRECT=0`

On Windows, first we need to install a X-Server. 
- I suggest use [Vcxsrv](https://sourceforge.net/projects/vcxsrv/)

After install, open XLaunch App (Vcxsrv) and, before start the X-Server, remember to untick 'Native OpenGL' and tick 'Disable Acess Control', as bellow:

![image](https://i.imgur.com/2mYaWi0.png)

At this point, the X11 Forwarding should be working, if you execute vlc on WSL, a X-Server window should appear running VLC's GUI.

But there is something missing, the WSL also doesn't support audio, so we have a tutorial for this too.

## Set up PulseAudio server.

As WSL doesn't currently support sound devices, we should install a PulseAudio server on Windows, you can find the binaries on link bellow:
- Zip: http://bosmans.ch/pulseaudio/pulseaudio-1.1.zip
- Zip's source page: https://www.freedesktop.org/wiki/Software/PulseAudio/Ports/Windows/Support/

After extract the zip, we need to edit 'etc/pulse/default.pa' file:
|  Line 61  | |
|--|--|
|From| #load-module module-native-protocol-tcp |
|To|load-module module-native-protocol-tcp auth-ip-acl=127.0.0.1;172.16.0.0/12|

This enables the PulseAudio server to accept TCP connections from 127.0.0.1 and 172.16.0.0/12 which is the default space for WSL2.

On WSL, set PulseAudion Server Environment variable:

`export PULSE_SERVER=tcp:$(awk '/nameserver / {print $2; exit}' /etc/resolv.conf 2>/dev/null)`

If all goes to plan and all steps were done correctly, you can start the vlc on your WSL and both audio and video should work.

## Note: Build.

The build was done following the VLC's wiki however, to make this tutorial complete, I will show you the steps I used to build VLC in the Ubuntu 20.04 distro running on WSL2:

Clone git repository and change directory:

  `git clone git://git.videolan.org/vlc.git` \
  `cd vlc`

Install some Dependencies

  `sudo apt-get install build-essential pkg-config libtool automake autopoint gettext` \
  `sudo apt-get install flex bison `

Start the bootstrapping

  `./bootstrap`

Get the third-party libraries

  `sudo apt-get build-dep vlc`

Install dependencies to get video support

  `sudo apt-get install libxcb-xkb-dev qtdeclarative5-dev qtquickcontrols2-5-dev`

Configure and make

  `./configure` \
  `make`
  
If you want to execute VLC with audio and video support, you should configure X-Server and PulseAudio Server as described before and execute:

  `./vlc`
