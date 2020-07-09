Pre-compiled binaries,
Use **esptool.py** to upload.
Example commands commands executed from current directory:<br>

    $ esptool.py --baud 460800 write_flash 0x0 ./firmware.bin
    $ esptool.py --baud 921600 write_flash 0x0 .pio/build/emonesp-dc/firmware.bin
