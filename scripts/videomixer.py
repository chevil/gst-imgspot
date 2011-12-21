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

# hiding a video source :
# POST /inputs/hide params : {channel: n}  

# showing a (hidden) video source :
# POST /inputs/show params : {channel: n}  

# loading a new source in a channel :
# POST /inputs/load params : {channel: n, url: 'file:///path/video.avi'}  
# POST /inputs/load params : {channel: n, url: 'device:///dev/video0'}  
# POST /inputs/load params : {channel: n, url: 'http:///server.com:8000/videostream.mpg'}  }  
# the new source must be of the same type as the previous one

# seeking to a position in a video :
# POST /inputs/seek params : {channel: n, percent: nn}  

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

# more optional features : detecting images on a channel
# POST /inputs/detect params : {channel:n, imagedir: /path/slides, minscore: score}  

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
                for i in range(0, nbchannels):
                  if newsource == mixer.uri[i]:
                    #cannot add a camera two times, would crash
                    self.send_response(400, 'Bad request')
                    self.send_header('Content-type', 'html')
                    self.end_headers()
                    return
            if ( channel < 0 or channel >= nbchannels ):
               print "adding %s on channel %d" % ( newsource, channel )
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return
            mixer.uri[channel]=newsource
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
            #mixer.launch_pipe()

        elif self.path == "/inputs/delete":
          if "channel" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            channel = int( params['channel'][0] )
            if ( channel < 0 or channel >= nbchannels ):
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return
            mixer.uri[channel]=""
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
            #mixer.launch_pipe()

        elif self.path == "/inputs/hide":
          if "channel" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            channel = int( params['channel'][0] )
            if ( channel < 0 or channel >= nbchannels ):
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return
            pads=list(mixer.player.get_by_name("mix").sink_pads())
            for j in range(0, len(pads)):
                if pads[j].props.zorder == channel+1:
                   pads[j].props.alpha=0.0
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()

        elif self.path == "/inputs/show":
          if "channel" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            channel = int( params['channel'][0] )
            if ( channel < 0 or channel >= nbchannels ):
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return
            pads=list(mixer.player.get_by_name("mix").sink_pads())
            for j in range(0, len(pads)):
                if pads[j].props.zorder == channel+1:
                   pads[j].props.alpha=1.0
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()

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
            if posx < 0 or posy < 0 or channel < 0 or channel >= nbchannels:
              self.send_response(400, 'Bad request')
              self.send_header('Content-type', 'html')
              self.end_headers()
              return
            mixer.xpos[channel]=posx
            mixer.ypos[channel]=posy
            pads=list(mixer.player.get_by_name("mix").sink_pads())
            for j in range(0, len(pads)):
                if pads[j].props.zorder == channel+1:
                   pads[j].props.xpos=posx
                   pads[j].props.ypos=posy
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()

        elif self.path == "/inputs/load":
          if "url" not in params or "channel" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            newsource = params['url'][0]
            channel = int( params['channel'][0] )

            # wrong channel number
            if ( channel < 0 or channel >= nbchannels ):
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return
            
            # cannot load a camera that is already in use
            if newsource[:9] == "device://":
                for i in range(0, nbchannels):
                  if newsource == mixer.uri[i]:
                    #cannot add a camera two times, would crash
                    self.send_response(400, 'Bad request')
                    self.send_header('Content-type', 'html')
                    self.end_headers()
                    return

            # check source type
            if mixer.uri[channel][:7] != newsource[:7]  or ( mixer.uri[channel][:9] == "device://" and newsource[:9] != "device://" ) :
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return

            if newsource[:7] == "file://":
               wsource = "kfilesrc%d" % ( channel+1 ) 
               kfilesrc = mixer.player.get_by_name(wsource)
               kfilesrc.set_property( "location", newsource[7:] );

            if newsource[:9] == "device://":
               wsource = "v4l2src%d" % ( channel+1 ) 
               v4l2src = mixer.player.get_by_name(wsource)
	       mixer.player.set_state(gst.STATE_PAUSED)
               v4l2src.set_property( "device", newsource[9:] )
	       mixer.player.set_state(gst.STATE_PLAYING)

            mixer.uri[channel]=newsource
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()

        elif self.path == "/seek":
          if "seconds" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            percent = int( params['percent'][0] )
	    mixer.player.set_state(gst.STATE_PAUSED)
	    mixer.player.seek_simple(gst.FORMAT_PERCENT, gst.SEEK_FLAG_ACCURATE, percent)
	    mixer.player.set_state(gst.STATE_PLAYING)
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()

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
            if width < 0 or height < 0 or channel < 0 or channel >= nbchannels:
              self.send_response(400, 'Bad request')
              self.send_header('Content-type', 'html')
              self.end_headers()
              return
            mixer.width[channel]=width
            mixer.height[channel]=height
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
            mixer.position_channels()

        elif self.path == "/inputs/detect":
          if "channel" not in params or "imagedir" not in params or "minscore" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            channel = int( params['channel'][0] )
            imagedir = params['imagedir'][0]
            minscore = int( params['minscore'][0] )
            if channel < 0 or channel >= nbchannels or minscore < 0:
              self.send_response(400, 'Bad request')
              self.send_header('Content-type', 'html')
              self.end_headers()
              return
            mixer.imagedir[channel]=imagedir
            mixer.minscore[channel]=minscore
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
            mixer.launch_pipe()

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
                self.imagedir=[]
                self.minscore=[]
                self.pattern.append( "snow" )
                self.pattern.append( "red" )
                self.pattern.append( "blue" )
                self.pattern.append( "green" )
                for i in range(0, nbchannels):
                   self.uri.append( "" );
                   self.xpos.append( 0 );
                   self.ypos.append( 0 );
                   self.width.append( 320 );
                   self.height.append( 240 );
                   self.imagedir.append( "" );
                   self.minscore.append( 0 );

                self.txsize=640
                self.tysize=480
                #for i in range(0, nbchannels):
                #   if ( self.xpos[i]+self.width[i] ) > self.txsize :
                #      self.txsize=self.xpos[i]+self.width[i]
                #   if ( self.ypos[i]+self.height[i] ) > self.tysize :
                #      self.tysize=self.ypos[i]+self.height[i]

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

		self.player.set_state(gst.STATE_PLAYING)
                mixer = self.player.get_by_name( "mix" )

                #self.txsize=0
                #self.tysize=0
                #for i in range(0, nbchannels):
                #   if ( self.xpos[i]+self.width[i] ) > self.txsize :
                #      self.txsize=self.xpos[i]+self.width[i]
                #   if ( self.ypos[i]+self.height[i] ) > self.tysize :
                #      self.tysize=self.ypos[i]+self.height[i]

                pads=list(mixer.sink_pads())
                for j in range(0, len(pads)):
                   for i in range(0, nbchannels):
                      if self.uri[i] !="":
                        if pads[j].props.zorder == i+1:
                           videocap = gst.Caps("video/x-raw-yuv,width=%d,height=%d" % ( self.width[i], self.height[i] ))
                           vcale = self.player.get_by_name( "videoscale%d" % ( i+1 ) )
                           vpads=list(vcale.src_pads())
                           vpads[0].unlink( pads[j] )
                           vpads[0].set_caps(videocap)
                           vpads[0].link( pads[j] );
                           vpads=list(vcale.sink_pads())
                           vpads[0].set_caps(videocap)

                         # print "setting pad %d to %d %d" % ( j, self.xpos[i], self.ypos[i] )
                   # resize the whole screen
                   # if pads[j].props.zorder == 0: #background pad
                   #       videocap = gst.Caps("video/x-raw-yuv,width=%d,height=%d" % ( self.txsize, self.tysize ))
                   #       print "setting caps to : video/x-raw-yuv,width=%d,height=%d" % ( self.txsize, self.tysize )
                   #       pads[j].set_caps(videocap)
                   #       videotestsrc = self.player.get_by_name( "background" )
                   #       bpads=list(videotestsrc.pads())
                   #       for k in range(0, len(bpads)):
                   #           bpads[k].set_caps(videocap)
                   #       cpads=list(mixer.src_pads())
                   #       for k in range(0, len(cpads)):
                   #           cpads[k].set_caps(videocap)

                # all the dynamic stuff deosn't seem to work really
                # relaunching the pipe for now
                #self.launch_pipe()
		self.player.set_state(gst.STATE_PLAYING)

	def launch_pipe(self):
                # calculating global size
                self.txsize=0
                self.tysize=0
                for i in range(0, nbchannels):
                   if ( self.xpos[i]+self.width[i] ) > self.txsize :
                      self.txsize=self.xpos[i]+self.width[i]
                   if ( self.ypos[i]+self.height[i] ) > self.tysize :
                      self.tysize=self.ypos[i]+self.height[i]

                pipecmd = ""

                if ( self.recfile != "" ):
                    pipecmd += "autoaudiosrc ! adder name=audiomix ! tee name=audmixout ! queue ! autoaudiosink audmixout. ! queue ! ffenc_aac ! queue ! mux. "
                else:
                    pipecmd += "autoaudiosrc ! adder name=audiomix ! queue ! autoaudiosink "

                pipecmd += "videomixer name=mix sink_0::xpos=0 sink_0::ypos=0 sink_0::zorder=0 "

                for i in range(0, nbchannels):
                    if self.uri[i] != "":
                       pipecmd += "sink_%d::xpos=%d sink_%d::ypos=%d sink_%d::alpha=1 sink_%d::zorder=%d " % ( i+1, self.xpos[i], i+1, self.ypos[i], i+1, i+1, i+1 )
                    else:
                       pipecmd += "sink_%d::xpos=%d sink_%d::ypos=%d sink_%d::alpha=0 sink_%d::zorder=%d " % ( i+1, self.xpos[i], i+1, self.ypos[i], i+1, i+1, i+1 )

                if ( self.recfile != "" ):
                    pipecmd += "! tee name=vidmixout ! queue leaky=1 ! xvimagesink sync=false vidmixout. ! queue ! x264enc ! queue ! mux. "
                else:
                    pipecmd += "! xvimagesink "

                pipecmd += "videotestsrc pattern=black name=background ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_0 " % ( self.txsize, self.tysize );

                for i in range(0, nbchannels):
                    if ( self.uri[i] != "" ):

                       if ( self.imagedir[i] != "" ):
                         detectstring = "imgspot width=%d height=%d algorithm=surf imgdir=%s minscore=%d output=bus !" % ( self.width[i], self.height[i], self.imagedir[i], self.minscore[i] )
                       else:
                         detectstring = ""

                       if self.uri[i][:7] == "file://":
                         pipecmd += " kfilesrc name=kfilesrc%d location=\"%s\" ! decodebin name=decodebin%d decodebin%d. ! queue ! ffmpegcolorspace !  %s ffmpegcolorspace ! video/x-raw-yuv,width=%d,height=%d ! videoscale name=videoscale%d ! mix.sink_%d decodebin%d. ! audiomix." % ( i+1, self.uri[i][7:], i+1, i+1, detectstring, self.width[i], self.height[i], i+1, i+1, i+1 );
                       if self.uri[i][:9] == "device://":
                         pipecmd += " v4l2src name=v4l2src%d device=%s ! queue ! ffmpegcolorspace ! %s ffmpegcolorspace !  video/x-raw-yuv,width=%d,height=%d ! videoscale name=videoscale%d ! mix.sink_%d " % ( i+1, self.uri[i][9:], detectstring, self.width[i], self.height[i], i+1, i+1 );
                       if self.uri[i][:7] == "http://":
                         pipecmd += " uridecodebin name=decodebin%d uri=\"%s\" decodebin%d. ! queue ! ffmpegcolorspace ! %s ffmpegcolorspace ! video/x-raw-yuv,width=%d,height=%d ! videoscale name=videoscale%d ! mix.sink_%d decodebin%d. ! audiomix." % ( i+1, self.uri[i], i+1, detectstring, self.width[i], self.height[i], i+1, i+1, i+1 );
                    #else:
                       # pipecmd += " videotestsrc pattern=%s ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_%d " % ( self.pattern[i], self.width[i], self.height[i], i+1 );

                if ( self.recfile != "" ):
                    #the real name uses a date in it
                    #not to override previous recordings
                    extension = self.recfile[-3:]
                    basename = self.recfile[:-4]
                    now = datetime.datetime.now()
                    pipecmd += " avimux name=mux ! filesink location=%s-%.2d-%.2d-%.2d:%.2d:%.2d.%s " % ( basename, now.month, now.day, now.hour, now.minute, now.second, extension );

                print pipecmd

		if self.player != None : 
                   self.player.set_state(gst.STATE_NULL)
		self.player = gst.parse_launch ( pipecmd )

                #self.window.resize(self.txsize, self.tysize)

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
    global nbchannels

    def usage():
        sys.stderr.write("usage: %s <nbchannels>\n" % args[0])
        sys.exit(1)

    if len(args) < 2:
        usage()

    nbchannels = int(args[1])

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

