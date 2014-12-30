holidayduino
============

Holiday "Arduino" firmware (with 20MHz related bootloader, etc)

As of v05 can be compiled for 16MHz or 20MHz crystal

Must use included boards.txt in Arduino environment for compilation for Holiday target

For production programming, combine the application hex file and optiboot hex file into one

v06:
I believe this was a fail - no actual code changes, except version # and switching to FastSPI_LED2 RC4.

v07:
Changed to use FastSPI_LED2 RC5 with minor changes to optimise timing for 20MHz ATmega which can thus do exact multiples of 50ns (16MHz can only do multiples of 62.5ns).

v08:
In addition to supporting longer strings (currently a max of 240), and a few other minor changes, it now reinitialises the ATmega SPI slave on each packet.  Occasionally a process could be terminated while in the middle of sending an SPI byte, and HolidayDuino wouldn’t resynchronise to the start of the new byte.  This probably won’t ever happen with apps using the compositor as they would typically exit cleanly, but it could happen with rainbow.  There is also an minor hardware change that could help with this, but the software fix seems to work.
