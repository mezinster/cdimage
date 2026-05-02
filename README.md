# CDImage
## A tool for burning visible pictures on a compact disc surface

![alt text](https://github.com/arduinocelentano/cdimage/blob/main/demo.png)

## Project's Origins and Credits
I know of at least two successful attempts to implement a similar technique. One was accomplished about 15 years ago by [argon](https://www.instructables.com/Burning-visible-images-onto-CD-Rs-with-data-beta/) Instructables user. [Another attempt](http://undefer.narod.ru/cdpaint/index.html) was made by a user with nickname [unDEFER] (no English documentation unfortunately). These two projects inspired me some time ago. And in fact my coordinate conversion code is mostly based on [unDEFER]’s implementation. I also used geometric parameters of some compact discs from that project. I acknowledge and am grateful to these developers for their contributions.

I played with color shades and different compact discs with moderate success and created a GUI with visual preview mode. I tried to implement a user-friendly solution but finally abandoned the project in 2008 due to a problem of calibration for every particular brand and type of compact disc. Recently I found my old code and decided to share it. More as a tribute to the compact disc era. But maybe you will make some use of it. I fixed some obvious bugs, brushed the code up a little and ported it to modern Qt6. I considered porting to Python but I still need C++ because audio track generation takes some time even on modern hardware.

## Building
You'll need [Qt 6](https://www.qt.io/product/qt6) library to build it. Just run `qmake` and then `make`. Alternatively you could build the project with [Qt Creator](https://www.qt.io/product/development-tools) if you installed it.

### Tests
A QTest-based unit suite lives in `tests/`. Build and run it with:

```
cd tests
qmake && make
./cdimage_tests
```

The Converter WAV-header tests each write ~800 MB and take a few minutes apiece. To skip them during fast iteration, set `CDIMAGE_SKIP_LONG=1`. CI runs the full suite with that variable set; release verification should run without it.

## Windows Users
Since many Windows users experienced problems with building it or did not want to install Qt and all the stuff, I've made a [Windows binary build](https://github.com/arduinocelentano/cdimage/releases).

On Windows, CDImage burns calibration test patterns natively using **IMAPI 2** (Image Mastering API v2), which is built into every Windows version since Vista — no additional software required. If you have `cdrecord` (cdrtools) installed and in your PATH, it will be used as a fallback should IMAPI initialisation fail, but it is no longer a prerequisite.

## Before you start
If your compact disc is not mentioned in the track generating dialog, you'll probably fail. All the discs are slightly different geometrically. This difference does not matter for data storage but dramatically influences image calculation.
You could try to guess geometry of an unknown disc and input it manually, but you'll probably spoil a lot of discs before you get some results. Moreover, all your test discs should be perfectly identical or you should use the same CD-RW disc. Sometimes discs of the same model happen to have different parameters. Don't know why. After all, manufacturers have never cared since those discs were never supposed to be used this way.

## Usage
1. Click **Edit→Load image** and select an image file. I recommend that you use a high-contrast image. If you choose a full color image, it will be converted to grayscale. You can adjust and scale your image:

* **left mouse button** — move image;

* **double click** — center image;

* **mouse scroll wheel** — zoom image.

2. Click **Edit→Create track** and pick a profile for your compact disc.

   Profiles are tagged in the dropdown:
   * `[Local]` — calibrated by you and saved to your local library.
   * `[Bundled]` — shipped with the application.

   If you've just calibrated a disc via the wizard, that profile is preselected automatically.

> If your disc is not in the list (which is likely), you can either input geometry manually or run the calibration wizard via **Edit→Detect disc geometry**. The wizard burns a test pattern, then measures the actual geometry either from a photograph of the disc or by drive read-back, and saves the result to your local profile library so you don't have to redo it next time. The library lives at the platform-standard `AppDataLocation/CDImage/profiles.json`. See [Considering Calibration](#considering-calibration) for the math.

Depending on your hardware, conversion will take some time. The output is a standard PCM WAV file (~800 MB — that's the size of a full Audio CD).

3. Burn the file as an **Audio CD** with any burning software:

   * **Nero Burning Rom**, **ImgBurn**, **Windows Media Player** — create an Audio CD project and add the WAV file.
   * **cdrecord** (Linux/Cygwin): `cdrecord -audio dev=<recorder_device> <generated_track.wav>`

Remember that you should create an **Audio CD**, not a data CD!

## Considering Calibration 
From the Mathematical point of view we have a sort of [multi-objective optimization](https://en.wikipedia.org/wiki/Multi-objective_optimization) problem. Bicriteria optimization, to be more precise. It means that two objective functions should be optimized simultaneously. If we define goal as getting a "neat image", we need an expert who is able to provide some feedback regarding image "quality". Which leads us to [interactive methods](https://en.wikipedia.org/wiki/Multi-objective_optimization#Solution).

The first and the most obvious idea is to select some series of equally spaced values for each criterium and burn a lot of discs with all possible combinations. If the space is narrow enough, you'll see something at some discs. Then narrow the range and repeat. A typical Computational Mathematics approach. A lot of iterations and time.

The number of iterations could be reduced if we gradually change criteria within one image from disc center to rim. Then one should look for areas where some distinctive fragments could be seen. The only implementation I know about was a part of [CD PAINT](http://undefer.narod.ru/cdpaint/index.html) project I mentioned. I thought it was gone, but finally found it. It was called `defcdparams`. Apparently CD PAINT project contributors used it to define geometry of four discs I know. However it is still a time consuming procedure. I think it was the main reason why CD PAINT project was abandoned.

At least for me it was the reason why I finally gave up. However I'd like to share some of my thoughts regarding possible improvements of calibration technique. The weakest component in the mentioned algorithm is the necessity of human expertise. How it could be automated?

1. If all the discs are geometrically different, then seek time delays would be different to. Because the same sector A is located at different angles for two different discs. So if we know "ideal" delays for a calibrated disc, we theoretically should be able to calibrate another one. But... These delays must be optical drive dependent. So this solution potentially would face some hardware issues.

2. I have not considered this option in 2008, but now we have more advanced image recognition algorithms and better cameras. So maybe some AI solution might be used instead of human expertise.

If you have other ideas, please share them.

## Further readings

[Probably the first implementation of similar technique](https://www.instructables.com/Burning-visible-images-onto-CD-Rs-with-data-beta/).

[The "Red Book"](https://www.ecma-international.org/wp-content/uploads/ECMA-130_2nd_edition_june_1996.pdf) (CD-ROM ECMA standard).

[Hackaday post](https://hackaday.io/project/186303-burning-pictures-on-a-compact-disc-surface) about this project.

[A project regarding optical drive reverse engineering](https://scanlime.org/2016/08/scanlime001-coastermelt-part-1/). Not related directly to this project, but might be useful to give you a clue to how it probably could be done with hardware approach. Thanks to [some guy](https://hackaday.com/2022/07/11/burn-pictures-on-a-cd-r-no-special-drive-needed/#comment-6491772) for suggestion.
