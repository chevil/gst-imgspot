#!/usr/bin/python

import sys, os, time, threading
import pygtk, gtk, gobject
import pygst
pygst.require("0.10")
import gst

from urlparse import urlparse, parse_qs
from BaseHTTPServer import BaseHTTPRequestHandler, HTTPServer

HOST, PORT = "localhost", 9999

class jsCommandsHandler(BaseHTTPRequestHandler):

    def do_GET(self):
        self.send_response(200, 'OK')
        self.send_header('Content-type', 'html')
        self.end_headers()
        ppath = urlparse(self.path)
        params = parse_qs(ppath[4])
        # print params
        w.player.set_state(gst.STATE_NULL)
        w.button.set_label("Start")
        spotter = w.player.get_by_name( "imgspot0" )
        imgdir=params["imgdir"][0]
        spotter.set_property("imgdir", imgdir );
        w.imgentry.set_text( imgdir )
        w.player.set_state(gst.STATE_PLAYING)
        w.button.set_label("Stop")

class HTTPServerThread(threading.Thread):
    def run(self) :
        httpd.serve_forever()

gtk.gdk.threads_init()

class GTK_Main:

	def __init__(self):
		window = gtk.Window(gtk.WINDOW_TOPLEVEL)
		window.set_title("v4l2feed.py")
		window.set_default_size(500, 400)
		window.connect("destroy", gtk.main_quit, "WM destroy")
		vbox = gtk.VBox()
		window.add(vbox)
		self.movie_window = gtk.DrawingArea()
		vbox.add(self.movie_window)
		hbox = gtk.HBox()
		vbox.pack_start(hbox, False)
		hbox.set_border_width(10)
		hbox.pack_start(gtk.Label())
		self.button = gtk.Button("Stop")
		self.button.connect("clicked", self.start_stop)
		hbox.pack_start(self.button, False)
		self.button2 = gtk.Button("Quit")
		self.button2.connect("clicked", self.exit)
		hbox.pack_start(self.button2, False)
		hbox.add(gtk.Label())
                imglabel = gtk.Label("Image directory")
                hbox.pack_start(imglabel)
                self.imgentry = gtk.Entry()
                self.imgentry.set_text( imgdir )
                hbox.pack_start(self.imgentry)
                self.imgentry.connect('activate', self.new_image_directory)
		window.show_all()

		# Set up the gstreamer pipeline
		self.player = gst.parse_launch ("v4l2src device="+videodev+"  ! ffmpegcolorspace ! imgspot width=320 height=240 algorithm="+algorithm+" imgdir="+imgdir+" minscore="+minscore+" output=bus ! ffmpegcolorspace ! timeoverlay ! autovideosink")

		bus = self.player.get_bus()
		bus.add_signal_watch()
		bus.enable_sync_message_emission()
		bus.connect("message", self.on_message)
		bus.connect("sync-message::element", self.on_sync_message)

		self.player.set_state(gst.STATE_PLAYING)
                self.starttime = time.time()

	def start_stop(self, w):
		if self.button.get_label() == "Start":
			self.button.set_label("Stop")
			self.player.set_state(gst.STATE_PLAYING)
                        self.starttime = time.time()
		else:
			self.player.set_state(gst.STATE_NULL)
                        spotter = self.player.get_by_name( "imgspot0" )
                        spotter.set_property("reset", 1 );
			self.button.set_label("Start")

        def new_image_directory(self, entry):
                self.player.set_state(gst.STATE_NULL)
                imgdir =  entry.get_text()
                spotter = self.player.get_by_name( "imgspot0" )
                spotter.set_property("imgdir", imgdir );
                self.player.set_state(gst.STATE_PLAYING)
		self.button.set_label("Stop")
                self.starttime = time.time()

	def exit(self, widget, data=None):
		gtk.main_quit()

	def on_message(self, bus, message):
		t = message.type
		if t == gst.MESSAGE_EOS:
			self.player.set_state(gst.STATE_NULL)
			self.button.set_label("Start")
                elif t == gst.MESSAGE_ELEMENT:
                   st = message.structure
                   if st.get_name() == "imgspot":
                      self.curtime = time.time()
                      gm = time.gmtime(self.curtime-self.starttime)
                      stime = "%.2d:%.2d:%.2d" % ( gm.tm_hour, gm.tm_min, gm.tm_sec )
                      print "imgspot : %s : image spotted : %s at %s (score=%d)" % (st["algorithm"], st["name"], stime, st["score"])
		elif t == gst.MESSAGE_ERROR:
			err, debug = message.parse_error()
			print "Error: %s" % err, debug
			self.player.set_state(gst.STATE_NULL)
			self.button.set_label("Start")

	def on_sync_message(self, bus, message):
		if message.structure is None:
			return
		message_name = message.structure.get_name()
		if message_name == "prepare-xwindow-id":
			# Assign the viewport
                        gtk.gdk.threads_enter()
                        gtk.gdk.display_get_default().sync()
			imagesink = message.src
			imagesink.set_property("force-aspect-ratio", True)
         		imagesink.set_xwindow_id(self.movie_window.window.xid)
                        gtk.gdk.threads_leave()

def main(args):

    global videodev
    global imgdir
    global algorithm
    global minscore
    global httpd
    global httpdt
    global w

    def usage():
        sys.stderr.write("usage: %s videodevice imgdir [algorithm=surf] [minscore=30]\n" % args[0])
        sys.exit(1)

    if len(args) < 3:
        usage()

    videodev = args[1]
    imgdir = args[2]

    if len(args) >= 4 :
       algorithm=args[3]
    else :
       algorithm = 'surf'

    if len(args) == 5 :
       minscore=args[4]
    else :
       minscore = "30"

    # build the window
    w = GTK_Main()

    # start http command handler
    httpd = HTTPServer((HOST, PORT), jsCommandsHandler)

    httpdt = HTTPServerThread()
    httpdt.setDaemon(True)
    httpdt.start()

    # enter main loop
    gtk.main()

if __name__ == '__main__':
    sys.exit(main(sys.argv))

