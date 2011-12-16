#!/usr/bin/python

# this script takes commands from http ( by default on port 9999 )
# at first it starts with a black screen of 320x240 
# then you can manipulate the video composition 
# with the following commands:
# 
# adding a video source :
# POST /inputs/add params : {url: 'file:///path/video.avi', channel: 0}  
# POST /inputs/add params : {url: 'device:///dev/video0', channel: 1}  
# POST /inputs/add params : {url: 'http:///server.com:8000/videostream.mp4', channel: 2}  

# removing a video source :
# POST /inputs/remove params : {channel: 0}  

import sys, os, time, threading, thread
import pygtk, gtk, gobject
import pygst
import cgi
pygst.require("0.10")
import gst

from urlparse import urlparse, parse_qs
from BaseHTTPServer import BaseHTTPRequestHandler, HTTPServer

HOST, PORT = "localhost", 9999

class jsCommandsHandler(BaseHTTPRequestHandler):

    def do_POST(self):
        length = int(self.headers.getheader('content-length'))        
        params = parse_qs(self.rfile.read(length))
        if self.path == "/inputs/add":
          if "url" not in params or "channel" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            newsource = params['url'][0]
            channel = int( params['channel'][0] )
            if ( channel < 0 or channel > 3 ):
               print "adding %s on channel %d" % ( newsource, channel )
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return
            mixer.add_source_on_channel( newsource, channel )
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
        elif self.path == "/inputs/delete":
          if "channel" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            channel = int( params['channel'][0] )
            if ( channel < 0 or channel > 3 ):
               print "deleting channel %d" % ( channel )
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return
            mixer.delete_source_on_channel( channel )
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
        else:
          self.send_response(404, 'Not Found')
          self.send_header('Content-type', 'html')
          self.end_headers()

class HTTPServerThread(threading.Thread):
    def run(self) :
        httpd.serve_forever()

gtk.gdk.threads_init()

class Gst4chMixer:

	def __init__(self):

                window = gtk.Window(gtk.WINDOW_TOPLEVEL)
                window.set_title("videomixer.py")
                window.set_default_size(640, 480)
                window.connect("destroy", gtk.main_quit, "WM destroy")
                vbox = gtk.VBox()
                window.add(vbox)
                self.movie_window = gtk.DrawingArea()
                vbox.add(self.movie_window)
                window.show_all()

		# just launch an empty mixer with 4 entries and a black background
		self.player = gst.parse_launch ("videomixer name=mix \
                              sink_0::xpos=0 sink_0::ypos=0 sink_0::alpha=0 \
                              sink_1::xpos=0 sink_1::ypos=0 \
                              sink_2::xpos=320 sink_2::ypos=0 \
                              sink_3::xpos=0 sink_3::ypos=240 \
                              sink_4::xpos=320 sink_4::ypos=240 \
                              ! xvimagesink \
                              videotestsrc pattern=black \
                              ! video/x-raw-yuv,width=640,height=480 \
                              ! mix.sink_0 \
                              videotestsrc pattern=snow \
                              ! video/x-raw-yuv,width=320,height=240 \
                              ! mix.sink_1 \
                              videotestsrc pattern=red \
                              ! video/x-raw-yuv,width=320,height=240 \
                              ! mix.sink_2 \
                              videotestsrc pattern=blue \
                              ! video/x-raw-yuv,width=320,height=240 \
                              ! mix.sink_3 \
                              videotestsrc pattern=green \
                              ! video/x-raw-yuv,width=320,height=240 \
                              ! mix.sink_4 \
                              ");

		bus = self.player.get_bus()
		bus.add_signal_watch()
		bus.enable_sync_message_emission()
		bus.connect("message", self.on_message)
                bus.connect("sync-message::element", self.on_sync_message)

		self.player.set_state(gst.STATE_PLAYING)
                self.starttime = time.time()
                self.uri=[]
                self.xpos=[]
                self.ypos=[]
                self.width=[]
                self.height=[]
                self.pattern=[]
                self.pattern.append( "snow" )
                self.pattern.append( "red" )
                self.pattern.append( "blue" )
                self.pattern.append( "green" )
                for i in range(0, 4):
                   self.uri.append( "" );
                   self.xpos.append( 0+(i%2)*320 );
                   self.ypos.append( 0+int(i/2)*240 );
                   self.width.append( 320 );
                   self.height.append( 240 );

	def on_message(self, bus, message):
		t = message.type
		if t == gst.MESSAGE_EOS:
			self.player.set_state(gst.STATE_NULL)

                elif t == gst.MESSAGE_ELEMENT:
                   st = message.structure
                   if st.get_name() == "imgspot":
                      self.curtime = time.time()
                      gm = time.gmtime(self.curtime-self.starttime)
                      stime = "%.2d:%.2d:%.2d" % ( gm.tm_hour, gm.tm_min, gm.tm_sec )
                      print "imgspot : %s : image spotted : %s at %s (score=%d)" % (st["algorithm"], st["name"], stime, st["score"])

		elif t == gst.MESSAGE_ERROR:
			err, debug = message.parse_error()
			print "Gst4chMixer : error: %s" % err

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

	def add_source_on_channel(self, uri, channel):
                self.uri[channel]=uri
                self.launch_pipe()

	def delete_source_on_channel(self, channel):
                self.uri[channel]=""
                self.launch_pipe()

	def launch_pipe(self):
                # calculating global size
                txsize=0
                tysize=0
                for i in range(0, 4):
                   if ( self.xpos[i]+self.width[i] ) > txsize :
                      txsize=self.xpos[i]+self.width[i]
                   if ( self.ypos[i]+self.height[i] ) > tysize :
                      tysize=self.ypos[i]+self.height[i]

                pipecmd = "videomixer name=mix \
                              sink_0::xpos=0 sink_0::ypos=0 sink_0::alpha=0 \
                              sink_1::xpos=%d sink_1::ypos=%d \
                              sink_2::xpos=%d sink_2::ypos=%d \
                              sink_3::xpos=%d sink_3::ypos=%d \
                              sink_4::xpos=%d sink_4::ypos=%d \
                              ! xvimagesink \
                              videotestsrc pattern=black \
                              ! video/x-raw-yuv,width=%d,height=%d \
                              ! mix.sink_0 " % ( self.xpos[0], self.ypos[0], self.xpos[1], self.ypos[1], 
                                                self.xpos[2], self.ypos[2], self.xpos[3], self.ypos[3], txsize, tysize );

                for i in range(0, 4):
                    if ( self.uri[i] != "" ):
                       if self.uri[i][:7] == "file://":
                         pipecmd += " filesrc location=%s ! decodebin2 ! queue ! ffmpegcolorspace ! \
                                      video/x-raw-yuv,width=%d,height=%d ! mix.sink_%d " % ( self.uri[i][7:], self.width[i], self.height[i], i+1 );
                       if self.uri[i][:9] == "device://":
                         pipecmd += " v4l2src device=%s ! ffmpegcolorspace ! \
                                      video/x-raw-yuv,width=%d,height=%d ! mix.sink_%d " % ( self.uri[i][9:], self.width[i], self.height[i], i+1 );
                       if self.uri[i][:7] == "http://":
                         pipecmd += " uridecodebin uri=%s ! decodebin ! queue ! ffmpegcolorspace ! \
                                      video/x-raw-rgb,width=%d,height=%d ! mix.sink_%d " % ( self.uri[i], self.width[i], self.height[i], i+1 );
                    else:
                       pipecmd += " videotestsrc pattern=%s ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_%d " % ( self.pattern[i], self.width[i], self.height[i], i+1 );

                print pipecmd

		self.player.set_state(gst.STATE_NULL)
		self.player = gst.parse_launch ( pipecmd )

		bus = self.player.get_bus()
		bus.add_signal_watch()
		bus.enable_sync_message_emission()
		bus.connect("message", self.on_message)
                bus.connect("sync-message::element", self.on_sync_message)

		self.player.set_state(gst.STATE_PLAYING)

def main(args):

    global mixer
    global httpd
    global httpdt

    def usage():
        sys.stderr.write("usage: %s" % args[0])
        sys.exit(1)

    # build the window
    mixer = Gst4chMixer()
    # enter main loop

    # start http command handler
    httpd = HTTPServer((HOST, PORT), jsCommandsHandler)

    httpdt = HTTPServerThread()
    httpdt.setDaemon(True)
    httpdt.start()

    gtk.main()

if __name__ == '__main__':
    sys.exit(main(sys.argv))

