#!

HEX=`dirname $0`/`basename $0 .sh`.hex
echo $HEX

if [ ! -r $HEX ] ; then
  echo "Cannot read file $HEX"
  exit
fi

#pulse GPIO16 to reset Arduino
[ ! -d /sys/class/gpio/gpio16 ] && echo 16 >/sys/class/gpio/export
echo out >/sys/class/gpio/gpio16/direction
echo 1 >/sys/class/gpio/gpio16/value
(
sleep 0.3
echo 0 >/sys/class/gpio/gpio16/value
echo 1 >/sys/class/gpio/gpio16/value
) &

#send hex file to Arduino bootloader
avrdude \
  -F \
  -vv \
  -pm328p \
  -carduino \
  -P/dev/ttySP1 \
  -b115200 \
  -D \
  -Uflash:w:$HEX #:i 

