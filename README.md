# hydra
OpenGL Sony Action Cam HDR-AS30 viewer and recorder

# usage
Hydra uses libcurl to connect and download image data over Wifi from a Sony Action Cam.
Just connect to the Action Cam and launch Hydra.

```
usage: hydra [options]
options:
  --primary-fs                     create a fullscreen window on primary monitor
  --primary-res [WidthxHeight]     create a width x height window on primary monitor (default: 800x600)
  --secondary-fs                   create a fullscreen window on secondary monitor
  --secondary-res [WidthxHeight]   create a width x height window on secondary monitor

```
You can use these commands during runtime:

```
  spacebar              freeze frame (broken)
  c                     sony on/off
  s                     save jpeg on/off (broken)
  t                     FPS printing
  q, ctrl+c, esc,       exit

```

# installation

```
apt-get install libjpeg-dev libcurl4-openssl-dev libglfw3-dev libxi-dev
git clone https://github.com/gnd/hydra.git
cd hydra
make
```

###
Inspired by https://github.com/erik-smit/sony-camera-api/blob/master/liveView.py
