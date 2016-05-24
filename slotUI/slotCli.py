#!/usr/bin/env python
"""
Freeslot project
Command line interface
"""

from freeslot import Blackbox, LOGLEVEL
from optparse import OptionParser
from operator import itemgetter
from subprocess import Popen, PIPE
import sys, os
from copy import copy
import curses
from time import sleep
import ConfigParser

import SimpleXMLRPCServer
import xmlrpclib
import threading


VERSION = "1.8"
MAXSLOTS = 2
TERM = {
    "caption": "\033[1;37m\033[1;44m",
    "text": "\033[1;30m",
    }

# disable debug log output
LOGLEVEL = 10

SOUNDPREFIX = "quake-"
EVENTPREFIX = "event/"
SOUNDS = {
        "countdown_start": os.path.abspath(SOUNDPREFIX + "sound/countdown.mp3"),
        "race_start":      os.path.abspath(SOUNDPREFIX + "sound/racestart.mp3"),
        "race_prepare":    os.path.abspath(SOUNDPREFIX + "sound/prepare.mp3"),
        "lap_record":      os.path.abspath(SOUNDPREFIX + "sound/laprecord.mp3"),
#        "first_position":  os.path.abspath(SOUNDPREFIX + "sound/laprecord.mp3"),
        "fuel_warning1":   os.path.abspath(SOUNDPREFIX + "sound/fuel1.mp3"),
        "fuel_warning2":   os.path.abspath(SOUNDPREFIX + "sound/fuel2.mp3"),
        "fuel_full":       os.path.abspath(SOUNDPREFIX + "sound/fuel_full.mp3"),
        "pitlane_enter":   os.path.abspath(SOUNDPREFIX + "sound/pitlane_enter.mp3"),
        "pitlane_exit":    os.path.abspath(SOUNDPREFIX + "sound/pitlane_exit.mp3"),
        "data_error":      os.path.abspath(SOUNDPREFIX + "sound/data_error.mp3"),
        "panic":           os.path.abspath(SOUNDPREFIX + "sound/panic.mp3"),
        "panic_shortcut":  os.path.abspath(SOUNDPREFIX + "sound/panic_shortcut.mp3"),
        "resume":          os.path.abspath(SOUNDPREFIX + "sound/resume.mp3"),
        "win":             os.path.abspath(SOUNDPREFIX + "sound/win.mp3"),

    }

def trigger_sound(what):
    if what in SOUNDS:
        Popen(["/usr/bin/mpg123", "-q", SOUNDS[what]])
        #os.spawnlp(os.P_NOWAIT, "/usr/bin/mpg123", "mpg123", SOUNDS[what])
        #Popen(["/usr/bin/mpg123", SOUNDS[what]]).pid

def trigger_event(what, slot = 0):
    trigger_sound(what)
    Popen(["/bin/sh", os.path.abspath(EVENTPREFIX + what), str(slot)])

class SlotServer(threading.Thread):
    def __init__(self, blackbox):
        threading.Thread.__init__(self)
        self.server = SimpleXMLRPCServer.SimpleXMLRPCServer(("localhost", 8000))
        self.server.register_instance(blackbox)
        #self.server.register_function(lambda astr: '_' + astr, '_string')
        self.daemon = True

    def run(self):
        self.server.serve_forever()

class SlotClient():
    def __init__(self, url):
        self.box = xmlrpclib.Server(url)

class SlotCli():
    def __init__(self, test = None, dev=""):
        self.box = Blackbox()
        self.nofuel = False
        self.pitfinish = False
        if (not test):
            self.box.connect(dev)
            self.rpcserver = SlotServer(self.box)
            self.rpcserver.start()
        self.slot_dummy = {
            "name": "uninitialized",
            "laps": 0,
            "laps_last": 0,
            "last": 0.00,
            "best": 0.00,
            "fuel": 0,
            "fuel_last": 0,
            "position": 0,
            "drive": 0,
            "status": "Idle",
            "clk": 0,
            "car": 0,
            "limit": 15,
            "profilename": "default",
            "profile": ConfigParser.RawConfigParser(),
            }
        self.slot_dummy["profile"].read("profiles/" + self.slot_dummy["profilename"])
        self.slot_dummy["name"] = self.slot_dummy["profile"].get("Settings", "Name")
        self.slot_dummy["limit"] = self.slot_dummy["profile"].getint("Settings", "Limit")

        self.slot = [
            copy(self.slot_dummy), copy(self.slot_dummy),
            copy(self.slot_dummy), copy(self.slot_dummy),
            copy(self.slot_dummy), copy(self.slot_dummy),
            ]
        self.reset_slots()
        self.sysclk = 0.00
        self.sysclk_last = 0.00
        self.bestlap = 9999999.00 # best lap time
        self.test = test
        self.laplimit = 999 # race laplimit
        self.timelimit = 0 # race timelimit
        self.firstpos = -1 # first position slot
        self.freerun = True # freerun mode = sort order by best lap

    def reset_slots(self):
        idx = 0
        for slt in self.slot:
            slt["laps"] = 0
            slt["laps_last"] = 0
            slt["last"] = 999.00
            slt["best"] = 999.00
            slt["fuel"] = 100
            slt["fuel_last"] = 0
            slt["position"] = idx
            slt["car"] = idx # used for sort order calculation
            slt["status"] = self.slot_dummy["status"]
            slt["clk"] = 0
            slt["limit"] = slt["profile"].getint("Settings", "Limit")
            idx += 1
        self.bestlap = 99999.00
        self.raceactive = False

    def update_positions(self):
        order1 = sorted(self.slot, key=itemgetter(
            "clk"))
        if self.freerun:
            order2 = sorted(self.slot, key=itemgetter(
                "best"))
        else:
            order2 = sorted(self.slot, key=itemgetter(
                "laps"), reverse=True)
        idx = 1
        for tst in order2:
            self.slot[tst["car"]]["position"] = idx
            # check if first position changed
            if (self.firstpos != tst["car"]) and (idx == 1):
                self.firstpos = tst["car"]
                trigger_event("first_position", tst["car"] + 1)
            idx += 1

    def render_slots(self):
        self.update_positions()
        self.scr.addstr(3,0,
            #"Pos | #/Name            | Laps | Best     | Last     | Fuel | Status    ",
            "Pos | #/Name                                                            ",
            curses.color_pair(2))
        self.scr.addstr(4,4,
            "  Laps | Best     | Last     | Fuel | Status                        ",
            curses.color_pair(2))
        for idx in range(MAXSLOTS):
            """
            self.scr.addstr((3 + (self.slot[idx]["position"] * 2)), 0,
                "%3i | %i %15s | %4i | %7.2fs | %7.2fs | %3i%% | %10s" % (
                self.slot[idx]["position"],
                self.slot[idx]["car"] + 1, self.slot[idx]["name"],
                self.slot[idx]["laps"],
                self.slot[idx]["best"],
                self.slot[idx]["last"],
                self.slot[idx]["fuel"],
                self.slot[idx]["status"],
                ),
                curses.color_pair(11 + idx) )
            """
            if idx > 3:
                namesuffix = " (%i)" % self.slot[idx]["drive"]
            else:
                namesuffix = ""

            self.scr.addstr((3 + (self.slot[idx]["position"] * 2)), 0,
                "%3i | %i %15s %48s" % (
                self.slot[idx]["position"],
                self.slot[idx]["car"] + 1, (self.slot[idx]["name"] + namesuffix),
                "",
                ),
                curses.color_pair(11 + idx) )
            self.scr.addstr((4 + (self.slot[idx]["position"] * 2)), 4,
                "  %4i | %7.2fs | %7.2fs | %3i%% | %10s | %2i% 15s" % (
                self.slot[idx]["laps"],
                self.slot[idx]["best"],
                self.slot[idx]["last"],
                self.slot[idx]["fuel"],
                self.slot[idx]["status"],
                self.slot[idx]["limit"],
                ""
                ),
                curses.color_pair(11 + idx) )

    def cleartop(self):
        self.scr.addstr(0,0, "%60s" % "Race Limits: %i Laps / %i Minutes" % (self.laplimit, self.timelimit))
        self.scr.addstr(1,0, "%80s" % " ")

    def flash_car_settings(self, slot):
        # write current settings to car firmware
        self.box.setmode(0)
        self.scr.addstr(1,0, "%70s" % "Writing settings for %s to car %i, PLEASE WAIT...          " % (
            self.slot[slot]["name"],
            slot + 1),
            curses.color_pair(9))
        self.scr.refresh()
        if not self.nofuel:
            self.box.progcar(slot, "fuel", 0)
            sleep(0.5)
        self.box.progcar(slot, "accel", self.slot[slot]["profile"].getint("Settings", "Accel"))
        sleep(0.5)
        self.box.progcar(slot, "brake", self.slot[slot]["profile"].getint("Settings", "Brake"))
        sleep(0.5)
        self.box.speedlimit(slot, self.slot[slot]["limit"])
        sleep(0.5)
        self.cleartop()
        self.box.setmode(1)

    def readName(self, slot):
        self.scr.nodelay(0) # enable delay on readkey
        curses.echo()
        self.scr.addstr(0,0, "Enter Name for Controller %i [%s]:" % (
            slot + 1,
            self.slot[slot]["name"]),
            curses.color_pair(1))
        self.scr.refresh()
        name = self.scr.getstr(1,0, 15)
        if name != "":
            # look if profile with that name found
            try:
                with open("profiles/" + name) as f: pass
                self.slot[slot]["profilename"] = name
                self.slot[slot]["profile"].read("profiles/" + name)
                self.slot[slot]["name"] = self.slot[slot]["profile"].get("Settings", "Name")
                self.slot[slot]["limit"] = self.slot[slot]["profile"].getint("Settings", "Limit")
                self.flash_car_settings(slot)
            except IOError, err:
                self.slot[slot]["profilename"] = "default"
                self.slot[slot]["profile"].read("profiles/default")
                self.slot[slot]["limit"] = self.slot[slot]["profile"].getint("Settings", "Limit")
                self.slot[slot]["name"] = name
                self.flash_car_settings(slot)
        self.cleartop()
        self.scr.refresh()
        curses.noecho()
        self.scr.nodelay(1) # disable delay on readkey

    def readLimit(self, slot):
        limit = self.readInt("SPEEDLIMIT for %s (%i)" % (
            self.slot[slot]["name"],
            slot + 1),
            self.slot[slot]["limit"], 15)
        if limit:
            self.slot[slot]["limit"] = limit
            self.cleartop()
            self.box.speedlimit(slot, limit)


    def readInt(self, msg, default, maximum = 999999):
        self.scr.nodelay(0) # enable delay on readkey
        curses.echo()
        self.scr.addstr(0,0, "%s [%i]:" % (
            msg,
            default),
            curses.color_pair(1))
        self.scr.refresh()
        inp = self.scr.getstr(1,0, 4)
        if inp != "":
            try:
                inp = int(inp)
                if inp > maximum: inp = maximum
            except Exception:
                inp = None
        else:
            inp = None
        self.cleartop()
        self.scr.refresh()
        curses.noecho()
        self.scr.nodelay(1) # disable delay on readkey
        return inp


    def monitor_init(self, live = 1):
        """
        Send initializing commands for live monitoring
        """
        if self.nofuel:
            #print cli.box.fueldivisor(0)
            self.box.query("F0\n") # set fuel logic disabled
        else:
            #print cli.box.fueldivisor(25)
            self.box.query("F1\n") # set fuel logic enabled

        if self.pitfinish:
            self.box.query("X1\n") # set pitlane finish function
        else:
            #print cli.box.fueldivisor(25)
            self.box.query("X0\n")

        self.box.query("*%i\n" % live) # set live fuel info

    def monitor_learn(self, slot):
        # clear garbage in UART rx buffer
        self.box.query("*0\n") # set live fuel info
        self.box.query("*0\n") # set live fuel info
        while self.box.readline() != "": pass

        trk = False
        spd = 0
        trk_old = False
        spd_old = 0
        clock = -1

        self.monitor_init(slot + 2)
        while 1:
            #key = self.scr.getch()
            #if key == ord('c'): break

            # is there something in the rx buffer?
            rx = self.box.readline()
            if (rx != ""):
                try:
                    data = rx.split(":")
                    if rx[:3] == "LN:":
                        if clock >= 0:
                            clock += 1
                        spd = int(data[1], 16)
                        trk = (data[2] != 'X')
                        if (spd != spd_old) or (trk != trk_old):
                            if clock < 0:
                                clock = 0
                            print "%i,%i,%s" % (clock, spd, trk)
                        trk_old = trk
                        spd_old = spd * 1
                    if rx[:2] == "L:":
                        # update lap time info
                        l = int(data[2], 16)
                        s = int(data[3]) - 1
                        t = int(data[4], 16) / 2000.00
                        if (slot == s):
                            print "# lap %i complete: %3.2f seconds" % (l, t)
                            clock = 0
                            print "%i,%i,%s" % (clock, spd, trk)
                except:
                    print "RX ERROR: " % rx

    def monitor_playback(self, slot, filename):
        # clear garbage in UART rx buffer
        self.box.query("*0\n") # set live fuel info
        self.box.query("*0\n") # set live fuel info
        sleep(1)
        cli.box.speedminimum(slot, 0 )
        while self.box.readline() != "": pass

        clock = -5
        trkfile = open(filename, "r").readlines()
        print "Loading %s..." % filename

        while 1:
            try:
                for l in trkfile:
                    l = l.strip()
                    if (l != "") and (l[:1] != "#"):
                        print "Line: %s" % repr(l)
                        data = l.split(",")
                        speed = int(data[1])
                        while (clock < int(data[0]) and (int(data[0]) > 0)):
                            clock += 1
                            sleep(0.07)
                        print "CLK %i/%i -> set: %i" % (clock, int(data[0]), speed)
                        cli.box.speedminimum(slot, speed )
                # now wait for lap sync :)
                while self.box.readline() != "": pass
                rx = ""
                while rx[:2] != "L:":
                    rx = self.box.readline()
                data = rx.split(":")
                l = int(data[2], 16)
                s = int(data[3]) - 1
                t = int(data[4], 16) / 2000.00
                print "# lap %i complete: %3.2f seconds" % (l, t)
                clock = -3
            except Exception, e:
                print repr(e)
                sys.exit(1)
            except KeyboardInterrupt:
                print "resetting"
                cli.box.speedminimum(slot, 0 )
                cli.box.speedminimum(slot, 0 )
                sys.exit(0)
                


    def monitor(self):
        """
        Live Monitor on the console
        Keyboard loop to control it???
        """
        # clear garbage in UART rx buffer
        while self.box.readline() != "": pass

        self.monitor_init()
        self.scr = curses.initscr()
        curses.start_color()
        curses.init_pair(1, curses.COLOR_WHITE, curses.COLOR_BLACK) # standard text
        curses.init_pair(2, curses.COLOR_WHITE, curses.COLOR_BLUE) # label
        curses.init_pair(9, curses.COLOR_WHITE, curses.COLOR_RED) # ATTENTION
        curses.init_pair(11, curses.COLOR_BLACK, curses.COLOR_YELLOW) # player 1 slot
        curses.init_pair(12, curses.COLOR_WHITE, curses.COLOR_BLACK) # player 2 slot
        curses.init_pair(13, curses.COLOR_BLACK, curses.COLOR_RED) # player 3 slot
        curses.init_pair(14, curses.COLOR_BLACK, curses.COLOR_MAGENTA) # player 4 slot
        curses.init_pair(15, curses.COLOR_WHITE, curses.COLOR_BLACK) # player 5 slot
        curses.init_pair(16, curses.COLOR_WHITE, curses.COLOR_BLACK) # player 6 slot
        curses.noecho() # disable key echo
        curses.cbreak() # do not buffer keypresses
        self.scr.keypad(1) # enable special keys
        self.scr.nodelay(1) # disable delay on readkey

        self.cleartop()
        self.render_slots()
        self.scr.refresh()


        while 1:
            key = self.scr.getch()
            if key == ord('q'): break
            elif key == ord(' '): self.box.query("+") # panic / resume
            elif key == 10: 
                self.freerun = False
                self.box.query("#") # remote start button press

            elif key == ord('1'): self.readName(0)
            elif key == ord('2'): self.readName(1)
            elif key == ord('3'): self.readName(2)
            elif key == ord('4'): self.readName(3)
            elif key == ord('5'): self.readName(4)
            elif key == ord('6'): self.readName(5)
            #elif key == ord('q'): self.readLimit(0)
            #elif key == ord('w'): self.readLimit(1)
            #elif key == ord('e'): self.readLimit(2)
            #elif key == ord('r'): self.readLimit(3)
            #elif key == ord('t'): self.readLimit(4)
            #elif key == ord('z'): self.readLimit(5)
            elif key == ord('a'):
                if self.slot[4]["drive"] > 0: self.slot[4]["drive"] -= 1
                cli.box.speedminimum(4, self.slot[4]["drive"])
            elif key == ord('s'):
                if self.slot[4]["drive"] < 16: self.slot[4]["drive"] += 1
                cli.box.speedminimum(4, self.slot[4]["drive"])
            elif key == ord('y'):
                if self.slot[5]["drive"] > 0: self.slot[5]["drive"] -= 1
                cli.box.speedminimum(5, self.slot[5]["drive"])
            elif key == ord('x'):
                if self.slot[5]["drive"] < 16: self.slot[5]["drive"] += 1
                cli.box.speedminimum(5, self.slot[5]["drive"])
            elif key == ord('t'):
                tmp = self.readInt("Set new Race TIME limit", self.timelimit)
                if tmp: self.timelimit = tmp
                self.cleartop()
            elif key == ord('l'):
                tmp = self.readInt("Set new Race LAP limit", self.laplimit)
                if tmp: self.laplimit = tmp
                self.cleartop()
            elif key == ord('/'):
                for slot in range(5):
                    self.flash_car_settings(slot)
            elif key == ord('f'):
                # set freerun mode manually (abort race)
                self.freerun = not self.freerun


            # is there something in the rx buffer?
            rx = self.box.readline()
            if (rx != "") or self.test:
                self.scr.addstr(17,3,
                    "RX: %19s" % rx, curses.color_pair(2))
                self.scr.redrawwin()
                self.scr.refresh()
                # we have received something
                try:
                    data = rx.split(":")
                    if rx[:2] == "L:":
                        # update lap time info
                        l = int(data[2], 16)
                        slot = int(data[3]) - 1
                        t = int(data[4], 16) / 2000.00
                        self.sysclk = int(data[5], 16) / 2000.00
                        self.slot[slot]["laps_last"] = self.slot[slot]["laps"]
                        self.slot[slot]["laps"] = l
                        self.slot[slot]["last"] = t
                        self.slot[slot]["clk"] = self.sysclk
                        if (self.slot[slot]["best"] > t) or (self.slot[slot]["best"] == 0):
                            self.slot[slot]["best"] = t
                        if self.bestlap > t:
                            if self.freerun:
                                trigger_event("lap_record", slot + 1)
                            self.bestlap = t

                        self.slot[slot]["status"] = "IN-RACE"
                        if (self.slot[slot]["laps_last"] != l) and (l == self.laplimit):
                            # we have lap limit reached!
                            trigger_event("win", slot + 1)
                            self.raceactive = False
                            self.slot[slot]["status"] = "WINNER!"
                            self.box.query("+") # stop race

                        self.render_slots()

                    if rx[:2] == "F:":
                        # update fuel level
                        slot = int(data[1])
                        f = int(data[2], 16)
                        f = f / 100 # fuel in percent
                        self.sysclk = int(data[3], 16) / 2000.00
                        self.slot[slot]["fuel_last"] = self.slot[slot]["fuel"]
                        self.slot[slot]["fuel"] = f
                        if self.slot[slot]["fuel_last"] != f:
                            if (self.slot[slot]["fuel_last"] == 16) and (f == 15):
                                # 15 percent fuel, set speed limit for car to 8
                                # warning sound
                                trigger_event("fuel_warning1", slot + 1)
                                cli.box.speedlimit(slot, 8)
                            if (self.slot[slot]["fuel_last"] == 6) and (f == 5):
                                # 5 percent, set speed limit for car to 6
                                # warning sound
                                trigger_event("fuel_warning2", slot + 1)
                                cli.box.speedlimit(slot, 6)
                            if (self.slot[slot]["fuel_last"] == 1) and (f == 0):
                                # fuel empty
                                # set speedlimit to 4
                                cli.box.speedlimit(slot, 4)
                            if (self.slot[slot]["fuel_last"] < f) and (f >= 11) and (f < 20):
                                cli.box.speedlimit(slot, 15)
                            if (self.slot[slot]["fuel_last"] < f) and (f == 100):
                                trigger_event("fuel_full", slot + 1)
                        self.render_slots()

                    if rx[:1] == "~":
                        # jumpstart occured
                        slot = int(rx[1:2])
                        t = int(data[1], 16) / 2000.00
                        self.slot[slot]["jumpstart"] = t
                        self.slot[slot]["status"] = "Jumpstart!"
			self.render_slots()

                    if rx[:3] == "RW:":
                        # ResponseWire packet, do nothing at the moment, just decode
                        slot = int(data[1])
                        devtype = int(data[2])
                        sender = int(data[3], 16)
                        status = int(data[4], 16)
                        self.sysclk = int(data[5], 16)
                        if (devtype == 4):
                            # pitlane sent something
                            if (status == 5):
                                self.slot[slot]["status"] = "PITLANE"
                                trigger_event("pitlane_enter", slot + 1)
                            if (status == 7):
                                self.slot[slot]["status"] = "IN-RACE"
                                trigger_event("pitlane_exit", slot + 1)

                        self.render_slots()

                    if rx == "!RACE PREPARE":
                        # reset current race status
                        # and display PREPARE PHASE
                        self.reset_slots()
                        for slot in range(MAXSLOTS):
                            self.slot[slot]["status"] = "Prepare"
                        trigger_event("race_prepare")

                    if rx == "!RACE START":
                        for slot in range(MAXSLOTS):
                            if self.slot[slot]["status"] == "~~~~~~~~~~":
                                self.slot[slot]["status"] = "Idle"
                        trigger_event("race_start")
                        self.raceactive = True

                    if rx == "!COUNTDOWN":
                        # countdown initiated
                        for slot in range(MAXSLOTS):
                            self.slot[slot]["status"] = "~~~~~~~~~~"
                        trigger_event("countdown_start")

                    if rx == "!PANIC":
                        # panic mode
                        trigger_event("panic")

                    if rx == "!SHORTCUT":
                        # panic mode
                        trigger_event("panic_shortcut")

                    if rx == "!RESUME":
                        # panic mode
                        trigger_event("resume")


                    if ((self.timelimit > 0) and (self.raceactive) and 
                        (self.sysclk_last != self.sysclk) and 
                        ((self.sysclk / 60) >= self.timelimit)):
                        self.sysclk_last = self.sysclk
                        self.raceactive = False
                        # we have time limit reached!
                        self.box.query("+") # stop race
                        trigger_event("win")
                        # get the one with position 1
                        for slot in self.slots:
                            if slot["position"] == 1:
                                slot["status"] = "WINNER!"
                        self.render_slots()
                    self.sysclk_last = self.sysclk



                    self.scr.addstr(17,31,
                        "Race Timer: %7.3f min" % (self.sysclk / 60),
                        curses.color_pair(2))

                    tmpmode = "RACE MODE (LAPS)"
                    if self.freerun:
                        tmpmode = "FREERUN (BEST)"
                    self.scr.addstr(18,31,
                        "%19s" % tmpmode, curses.color_pair(2))

                    self.scr.refresh()

                except Exception:
                    trigger_event("data_error")
                    pass

        # terminate
        curses.nocbreak()
        self.scr.keypad(0)
        curses.echo()
        curses.endwin()
        return None

    def cyclemode(self):
        pass

if __name__ == "__main__":
    parser = OptionParser(version="%prog " + VERSION)
    parser.add_option("--live", dest="live", action="store_true", default=False,
        help="Run Live monitor on console", metavar="[0-5]")
    parser.add_option("--nofuel", dest="nofuel", action="store_true", default=False,
        help="Disable Freeslot fuel management", metavar="[0-5]")
    parser.add_option("--pit", dest="pitfinish", action="store_true", default=False,
        help="Pitlane entry acts as finish line too", metavar="[0-5]")
    parser.add_option("--learn", dest="learn", action="store_true", default=False,
        help="Run Learning mode for [slot]", metavar="[0-5]")
    parser.add_option("--teach", dest="playback", 
        help="Playback teach file", metavar="[filename]")

    parser.add_option("--slot", dest="carid",
        help="Required for programming a car directly", metavar="[1-6]")
    parser.add_option("--fuel", dest="fuel",
        help="Set maximum CAR fuel level", metavar="[0-15]")
    parser.add_option("--brake", dest="brake",
        help="Set CAR brake strength", metavar="[0-15]")
    parser.add_option("--accel", dest="accel",
        help="Set CAR acceleration ", metavar="[6-15]")
    parser.add_option("--blink", dest="blink",
        help="Set car lights blinking state", metavar="[on|off]")
    parser.add_option("--limit", dest="limit",
        help="Controlled SPEED LIMIT (15 = no limit)", metavar="[0-15]")
    parser.add_option("--drive", dest="drive",
        help="Controlled SPEED MINIMUM (0 = disabled)", metavar="[0-15]")
    parser.add_option("--test", dest="test", action="store_true", default=False,
        help="", metavar="")
    parser.add_option("--dev", dest="dev", default="/dev/ttyUSB0",
        help="Communication port", metavar="[/dev/ttyUSB0]")

    (options, args) = parser.parse_args()
    #if not options.dev:
    #    options.dev = "/dev/ttyUSB0"

    if options.live or options.learn or options.playback:
        cli = SlotCli(options.test, options.dev)
    else:
        cli = SlotClient('http://localhost:8000')
    # should a CLI function be started?

    if options.live:
        # start the live monitor
        cli.nofuel = options.nofuel
        cli.pitfinish = options.pitfinish
        cli.monitor()
        sys.exit(0)

    # check commandline if we have to program something
    if not options.carid:
        print "Option --slot is required for all car programming commands!\nUse --help to get a list of available commands"
        sys.exit(1)
    else:
        options.carid = int(options.carid) - 1
        if (options.carid < 0) or (options.carid > 6):
            print "Error: Invalid slot selected"
            sys.exit(1)

    if options.learn:
        # start the learn monitor
        cli.monitor_learn(options.carid)
        sys.exit(0)

    if options.playback:
        # start the playback monitor
        cli.monitor_playback(options.carid, options.playback)
        sys.exit(0)

    if options.fuel:
        print "setFuel: " + cli.box.progcar(int(options.carid), "fuel", int(options.fuel))

    if options.accel:
        print "setAccel: " + cli.box.progcar(int(options.carid), "accel", int(options.accel))

    if options.brake:
        print "setBrake: " + cli.box.progcar(int(options.carid), "brake", int(options.brake))

    if options.blink:
        state = False
        if options.blink == "on":
            state = True
        print "setBlink: " + cli.box.blinkcar(int(options.carid), state)

    if options.limit:
        print "Change Speed Limit: " + cli.box.speedlimit(int(options.carid), int(options.limit))

    if options.drive:
        print "Change minimum Speed drive: " + cli.box.speedminimum(int(options.carid), int(options.drive))

