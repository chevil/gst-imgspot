#!/usr/bin/python

# this script is a nth channel video mixer 
# you set the number of channel by passing it as argument
# you also pass as arguments the width and height of the mix
# usage : python videomixer.py 3 640 480
# author : ydegoyon@gmail.com

# the script takes commands from http ( by default on port 9999 )
# at first it starts with a black screen of WxH ( no emission )
# then you can manipulate the video composition 
# with the following commands:
# 
# adding a video source :
# POST /inputs/add params : {channel: n, url: 'file:///path/video.avi'}  
# POST /inputs/add params : {channel: n, url: 'device:///dev/video0'}  
# POST /inputs/add params : {channel: n, url: 'http:///server.com:8000/videostream.mpg'}  
# POST /inputs/add params : {channel: n, url: 'rtsp:///wowza.com:1935/app/stream.sdp'}  
# after adding a channel, you have to restart the mixer with "/outputs/state start"

# removing a video source :
# POST /inputs/remove params : {channel: n}  
# after adding a channel, you have to restart the mixer with "/outputs/state start"

# hiding a video source :
# POST /inputs/hide params : {channel: n}  
# you don't need to restart the mixer

# showing a (hidden) video source :
# POST /inputs/show params : {channel: n}  
# you don't need to restart the mixer

# positioning a channel
# POST /inputs/move params : {channel:n, posx: nnn, posy: nnn}  
# you don't need to restart the mixer

# resizing a channel
# POST /inputs/resize params : {channel:n, width: nnn, height: nnn}  
# you don't need to restart the mixer

# setting playing position ( global position for video files )
# POST /seek params : {seconds:nn}  
# you don't need to restart the mixer

# recording the output
# POST /outputs/record params : {file: '/path/recording.mp4'}  
# note : as the pipe is restarted when you change some parameters,
# we actually have to record in a file name with the date
# not to crush previous recordings
# the real name of the recording will be :
# /tmp/output-mm-dd-hh:mm:ss.mp4 for example
# after setting the record file, you have to restart the mixer with "/outputs/state start"

# recording the output
# POST /outputs/stream params : {hostname: 'xxx.xxx.xxx.xxx', audioport: nnnn, videoport: nnnn}  
# after setting the streaming, you have to restart the mixer with "/outputs/state start"
# note : streaming and recording are exclusive
# because if you stream you can record the stream
# on another machine or on the server

# starting and stopping the mixer
# POST /outputs/state params : {state: start|stop}  
# note : when you add or remove a channel from 
# the composition or activate recording, streaming or slides detection,
# you need to send a new start message to the mixer
# so to have a smooth experience,
# so prepare all your channels before,
# even the different videos

# detecting images/slides on a channel
# POST /inputs/detect params : {channel:n, imagedir: /path/slides, minscore: score}  
# after setting detection, you have to restart the mixer with "/outputs/state start"

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
	    mixer.player.set_state(gst.STATE_PAUSED)

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
	    mixer.player.set_state(gst.STATE_PAUSED)

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

        elif self.path == "/seek":
          if "seconds" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            seconds = int( params['seconds'][0] )
            if seconds < 0:
              self.send_response(400, 'Bad request')
              self.send_header('Content-type', 'html')
              self.end_headers()
              return

            # request is ok
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()

            event = gst.event_new_seek(1.0, gst.FORMAT_TIME,
                    gst.SEEK_FLAG_FLUSH | gst.SEEK_FLAG_ACCURATE,
                    gst.SEEK_TYPE_SET, seconds,
                    gst.SEEK_TYPE_NONE, 0)

            res = mixer.player.send_event(event)
            if res:
               print "setting new stream time to 0"
               mixer.player.set_new_stream_time(0L)
            else:
               print "seek to %r failed" % location

        elif self.path == "/inputs/load":
          if "channel" not in params or "url" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            channel = int( params['channel'][0] )
            url = params['url'][0]
            if channel < 0 or channel >= nbchannels or mixer.uri[channel][:7] != "file://":
              self.send_response(400, 'Bad request')
              self.send_header('Content-type', 'html')
              self.end_headers()
              return
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
            mixer.uri[channel]=url
            mixer.launch_pipe()

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
            #mixer.launch_pipe()
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
          #mixer.launch_pipe()
	  mixer.player.set_state(gst.STATE_PAUSED)

        elif self.path == "/outputs/stream":
          if "hostname" not in params or "audioport" not in params or "videoport" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          mixer.hostname=params['hostname'][0]
          mixer.audioport=int(params['audioport'][0])
          mixer.videoport=int(params['videoport'][0])
          self.send_response(200, 'OK')
          self.send_header('Content-type', 'html')
          self.end_headers()
          #mixer.launch_pipe()
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
                self.hostname=""
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

                self.txsize=gwidth
                self.tysize=gheight

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
                  # loop the stream
                  event = gst.event_new_seek(1.0, gst.FORMAT_TIME,
                    gst.SEEK_FLAG_FLUSH | gst.SEEK_FLAG_ACCURATE,
                    gst.SEEK_TYPE_SET, 0,
                    gst.SEEK_TYPE_NONE, 0)

                  res = mixer.player.send_event(event)
                  if res:
                    print "setting new stream time to 0"
                    mixer.player.set_new_stream_time(0L)
                  else:
                    print "seek to %r failed" % location

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

		self.player.set_state(gst.STATE_PAUSED)
                mixer = self.player.get_by_name( "mix" )

                pads=list(mixer.sink_pads())
                for j in range(0, len(pads)):
                   for i in range(0, nbchannels):
                      if self.uri[i] !="":
                        if pads[j].props.zorder == i+1:
                           videocap = gst.Caps("video/x-raw-yuv,width=%d,height=%d" % ( self.width[i], self.height[i] ))
                           vscale = self.player.get_by_name( "videoscale%d" % ( i+1 ) )
                           vpads=list(vscale.src_pads())
                           vpads[0].unlink( pads[j] )
                           vpads[0].set_caps(videocap)
                           vpads[0].link( pads[j] );

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

                pipecmd = ""

                if ( self.hostname != "" ):
                    pipecmd += "gstrtpbin name=rtpbin autoaudiosrc ! adder name=audiomix ! tee name=audmixout ! queue ! autoaudiosink audmixout. ! queue ! ffenc_aac ! rtpmp4apay ! rtpbin.send_rtp_sink_1 "
                elif ( self.recfile != "" ):
                    pipecmd += "autoaudiosrc ! adder name=audiomix ! tee name=audmixout ! queue ! autoaudiosink audmixout. ! queue ! ffenc_aac ! queue ! mux. "
                else:
                    pipecmd += "autoaudiosrc ! adder name=audiomix ! queue ! autoaudiosink "

                pipecmd += "videomixer name=mix sink_0::xpos=0 sink_0::ypos=0 sink_0::zorder=0 "

                for i in range(0, nbchannels):
                    if self.uri[i] != "":
                       pipecmd += "sink_%d::xpos=%d sink_%d::ypos=%d sink_%d::alpha=1 sink_%d::zorder=%d " % ( i+1, self.xpos[i], i+1, self.ypos[i], i+1, i+1, i+1 )
                    else:
                       pipecmd += "sink_%d::xpos=%d sink_%d::ypos=%d sink_%d::alpha=0 sink_%d::zorder=%d " % ( i+1, self.xpos[i], i+1, self.ypos[i], i+1, i+1, i+1 )

                if ( self.hostname != "" ):
                    pipecmd += "! tee name=vidmixout ! queue leaky=1 ! xvimagesink sync=false vidmixout. ! queue leaky=1 ! x264enc ! queue ! rtph264pay ! rtpbin.send_rtp_sink_0 "
                elif ( self.recfile != "" ):
                    pipecmd += "! tee name=vidmixout ! queue leaky=1 ! xvimagesink sync=false vidmixout. ! queue leaky=1 ! ffenc_mpeg4 ! queue ! mux. "
                else:
                    pipecmd += "! autovideosink "

                pipecmd += "videotestsrc pattern=black name=background ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_0 " % ( self.txsize, self.tysize );

                for i in range(0, nbchannels):
                    if ( self.uri[i] != "" ):

                       if ( self.imagedir[i] != "" ):
                         detectstring = "imgspot width=%d height=%d algorithm=surf imgdir=%s minscore=%d output=bus !" % ( self.width[i], self.height[i], self.imagedir[i], self.minscore[i] )
                       else:
                         detectstring = ""

                       if self.uri[i][:7] == "file://":
                         pipecmd += " filesrc name=filesrc%d location=\"%s\" ! decodebin2 name=decodebin%d decodebin%d. ! queue ! ffmpegcolorspace !  %s ffmpegcolorspace ! video/x-raw-yuv,width=%d,height=%d ! videoscale name=videoscale%d ! mix.sink_%d decodebin%d. ! audiomix." % ( i+1, self.uri[i][7:], i+1, i+1, detectstring, self.width[i], self.height[i], i+1, i+1, i+1 );

                       if self.uri[i][:9] == "device://":
                         pipecmd += " v4l2src name=v4l2src%d device=%s ! queue ! ffmpegcolorspace ! %s ffmpegcolorspace !  video/x-raw-yuv,width=%d,height=%d ! videoscale name=videoscale%d ! mix.sink_%d " % ( i+1, self.uri[i][9:], detectstring, self.width[i], self.height[i], i+1, i+1 );

                       if self.uri[i][:7] == "http://":
                         pipecmd += " uridecodebin name=decodebin%d uri=\"%s\" decodebin%d. ! queue ! ffmpegcolorspace ! %s ffmpegcolorspace ! video/x-raw-yuv,width=%d,height=%d ! videoscale name=videoscale%d ! mix.sink_%d decodebin%d. ! audiomix." % ( i+1, self.uri[i], i+1, detectstring, self.width[i], self.height[i], i+1, i+1, i+1 );

                       if self.uri[i][:7] == "rtsp://":
                         pipecmd += " rtspsrc location=\"%s\" ! decodebin name=decodebin%d decodebin%d. ! queue ! ffmpegcolorspace ! %s ffmpegcolorspace ! video/x-raw-yuv,width=%d,height=%d ! videoscale name=videoscale%d ! mix.sink_%d decodebin%d. ! audiomix." % ( self.uri[i], i+1, i+1, detectstring, self.width[i], self.height[i], i+1, i+1, i+1 );

                    #else:
                       # pipecmd += " videotestsrc pattern=%s ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_%d. " % ( self.pattern[i], self.width[i], self.height[i], i+1 );

                if ( self.hostname != "" ):
                    pipecmd += " rtpbin.send_rtp_src_0 ! udpsink host=%s port=%d ts-offset=0 name=vrtpsink rtpbin.send_rtcp_src_0 ! udpsink host=%s port=%d sync=false async=false name=vrtcpsink udpsrc port=%d name=vrtpsrc ! rtpbin.recv_rtcp_sink_0 " % ( self.hostname, self.videoport, self.hostname, self.videoport+1, self.videoport+5 )
                    pipecmd += " rtpbin.send_rtp_src_1 ! udpsink host=%s port=%d ts-offset=0 name=artpsink rtpbin.send_rtcp_src_1 ! udpsink host=%s port=%d sync=false async=false name=artcpsink udpsrc port=%d name=artpsrc ! rtpbin.recv_rtcp_sink_1 " % ( self.hostname, self.audioport, self.hostname, self.audioport+1, self.audioport+5 )
                elif ( self.recfile != "" ):
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
    global gwidth
    global gheight

    def usage():
        sys.stderr.write("usage: %s <nbchannels> <width> <height>\n" % args[0])
        sys.exit(1)

    if len(args) < 4:
        usage()

    nbchannels = int(args[1])
    gwidth = int(args[2])
    gheight = int(args[3])

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

