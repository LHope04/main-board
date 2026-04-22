import serial
import sys
import time
import os

port = sys.argv[1] if len(sys.argv) > 1 else "COM10"
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
log_path = os.path.join(os.path.dirname(__file__), "serial_log.txt")

print(f"Monitoring {port} @ {baud}, log -> {log_path}")
with serial.Serial(port, baud, timeout=1) as ser, open(log_path, "w") as f:
    while True:
        line = ser.readline()
        if line:
            text = line.decode("utf-8", errors="replace").rstrip()
            print(text)
            f.write(text + "\n")
            f.flush()
