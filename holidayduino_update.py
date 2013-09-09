#!/usr/bin/python2
import sys, os, serial

hdev = "/dev/ttySP1"
latest = "05"

if not os.path.exists(hdev):
  print "Device " + hdev + " not found"
  sys.exit(1) 

hd = serial.Serial()
hd.port = hdev
hd.baudrate = 115200
hd.timeout = 1
hd.open()
hd.flushInput()
hd.write("?")
resp = hd.readline()

if resp == "":
  hd.baudrate = 57600
  hd.flushInput()
  hd.write("?")
  resp = hd.readline()

hd.close()

resp = resp.strip()

if not resp.startswith("HolidayDuino"):
  print "Unexpected response: " + resp
  sys.exit(2) 

freq=""
baud=""
vers = resp[12:]

check = vers.split("-")
if len(check) == 2:
  vers = check[0]
  freq = check[1]

if vers == latest:
  print "Already at latest: " + resp
  sys.exit(0)
# version 01 was 16MHz only
# and used Duemilanove bootloader at 57600
elif vers == "01":
  freq="16"
  baud="57600"
# version 02 thru 04 were 20MHz only using optiboot at 115200
elif vers == "02" or vers == "03" or vers == "04":
  freq="20"
  baud="115200"
# later version numbers specify frequency (or should...)
elif freq == "":
  print "Unknown: " + resp
  sys.exit(3)
else:
  baud="115200"

if freq != "16" and freq != "20":
  print "Unable to determine frequency: " + resp
  sys.exit(4)

if baud == "":
  print "Unable to determine baud rate: " + resp
  sys.exit(5)

newfw = "HolidayDuino" + latest + "-" + freq + ".hex"

print "Upgrading from: " + resp
print "Using: " + newfw

sys.exit(os.system("./holidayduino_upload.sh " + newfw + " " + baud))

