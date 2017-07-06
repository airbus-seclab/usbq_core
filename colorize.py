#!/usr/bin/python -u
# -*- coding: utf-8 -*-

""" Template python program

Usage
  Usage Text

Help
  Help extract

Requirements
  python-argparse
"""

import sys
import re
import time

class Color:
    black = 0
    red = 160
    green = 28
    yellow = 220
    blue = 12
    purple = 126
    cyan = 45
    grey = 239

    normal = 0
    bold = 1

def colorize(s,color):
    if type(color) is tuple:
        color,modif = color
        return "\033[%dm\x1b[38;5;%dm%s\x1b[0m\033[0m" % (modif,color,s)
    else:
        return "\x1b[38;5;%dm%s\x1b[0m" % (color,s)

DBG  =   0x0f
DBG1 =   0x0e
DBG2 =   0x0d
INFO =   0x10
NOTICE = 0x20
WRN =    0x30
ERR =    0x40
ASSERT = 0xFF
SPEC = 0xFE

class Log:
    def is_my_log(self,l):
        return True

    def sanitize(self,l):
        return l.strip()

    def output(self,l):
        l = self.sanitize(l)
        if not self.is_my_log(l):
            pass
        else:
            out = self.parse(l)
            if out is not None:
                self.write("%s\n" % (out,))
        self.flush()

    def parse(self,l):
        return l

    def write(self,s):
        sys.stdout.write(s)

    def flush(self):
        sys.stdout.flush()

class ProxyLog(Log):
    def is_my_log(self,l):
        return "Proxy Gadget" in l

class USBMonLog(Log):
    STYLE = {
        "Ci":Color.red,
        "Co":Color.purple,
        "Ii":Color.blue,
        "Io":Color.cyan,
        "Bi":Color.green,
        "Bo":Color.yellow,
        "Zi":47,
        "Zo":172,
    }

    def parse(self,l):
        for t,c in USBMonLog.STYLE.iteritems():
            if t in l:
                return colorize(l,c)
        else:
            return None


class USBMITMLog(Log):
    PATTERN = "\[\s*(?P<lvl>\d+)\]\s+(?P<module>[^\s]+)\s*(?P<log>.*$)"
    MASTER_STYLE = {
        ("GADGET",DBG) : Color.green,
        ("DRIVER",DBG)   : Color.blue,
        ("GADGET",INFO) : (Color.green,Color.bold),
        ("DRIVER",INFO)   : (Color.blue,Color.bold),
    }
    SECOND_STYLE = {
        ASSERT : (Color.red,Color.bold),
        SPEC   : (214,Color.bold),
        ERR  : (Color.red,Color.bold),
        WRN  : Color.red,
        INFO : Color.yellow,
        "musb" : Color.purple
    }

    def __init__(self):
        self.pattern = re.compile(USBMITMLog.PATTERN)

    def is_my_log(self,l):
        return (len(l) > 53 and l[self.START_LOG] == "[" and l[self.START_LOG+4] == "]") or "musb" in l or "B64" in l

    def parse(self,l):
        l = l[self.START_LOG:]
        m = self.pattern.match(l)
        if not m:
            if "musb" in l or "B64" in l:
                lvl = "musb"
                module = "MUSB"
                log = l
            else:
                return None
        else:
            lvl = int(m.group("lvl"))
            module = m.group("module")
            log = m.group("log")
        return self.colorize(lvl,module,log)

    def colorize(self,lvl,module,log):
        if (module,lvl) in USBMITMLog.MASTER_STYLE:
            return colorize(log,USBMITMLog.MASTER_STYLE[(module,lvl)])
        elif lvl in USBMITMLog.SECOND_STYLE:
            return colorize("%s %s" % (module,log), USBMITMLog.SECOND_STYLE[lvl])
        else:
            return "[%s] %s %s" % (str(lvl).rjust(3," "),module,log)

class IGEPMITMLog(USBMITMLog):
    START_LOG=48

class BEAGLEMITMLog(USBMITMLog):
    START_LOG=50

class NetconsoleLog(USBMITMLog):
    START_LOG=15

def parse_args():
    """ Parse command line arguments """
    try:
        import argparse
    except:
        print "python-argparse is needed"
        sys.exit(0)

    parser = argparse.ArgumentParser(description="Description of python program")
    parser.add_argument("data",metavar="INTEGERS",nargs="*")
    parser.add_argument("--param","-p",metavar="PARAM",type=int,default=1,help="Example of parameter argument")
    parser.add_argument("--proxy",action="store_true",help="Log for proxy")
    parser.add_argument("--usbmon",action="store_true",help="Log for usbmon")
    parser.add_argument("--igep",action="store_true",help="Log for igep")
    parser.add_argument("--netconsole",action="store_true",help="Colorize netconsole")
    return parser.parse_args()

def main():
    """ Entry Point Program """
    args = parse_args()

    if args.proxy:
        log = ProxyLog
    elif args.usbmon:
        log = USBMonLog
    elif args.igep:
        log = IGEPMITMLog
    elif args.netconsole:
        log = NetconsoleLog
    else:
        log = BEAGLEMITMLog

    while 1:
        line = sys.stdin.readline()
        if not line:
            time.sleep(1)
        else:
            log().output(line)

    return 0


if __name__ == "__main__":
   sys.exit(main())
