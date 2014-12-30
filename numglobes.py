#!/usr/bin/python2
import sys, os, serial, time

hdev = "/dev/ttySP1"

if len(sys.argv) < 1 or len(sys.argv) > 2:
  print "Usage: " + sys.argv[0] + " <num_globes>"
  sys.exit(1)

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

resp = resp.strip()

if not resp.startswith("HolidayDuino"):
  print "Unexpected response: " + resp
  hd.close()
  sys.exit(2)

print "Response: " + resp

if len(sys.argv) == 2:
  hd.write(sys.argv[1])
hd.write("L")
resp = hd.readline()
resp = resp.strip()

print "Response: " + resp

hd.close()

if len(sys.argv) == 2 and resp == sys.argv[1]:
  print "Rebooting HolidayDuino"
  time.sleep(1)
  os.system("/home/holiday/scripts/reduino.sh")

