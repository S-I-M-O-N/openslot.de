"""
FreeSlot project
Blackbox communication library
"""

import serial, sys, string, time

# how often should a command retried when busy?
RETRIES = 10

# define loglevels
DEBUG = 20
LOGLEVEL = 20
def log(level, msg):
    """
    Logging output function
    """
    if level <= LOGLEVEL: print msg

class SerialCommunicator():
    def __init__(self, device, speed):
        self.device = device
        self.speed = speed
        self.com = None
        self.connected = False

    def connect(self):
        if self.connected:
            return True
        try:
            self.com = serial.Serial(self.device, baudrate=self.speed, 
                xonxoff=0, timeout=0.1)
        except serial.SerialException, err:
            print err
            sys.exit(1)
        self.connected = True
        return True

    def disconnect(self):
        self.com = None
        return True

    def write(self, msg, getanswer=False):
        self.com.write(msg + "\n")
        if getanswer:
            return self.readline()
        return None

    def readline(self):
        answer = self.com.readline()
        return string.strip(answer, "\n")

    def query(self, msg):
        retry = 0
        response = self.write(msg, True)
        while (retry < RETRIES) and (response == "BUSY"):
            time.sleep(0.1)
            response = self.write(msg, True)
            retry += 1
        log( DEBUG, "%i> %s\n< %s" % (retry, msg, response) )
        return response


class Blackbox():
    def __init__(self):
        self.com = None
        self.info = None

    def readline(self):
        if self.com:
            return self.com.readline()
        return ""

    def query(self, msg):
        if self.com:
            return self.com.query(msg)
        return ""

    def connect(self, device="/dev/ttyUSB0", speed=57600):
        if self.com == None:
            self.com = SerialCommunicator(device, speed)
        if self.com.connected:
            self.com.disconnect()
        self.com.connect()
        self.info = self.readinfo()

    def disconnect(self):
        self.com.disconnect()

    def readinfo(self):
        """
        Read complete Information from connected box
        This does not include race+car status!
        """
        return None

    def progcar(self, carid, command, value):
        """
        Send program packets to specified car id
        valid command: speed, brake, fuel
        valid value: 4 bit integer (0..15)
        valid carid: 0..5
        """
        if (carid < 0) or (carid > 5):
            return "ERR - invalid carid"
        cmd = -1
        if command == "accel":
            cmd = 0
        if command == "brake":
            cmd = 1
        if command == "fuel":
            cmd = 2
        if (cmd == -1):
            return "ERR - invalid command"
        if (value<0) or (value>15):
            return "ERR - invalid value"
        if command == "accel" and value < 6:
            return "ERR - value too low"
        # transform value 10..15 to A..F
        if (value>9):
            value = chr(ord("A") + (value-10))
        command = "P%i%s%i" % (cmd, value, carid)
        response = self.com.query( command )
        return response

    def blinkcar(self, carid, blink):
        """
        Set car blinking state
        """
        if (carid < 0) or (carid > 5):
            return "ERR - invalid carid"
        if blink:
            return self.com.query( "P48%i" % carid )
        else:
            return self.com.query( "P40%i" % carid )

    def speedlimit(self, carid, value):
        """
        Set the maximum controller speed for a car
        Attention: this is software limited, this does not affect car acceleration!
        """
        if (carid < 0) or (carid > 5):
            return "ERR - invalid carid"
        if (value<0) or (value>15):
            return "ERR - invalid value"
        # transform value 10..15 to A..F
        if (value>9):
            value = chr(ord("A") + (value-10))
        return self.com.query( "L%i%s" % (carid, value) )

    def speedminimum(self, carid, value):
        """
        Set the minimzm controller speed for a car
        """
        if (carid < 0) or (carid > 5):
            return "ERR - invalid carid"
        if (value<0) or (value>15):
            return "ERR - invalid value"
        # transform value 10..15 to A..F
        if (value>9):
            value = chr(ord("A") + (value-10))
        return self.com.query( "S%i%s" % (carid, value) )

    def fueldivisor(self, value):
        """
        Set the minimzm controller speed for a car
        """
        if (value<0) or (value>255):
            return "ERR - invalid value"
        return self.com.query( "F:%s" % (value) )


    def setmode(self, mode):
        """
        Switch the Blackbox mode
        Valid modes are: idle, prepare, race
        note: box will permanently send status info in race mode, so no
              polling is required
        """
        return True

    def getmode(self):
        self.readinfo()
        return self.info["mode"]