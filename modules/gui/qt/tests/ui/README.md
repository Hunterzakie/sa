# vlc-gui-testing

This is a project to test the VLC GUI 

It is compatible with both Windows and Linux

In Windows, it uses Appium, which allows to automate desktop applications using Selenium.
The driver that is used is the NovaWindows driver.

In Linux, it uses dogtail, an RHEL project that allows to automate Linux Desktop apps. It works with both X11 and Wayland

## Prerequisites on Windows

- Appium
- NovaWindows driver
- QWERTY Layout configured on Windows

## How to use 

**Clone repository:**

```bash
$ git clone --recurse-submodules https://code.videolan.org/videolan/vlc
```

Go to modules/gui/qt/tests/ui

**Create and use venv:**

```bash
$ python -m venv .venv
$ source .venv/bin/activate
```

**Install dependencies:**

```bash
$ python3 bootstrap.py
$ pip install .
```

**Write or use tests:**

You need to have a "tests" subdirectory containing .py test files. An example of our current tests is already present in this directory

**Run main.py file with desired parameters.**

```bash
$ python main.py -p VLC_PATH
```

You can select a particular test to run by adding "tests.\[Test file without .py\].\[Class name\].\[Test function name\]" as an argument.

To see all available parameters:

```bash
$ python main.py -h
```
