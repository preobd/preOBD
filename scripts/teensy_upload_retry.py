"""
PlatformIO post-script: retry the Teensy loader once (#461).

On the bench (Teensy 4.0, macOS) the soft reboot reliably reaches the HalfKay
bootloader and it stays there, but teensy_loader_cli's FIRST write errors
("error writing to Teensy") ~0.4 s into programming, every time; an immediate
second run programs normally. It's the host loader, not the firmware (the
armed watchdog was ruled out: HalfKay stayed up for 8 s against a 2 s WDOG1).

So: if the upload fails, run the loader again without -s (the board is
already in HalfKay). A board that genuinely can't be reached still fails,
after the second -w wait is interrupted or the button is pressed.
"""
Import("env")  # noqa: F821  (SCons global)

if env.subst("$UPLOAD_PROTOCOL") == "teensy-cli":  # noqa: F821
    env.Replace(  # noqa: F821
        UPLOADCMD=(
            "$UPLOADER $UPLOADERFLAGS $SOURCES"
            " || $UPLOADER -mmcu=$BOARD_MCU -w -v $SOURCES"
        )
    )
