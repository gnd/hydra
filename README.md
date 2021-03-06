# hydra
Linux-based OpenGL Sony Action Cam HDR-AS30 viewer and recorder (tested on HX50 too)

# usage
Hydra uses libcurl to connect and download image data over Wi-Fi from a Sony Action Cam.
Just connect to the Action Cam over Wi-Fi and launch Hydra.

```
usage:
hydra [options]

Cam options:
  --cam [type]                     provide cam type (currently only hx50 and as30 (default))
  --cam-link [link]                overrides the cam type and sets the remote location directly
                                   use without http://, eg. --cam-link 10.0.0.1:60152

Display options:
  --primary-fs                     create a fullscreen window on primary monitor
  --primary-res [WidthxHeight]     create a width x height window on primary monitor (default: 800x600)
  --secondary-fs                   create a fullscreen window on secondary monitor
  --secondary-res [WidthxHeight]   create a width x height window on secondary monitor

Saving options:
  --save-dir [dir]                 directory where to save frames
  --save-file [filename]           filename to save frames in the form: name_%0Nd.jpeg
                                   %0Nd stands for number of digits, eg. my_%06d.jpeg
                                   will be saved as my_000001.jpeg, my_000002.jpeg, etc..
```
You can use these commands during runtime:

```
  spacebar              freeze frame (broken)
  c                     sony on/off
  s                     save jpeg on/off
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
