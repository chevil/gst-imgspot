#!/usr/bin/python

# this script is a four channel video mixer 
# author : ydegoyon@gmail.com

# the script takes commands from http ( by default on port 9999 )
# at first it starts with a black screen of 320x240 ( no emission )
# then you can manipulate the video composition 
# with the following commands:
# 
# adding a video source :
# POST /inputs/add params : {channel: n, url: 'file:///path/video.avi'}  
# POST /inputs/add params : {channel: n, url: 'device:///dev/video0'}  
# POST /inputs/add params : {channel: n, url: 'http:///server.com:8000/videostream.mpg'}  

# removing a video source :
# POST /inputs/remove params : {channel: n}  

# positioning a channel
# POST /inputs/move params : {channel:n, posx: nnn, posy: nnn}  

# resizing a channel
# POST /inputs/resize params : {channel:n, width: nnn, height: nnn}  

# recording the output
# POST /outputs/record params : {file: '/path/recording.mp4'}  
# note : as the pipe is restarted when you change some parameters,
# we actually have to record in a file name with the date
# not to crush previous recordings
# the real name of the recording will be :
# /tmp/output-mm-dd-hh:mm:ss.mp4 for example

# starting and stopping the mixer
# POST /outputs/state params : {state: start|stop}  
# IMPORTANT NOTE : when you modify parameters of the mixer,
# all we do is storing them until you restart the pipe,
# so all your modifications will be visible
# _only_ when you send a start message to the mixer.
# this is due to some limitations in which parameters
# can be modified dynamically with gstreamer.

import sys, os, time, threading, thread
import pygtk, gtk, gobject
import pygst
import cgi
pygst.require("0.10")
import gst
import datetime

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
            if newsource[:9] == "device://":
                for i in range(0, 4):
                  if newsource == mixer.uri[i]:
                    #cannot add a camera two times, would crash
                    self.send_response(400, 'Bad request')
                    self.send_header('Content-type', 'html')
                    self.end_headers()
                    return
            if ( channel < 0 or channel > 3 ):
               print "adding %s on channel %d" % ( newsource, channel )
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return
            mixer.uri[channel]=newsource
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
	    mixer.player.set_state(gst.STATE_PAUSED)

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
            mixer.uri[channel]=""
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
	    mixer.player.set_state(gst.STATE_PAUSED)

        elif self.path == "/inputs/move":
          if "channel" not in params or "posx" not in params or "posy" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            channel = int( params['channel'][0] )
            posx = int( params['posx'][0] )
            posy = int( params['posy'][0] )
            if posx < 0 or posy < 0 or channel < 0 or channel > 3:
              self.send_response(400, 'Bad request')
              self.send_header('Content-type', 'html')
              self.end_headers()
              return
            mixer.xpos[channel]=posx
            mixer.ypos[channel]=posy
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
	    mixer.player.set_state(gst.STATE_PAUSED)

        elif self.path == "/inputs/resize":
          if "channel" not in params or "width" not in params or "height" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            channel = int( params['channel'][0] )
            width = int( params['width'][0] )
            height = int( params['height'][0] )
            if width < 0 or height < 0 or channel < 0 or channel > 3:
              self.send_response(400, 'Bad request')
              self.send_header('Content-type', 'html')
              self.end_headers()
              return
            mixer.width[channel]=width
            mixer.height[channel]=height
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
	    mixer.player.set_state(gst.STATE_PAUSED)

        elif self.path == "/outputs/record":
          if "file" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          mixer.recfile=params['file'][0]
          self.send_response(200, 'OK')
          self.send_header('Content-type', 'html')
          self.end_headers()
	  mixer.player.set_state(gst.STATE_PAUSED)

        elif self.path == "/outputs/state":
          if "state" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          state=params['state'][0]
          if state != "start" and state != "stop":
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
          self.send_response(200, 'OK')
          self.send_header('Content-type', 'html')
          self.end_headers()
          if state == "start":
             mixer.launch_pipe()
          if state == "stop":
	     mixer.player.set_state(gst.STATE_NULL)

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

                self.recfile=""
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
                   self.xpos.append( 0 );
                   self.ypos.append( 0 );
                   self.width.append( 320 );
                   self.height.append( 240 );

                self.txsize=0
                self.tysize=0
                for i in range(0, 4):
                   if ( self.xpos[i]+self.width[i] ) > self.txsize :
                      self.txsize=self.xpos[i]+self.width[i]
                   if ( self.ypos[i]+self.height[i] ) > self.tysize :
                      self.tysize=self.ypos[i]+self.height[i]

                self.window = gtk.Window(gtk.WINDOW_TOPLEVEL)
                self.window.set_title("videomixer.py")
                self.window.connect("destroy", gtk.main_quit, "WM destroy")
                self.window.set_default_size(self.txsize, self.tysize)
                vbox = gtk.VBox()
                self.window.add(vbox)
                self.movie_window = gtk.DrawingArea()
                vbox.add(self.movie_window)
                self.window.show_all()

                self.player = None
                self.launch_pipe()

		bus = self.player.get_bus()
		bus.add_signal_watch()
		bus.enable_sync_message_emission()
		bus.connect("message", self.on_message)
                bus.connect("sync-message::element", self.on_sync_message)

                self.starttime = time.time()

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

	def position_channels(self):
                mixer = self.player.get_by_name( "mix" )

                self.txsize=0
                self.tysize=0
                for i in range(0, 4):
                   if ( self.xpos[i]+self.width[i] ) > self.txsize :
                      self.txsize=self.xpos[i]+self.width[i]
                   if ( self.ypos[i]+self.height[i] ) > self.tysize :
                      self.tysize=self.ypos[i]+self.height[i]

                pads=list(mixer.sink_pads())
                for j in range(0, len(pads)):
                   for i in range(0, 4):
                      if pads[j].props.zorder == i+1:
                         pads[j].props.xpos=self.xpos[i]
                         pads[j].props.ypos=self.ypos[i]
                         # print "setting pad %d to %d %d" % ( j, self.xpos[i], self.ypos[i] )
                   if pads[j].props.zorder == 0: #background pad
                         videocap = gst.Caps("video/x-raw-yuv,width=%d,height=%d" % ( self.txsize, self.tysize ))
                         print "setting caps to : video/x-raw-yuv,width=%d,height=%d" % ( self.txsize, self.tysize )
                         pads[j].set_caps(videocap)
                         videotestsrc = self.player.get_by_name( "background" )
                         bpads=list(videotestsrc.pads())
                         for k in range(0, len(bpads)):
                             bpads[k].set_caps(videocap)

                # all the dynamic stuff deosn't seem to work really
                # relaunching the pipe for now
                self.launch_pipe()

	def launch_pipe(self):
                # calculating global size
                self.txsize=0
                self.tysize=0
                for i in range(0, 4):
                   if ( self.xpos[i]+self.width[i] ) > self.txsize :
                      self.txsize=self.xpos[i]+self.width[i]
                   if ( self.ypos[i]+self.height[i] ) > self.tysize :
                      self.tysize=self.ypos[i]+self.height[i]

                pipecmd = ""

                if ( self.recfile != "" ):
                    pipecmd += "autoaudiosrc ! adder name=audiomix ! tee name=audmixout ! queue ! pulsesink audmixout. !  queue ! ffenc_aac ! queue ! mux. "
                else:
                    pipecmd += "autoaudiosrc ! adder name=audiomix ! queue ! autoaudiosink "

                pipecmd += "videomixer name=mix \
                              sink_0::xpos=0 sink_0::ypos=0 sink_0::zorder=0 "

                for i in range(0, 4):
                    if self.uri[i] != "":
                       pipecmd += "sink_%d::xpos=%d sink_%d::ypos=%d sink_%d::alpha=1 sink_%d::zorder=%d " % ( i+1, self.xpos[i], i+1, self.ypos[i], i+1, i+1, i+1 )
                    else:
                       pipecmd += "sink_%d::xpos=%d sink_%d::ypos=%d sink_%d::alpha=0 sink_%d::zorder=%d " % ( i+1, self.xpos[i], i+1, self.ypos[i], i+1, i+1, i+1 )

                if ( self.recfile != "" ):
                    pipecmd += "! tee name=vidmixout ! queue ! xvimagesink sync=false vidmixout. !  queue ! ffenc_mpeg4 ! queue ! mux. "
                else:
                    pipecmd += "! xvimagesink "

                pipecmd += "videotestsrc pattern=black name=background \
                           ! video/x-raw-yuv,width=%d,height=%d \
                           ! mix.sink_0 " % ( self.txsize, self.tysize );

                for i in range(0, 4):
                    if ( self.uri[i] != "" ):
                       if self.uri[i][:7] == "file://":
                         pipecmd += " filesrc location=\"%s\" ! decodebin name=decodebin%d \
                                      decodebin%d. ! queue ! ffmpegcolorspace !  videoscale ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_%d \
                                      decodebin%d. ! audiomix." % ( self.uri[i][7:], i+1, i+1, self.width[i], self.height[i], i+1, i+1 );
                       if self.uri[i][:9] == "device://":
                         pipecmd += " v4l2src device=%s ! ffmpegcolorspace ! \
                                      videoscale ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_%d " % ( self.uri[i][9:], self.width[i], self.height[i], i+1 );
                       if self.uri[i][:7] == "http://":
                         pipecmd += " uridecodebin uri=\"%s\" name=decodebin%d \
                                      decodebin%d. ! queue ! ffmpegcolorspace ! videoscale ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_%d \
                                      decodebin%d. ! audiomix." % ( self.uri[i], i+1, i+1, self.width[i], self.height[i], i+1, i+1 );
                    #else:
                       # pipecmd += " videotestsrc pattern=%s ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_%d " % ( self.pattern[i], self.width[i], self.height[i], i+1 );

                if ( self.recfile != "" ):
                    #the real name uses a date in it
                    #not to override previous recordings
                    extension = self.recfile[-3:]
                    basename = self.recfile[:-4]
                    now = datetime.datetime.now()
                    pipecmd += "\
                                avimux name=mux ! filesink location=%s-%.2d-%.2d-%.2d:%.2d:%.2d.%s " % ( basename, now.month, now.day, now.hour, now.minute, now.second, extension );

                print pipecmd

		if self.player != None : 
                   self.player.set_state(gst.STATE_NULL)
		self.player = gst.parse_launch ( pipecmd )

                self.window.resize(self.txsize, self.tysize)

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

