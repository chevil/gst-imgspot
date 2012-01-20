#!/usr/bin/python

# usage : python videomixer.py 3 640 480 [nosound] [novideo]
# this script is a nth channel video mixer 
# you set the number of channel by passing it as argument
# you also pass as arguments the width and height of the mix
# author : ydegoyon@gmail.com

# the script takes commands from http ( by default on port 9999 )
# at first it starts with a black screen of WxH ( no emission )
# then you can manipulate the video composition 
# with the following commands:
  
#  --------------------- STATIC CONFIGURATION -----------------------------------------------

# adding a video source :
# POST /inputs/add params : {channel: n, url: 'file:///path/video.avi'}
# POST /inputs/add params : {channel: n, url: 'device:///dev/video0'}  
# POST /inputs/add params : {channel: n, url: 'rtsp:///wowza.com:1935/app/stream.sdp'}  
# POST /inputs/add params : {channel: n, url: 'rtmp:///wowza.com:1935/app/stream.sdp'}  
# POST /inputs/add params : {channel: n, url: 'http:///server.com:8000/videostream.mpg'}  
# POST /inputs/add params : {channel: n, url: 'still:///path/image.png'}  
# after this operation, the mixer is restarted

# removing a video source :
# POST /inputs/remove params : {channel: n}  
# after this operation, the mixer is restarted

# recording the output
# POST /outputs/record params : {file: '/path/recording.mp4'}  
# note : as the pipe is restarted when you change the number of channels,
# we actually have to record in a file name with the date
# not to crush previous recordings
# the real name of the recording will be :
# /tmp/output-mm-dd-hh:mm:ss.mp4 for example
# if you asked for /tmp/output.mp4
# after this operation, the mixer is restarted

# streaming the output
# POST /outputs/stream params : {hostname: 'xxx.xxx.xxx.xxx', audioport: nnnn, videoport: nnnn}  
# this activate a rtp streaming towards a wowza server for example
# note : streaming and recording are exclusive
# because if you stream you can record the stream
# on another machine or on the server
# after this operation, the mixer is restarted

# detecting images/slides on a channel
# POST /inputs/detect params : {channel:n, imagedir: /path/slides, minscore: score}  
# after this operation, the mixer is restarted

#  -------------------------- PASSING IN DYNAMIC MODE --------------------------------------

# starting and stopping the mixer
# POST /outputs/state params : {state: start|stop}  
# note : when you add or remove a channel from 
# the composition or activate recording, streaming or slides detection,
# the mixer is restarted
# so to have a smooth experience,
# prepare all your channels before,
# even the different videos ans cameras 
# you want to use in a session

#  -------------------------- DYNAMIC OPERATIONS --------------------------------------------

# positioning a channel
# POST /inputs/move params : {channel:n, posx: nnn, posy: nnn}  

# resizing a channel
# POST /inputs/resize params : {channel:n, width: nnn, height: nnn}  

# hiding a channel :
# POST /inputs/hide params : {channel: n}  

# showing a (hidden) channel :
# POST /inputs/show params : {channel: n}  

# setting transparency on a channel
# POST /inputs/alpha params : {alpha: dd.dd}  

# setting z-order of a channel
# POST /inputs/zorder params : {channel:n, zorder: n}  

# setting playing position ( global position for video files )
# ( a bit buggy with some video formats )
# POST /seek params : {seconds:nn}  

#  -------------------------- THAT's ALL for NOW --------------------------------------------

import sys, os, time, threading, thread, commands, subprocess, re
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
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return

            #check if the source exists
            if newsource[:7] == "file://":
               if (not os.path.exists(newsource[7:]) ): 
                 self.send_response(400, 'Bad request')
                 self.send_header('Content-type', 'html')
                 self.end_headers()
                 return

            #determine the size of the source
            if newsource[:7] == "file://":
             mixer.uri[channel]=newsource
             self.send_response(200, 'OK')
             self.send_header('Content-type', 'html')
             self.end_headers()

             if os.path.exists('/usr/bin/ffprobe'):
               cmd = "/usr/bin/ffprobe -show_streams \"%s\" 2>&1 | grep width" % ( mixer.uri[channel][7:] )
               fwidth = commands.getoutput(cmd)
               widths = fwidth.split('=')
               #print "Video width %s" % widths[1];
               mixer.owidth[channel]=int(widths[1]) 
               cmd = "ffprobe -show_streams \"%s\" 2>&1 | grep height" % ( mixer.uri[channel][7:] )
               fheight = commands.getoutput(cmd)
               heights = fheight.split('=')
               #print "Video height %s" % heights[1];
               mixer.oheight[channel]=int(heights[1])
               cmd = "ffprobe -show_streams \"%s\" 2>&1 | grep audio" % ( mixer.uri[channel][7:] )
               audio = commands.getoutput(cmd)
               if audio == "codec_type=audio":
                  mixer.noaudio[channel]=False
               else:
                  mixer.noaudio[channel]=True
             else:
               print "warning : cannot detect video size ( is ffmpeg installed ?)"

            if newsource[:9] == "device://":
             if os.path.exists('/usr/bin/v4l-info'):
               cmd = "/usr/bin/v4l-info %s 2>/dev/null | grep fmt.pix.width | awk '{print $3}'" % ( newsource[9:] )
               width = commands.getoutput(cmd)
               cmd = "/usr/bin/v4l-info %s 2>/dev/null | grep fmt.pix.height | awk '{print $3}'" % ( newsource[9:] )
               height = commands.getoutput(cmd)
               if width != '':
                 self.send_response(200, 'OK')
                 self.send_header('Content-type', 'html')
                 self.end_headers()
                 mixer.uri[channel]=newsource
                 mixer.owidth[channel]=int(width) 
                 mixer.oheight[channel]=int(height)
               else:
                 self.send_response(400, 'Bad request')
                 self.send_header('Content-type', 'html')
                 self.end_headers()
                 return
             else:
               print "warning : cannot detect camera size ( is v4l-conf installed ?)"
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return

            if newsource[:7] == "rtsp://" or newsource[:7] == "rtmp://":
             if os.path.exists('/usr/bin/mplayer'):
               process = subprocess.Popen(['/usr/bin/mplayer','-endpos', '1', '-vo', 'null', '-ao', 'null', newsource], shell=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
               streamis=False
               pv = re.compile('VO:')
               pa = re.compile('AO:')
               mixer.noaudio[channel]=True
               for line in process.stdout:
                  if pv.match( line ):
                    sizes=line.split(' '); 
                    wihe=sizes[2].split('x'); 
                    mixer.uri[channel]=newsource
                    mixer.owidth[channel]=int(wihe[0]) 
                    mixer.oheight[channel]=int(wihe[1])
                    self.send_response(200, 'OK')
                    self.send_header('Content-type', 'html')
                    self.end_headers()
                    streamis=True
                  if pa.match( line ):
                    mixer.noaudio[channel]=False
               if not streamis:
                    self.send_response(400, 'Bad request')
                    self.send_header('Content-type', 'html')
                    self.end_headers()
                    return
             else:
               print "warning : cannot detect stream size ( is mplayer installed ?)"
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return

            if newsource[:7] == "http://":
             if os.path.exists('/usr/bin/mplayer'):
               process = subprocess.Popen(['/usr/bin/mplayer','-endpos', '1', '-vo', 'null', '-ao', 'null', newsource], shell=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
               streamis=False
               pv = re.compile('VO:')
               pa = re.compile('AO:')
               mixer.noaudio[channel]=True
               for line in process.stdout:
                  if pv.match( line ):
                    sizes=line.split(' '); 
                    wihe=sizes[2].split('x'); 
                    mixer.uri[channel]=newsource
                    mixer.owidth[channel]=int(wihe[0]) 
                    mixer.oheight[channel]=int(wihe[1])
                    self.send_response(200, 'OK')
                    self.send_header('Content-type', 'html')
                    self.end_headers()
                    streamis=True
                  if pv.match( line ):
                    mixer.noaudio[channel]=False
               if not streamis:
                    self.send_response(400, 'Bad request')
                    self.send_header('Content-type', 'html')
                    self.end_headers()
                    return
             else:
               print "warning : cannot detect remote video size ( is mplayer installed ?)"
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return

            #check if the image exists
            if newsource[:8] == "still://":
               if (not os.path.exists(newsource[8:]) ): 
                 self.send_response(400, 'Bad request')
                 self.send_header('Content-type', 'html')
                 self.end_headers()
                 return

            #determine the size of the source
            if newsource[:8] == "still://":
             mixer.uri[channel]=newsource
             self.send_response(200, 'OK')
             self.send_header('Content-type', 'html')
             self.end_headers()

             if os.path.exists('/usr/bin/ffprobe'):
               cmd = "/usr/bin/ffprobe -show_streams %s 2>&1 | grep width" % ( mixer.uri[channel][8:] )
               fwidth = commands.getoutput(cmd)
               widths = fwidth.split('=')
               #print "Video width %s" % widths[1];
               mixer.owidth[channel]=int(widths[1]) 
               cmd = "ffprobe -show_streams %s 2>&1 | grep height" % ( mixer.uri[channel][8:] )
               fheight = commands.getoutput(cmd)
               heights = fheight.split('=')
               #print "Video height %s" % heights[1];
               mixer.oheight[channel]=int(heights[1])
             else:
               print "warning : cannot detect video size ( is ffmpeg installed ?)"

            mixer.launch_pipe()
	    #mixer.player.set_state(gst.STATE_PAUSED)

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
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
            mixer.uri[channel]=""
            mixer.launch_pipe()
	    #mixer.player.set_state(gst.STATE_PAUSED)

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
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
            pads=list(mixer.player.get_by_name("mix").sink_pads())
            for j in range(0, len(pads)):
                if pads[j].props.ename == "channel%d" % ( channel+1 ):
                   pads[j].props.alpha=0.0

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
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
            pads=list(mixer.player.get_by_name("mix").sink_pads())
            for j in range(0, len(pads)):
                if pads[j].props.ename == "channel%d" % ( channel+1 ):
                   pads[j].props.alpha=1.0

        elif self.path == "/inputs/alpha":
          if "channel" not in params or "alpha" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            channel = int( params['channel'][0] )
            alpha = float( params['alpha'][0] )
            if ( channel < 0 or channel >= nbchannels or alpha<0 or alpha>1.0 ):
               self.send_response(400, 'Bad request')
               self.send_header('Content-type', 'html')
               self.end_headers()
               return
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
            pads=list(mixer.player.get_by_name("mix").sink_pads())
            for j in range(0, len(pads)):
                if pads[j].props.ename == "channel%d" % ( channel+1 ):
                   pads[j].props.alpha=alpha

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
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
            mixer.xpos[channel]=posx
            mixer.ypos[channel]=posy
            pads=list(mixer.player.get_by_name("mix").sink_pads())
            for j in range(0, len(pads)):
                if pads[j].props.ename == "channel%d" % ( channel+1 ):
                   pads[j].props.xpos=posx
                   pads[j].props.ypos=posy

        elif self.path == "/inputs/zorder":
          if "channel" not in params or "zorder" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            channel = int( params['channel'][0] )
            zorder = int( params['zorder'][0] )
            if channel < 0 or channel >= nbchannels or zorder<0:
              self.send_response(400, 'Bad request')
              self.send_header('Content-type', 'html')
              self.end_headers()
              return
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
            mixer.zorder[channel]=zorder
            pads=list(mixer.player.get_by_name("mix").sink_pads())
            for j in range(0, len(pads)):
                if pads[j].props.ename == "channel%d" % ( channel+1 ):
                   pads[j].props.zorder=zorder

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
            self.send_response(200, 'OK')
            self.send_header('Content-type', 'html')
            self.end_headers()
            mixer.owidth[channel]=width
            mixer.oheight[channel]=height
            pads=list(mixer.player.get_by_name("mix").sink_pads())
            for j in range(0, len(pads)):
                if pads[j].props.ename == "channel%d" % ( channel+1 ):
                   pads[j].props.width=width
                   pads[j].props.height=height

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

            # try the seek
            res = mixer.player.send_event(event)
            if res:
               mixer.player.set_new_stream_time(0L)
	       mixer.player.set_state(gst.STATE_PLAYING)

        elif self.path == "/inputs/load":
          if "channel" not in params or "url" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          else:
            channel = int( params['channel'][0] )
            url = params['url'][0]
            if channel < 0 or channel >= nbchannels or ( mixer.uri[channel][:7] != "file://" and mixer.uri[channel][:8] != "still://" ):
              self.send_response(400, 'Bad request')
              self.send_header('Content-type', 'html')
              self.end_headers()
              return

            # try to load the file for still or video ( but video will fail )
            kfilesrc=mixer.player.get_by_name("kfilesrc%d"%(channel+1))
            if mixer.uri[channel][:7] == "file://" and url[:7] == "file://":
               mixer.uri[channel]=url
               self.send_response(200, 'OK')
               self.send_header('Content-type', 'html')
               self.end_headers()

	       mixer.player.set_state(gst.STATE_PAUSED)
               event = gst.event_new_seek(1.0, gst.FORMAT_TIME,
                    gst.SEEK_FLAG_FLUSH | gst.SEEK_FLAG_ACCURATE,
                    gst.SEEK_TYPE_SET, 0,
                    gst.SEEK_TYPE_SET, 0)

               res = mixer.player.send_event(event)
               if res:
                   mixer.player.set_new_stream_time(0L)
               kfilesrc.set_property( "location", url[7:] )
	       mixer.player.set_state(gst.STATE_PLAYING)

               return

            if mixer.uri[channel][:8] == "still://" and url[:8] == "still://":
               mixer.uri[channel]=url
               self.send_response(200, 'OK')
               self.send_header('Content-type', 'html')
               self.end_headers()

	       mixer.player.set_state(gst.STATE_PAUSED)
               event = gst.event_new_seek(1.0, gst.FORMAT_TIME,
                    gst.SEEK_FLAG_FLUSH | gst.SEEK_FLAG_ACCURATE,
                    gst.SEEK_TYPE_SET, 0,
                    gst.SEEK_TYPE_SET, 0)

               res = mixer.player.send_event(event)
               if res:
                   mixer.player.set_new_stream_time(0L)
               kfilesrc.set_property( "location", url[8:] )
	       mixer.player.set_state(gst.STATE_PLAYING)

               return

            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()

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
	    #mixer.player.set_state(gst.STATE_PAUSED)

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
          mixer.launch_pipe()
	  #mixer.player.set_state(gst.STATE_PAUSED)

        elif self.path == "/outputs/stream":
          if "hostname" not in params or "audioport" not in params or "videoport" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          self.send_response(200, 'OK')
          self.send_header('Content-type', 'html')
          self.end_headers()
          mixer.hostname=params['hostname'][0]
          mixer.audioport=int(params['audioport'][0])
          mixer.videoport=int(params['videoport'][0])
          mixer.launch_pipe()
	  #mixer.player.set_state(gst.STATE_PAUSED)

        elif self.path == "/outputs/icestream":
          if "hostname" not in params or "port" not in params or "mountpoint" not in params or "password" not in params:
            self.send_response(400, 'Bad request')
            self.send_header('Content-type', 'html')
            self.end_headers()
            return
          self.send_response(200, 'OK')
          self.send_header('Content-type', 'html')
          self.end_headers()
          mixer.icehost=params['hostname'][0]
          mixer.iceport=int(params['port'][0])
          mixer.icemount=params['mountpoint'][0]
          mixer.icepass=params['password'][0]
          mixer.launch_pipe()
	  #mixer.player.set_state(gst.STATE_PAUSED)

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
                self.audioport=""
                self.videoport=""
                self.icehost=""
                self.iceport=""
                self.icemount=""
                self.icepass=""
                self.uri=[]
                self.xpos=[]
                self.ypos=[]
                self.width=[]
                self.height=[]
                self.owidth=[]
                self.oheight=[]
                self.zorder=[]
                self.pattern=[]
                self.imagedir=[]
                self.minscore=[]
                self.noaudio=[]
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
                   self.owidth.append( 320 );
                   self.oheight.append( 240 );
                   self.zorder.append( i+1 );
                   self.imagedir.append( "" );
                   self.minscore.append( 0 );
                   self.noaudio.append( False );

                self.txsize=gwidth
                self.tysize=gheight

                if novideo==False: 
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
                   print "end of stream received"
                   sys.stdout.flush()
                  
                elif t == gst.MESSAGE_ELEMENT:
                   st = message.structure
                   if st.get_name() == "imgspot":
                      self.curtime = time.time()
                      gm = time.gmtime(self.curtime-self.starttime)
                      stime = "%.2d:%.2d:%.2d" % ( gm.tm_hour, gm.tm_min, gm.tm_sec )
                      print "imgspot : %s : image spotted : %s at %s (score=%d)" % (st["algorithm"], st["name"], stime, st["score"])
                      sys.stdout.flush()

                   elif st.get_name() == "kfilesrc":
                      print "soon end of video : %s" % ( st["filename"] )
                      sys.stdout.flush()
                      # clear up that video and relaunch the pipe
                      # this shouldn't affect the RTP streaming
                      for i in range(0, nbchannels):
                         if self.uri[i] == "file://"+st["filename"]:
                            self.uri[i] = ""
                            self.launch_pipe()

		elif t == gst.MESSAGE_ERROR:
			err, debug = message.parse_error()
			print "Gst4chMixer : error: %s" % err
                        sys.stdout.flush()

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

	def launch_pipe(self):

                pipecmd = ""

                if ( self.hostname != "" and self.icehost == "" ):
                    pipecmd += "gstrtpbin name=rtpbin "

                if self.icehost != "" and nosound==False:
                    pipecmd += "autoaudiosrc ! adder name=audiomix ! tee name=audmixout ! queue ! autoaudiosink audmixout. ! queue ! vorbisenc ! queue ! mux. "
                elif self.hostname != "" and nosound==False:
                    pipecmd += "autoaudiosrc ! adder name=audiomix ! tee name=audmixout ! queue ! autoaudiosink audmixout. ! queue ! ffenc_aac ! rtpmp4apay ! rtpbin.send_rtp_sink_1 "
                elif self.recfile != "" and nosound==False:
                    pipecmd += "autoaudiosrc ! adder name=audiomix ! tee name=audmixout ! queue ! autoaudiosink audmixout. ! queue ! ffenc_aac ! queue ! mux. "
                elif nosound==False:
                    pipecmd += "autoaudiosrc ! adder name=audiomix ! queue ! autoaudiosink "

                pipecmd += "videoscaledmixer name=mix sink_0::xpos=0 sink_0::ypos=0 sink_0::zorder=0 sink_0::width=%d sink_0::height=%d sink_0::ename=channel0 " % ( self.txsize, self.tysize )

                for i in range(0, nbchannels):
                    if self.uri[i] != "":
                       pipecmd += "sink_%d::xpos=%d sink_%d::ypos=%d sink_%d::alpha=1 sink_%d::zorder=%d sink_%d::width=%d sink_%d::height=%d sink_%d::ename=channel%d " % ( i+1, self.xpos[i], i+1, self.ypos[i], i+1, i+1, self.zorder[i], i+1, self.owidth[i], i+1, self.oheight[i], i+1, i+1 )
                    else:
                       pipecmd += "sink_%d::xpos=%d sink_%d::ypos=%d sink_%d::alpha=0 sink_%d::zorder=%d sink_%d::width=%d sink_%d::height=%d sink_%d::ename=channel%d " % ( i+1, self.xpos[i], i+1, self.ypos[i], i+1, i+1, self.zorder[i], i+1, self.owidth[i], i+1, self.oheight[i], i+1, i+1 )

                if ( self.icehost != "" ):
                    if novideo==False:
                       pipecmd += "! tee name=vidmixout ! queue leaky=1 ! xvimagesink sync=false vidmixout. ! queue leaky=1 ! theoraenc quality=5 ! queue ! mux. "
                    else:
                       pipecmd += "! tee name=vidmixout ! queue leaky=1 ! fakesink sync=false vidmixout. ! queue leaky=1 ! theoraenc quality=5 ! queue ! mux. "
                elif ( self.hostname != "" ):
                    if novideo==False:
                       pipecmd += "! tee name=vidmixout ! queue leaky=1 ! xvimagesink sync=false vidmixout. ! queue leaky=1 ! x264enc ! queue ! rtph264pay ! rtpbin.send_rtp_sink_0 "
                    else:
                       pipecmd += "! tee name=vidmixout ! queue leaky=1 ! fakesink sync=false vidmixout. ! queue leaky=1 ! x264enc ! queue ! rtph264pay ! rtpbin.send_rtp_sink_0 "
                elif ( self.recfile != "" ):
                    if novideo==False:
                       pipecmd += "! tee name=vidmixout ! queue ! xvimagesink sync=false vidmixout. ! queue ! ffenc_mpeg4 ! queue ! mux. "
                    else:
                       pipecmd += "! tee name=vidmixout ! queue ! fakesink sync=false vidmixout. ! queue ! ffenc_mpeg4 ! queue ! mux. "
                else:
                    if novideo==False:
                       pipecmd += "! autovideosink "
                    else:
                       pipecmd += "! fakesink "

                pipecmd += "videotestsrc pattern=black name=background ! video/x-raw-yuv,width=%d,height=%d,format=(fourcc)I420,framerate=(fraction)15/1 ! mix.sink_0 " % ( self.txsize, self.tysize );

                for i in range(0, nbchannels):
                    if ( self.uri[i] != "" ):

                       if ( self.imagedir[i] != "" ):
                         detectstring = "imgspot width=%d height=%d algorithm=surf imgdir=%s minscore=%d output=bus ! ffmpegcolorspace ! " % ( self.width[i], self.height[i], self.imagedir[i], self.minscore[i] )
                       else:
                         detectstring = ""

                       if self.uri[i][:7] == "file://":
                         pipecmd += " kfilesrc name=kfilesrc%d location=\"%s\" ! decodebin2 name=decodebin%d decodebin%d. ! ffmpegcolorspace !  %svideoscale name=videoscale%d ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_%d " % ( i+1, self.uri[i][7:], i+1, i+1, detectstring, i+1, self.width[i], self.height[i], i+1 );
                         if nosound==False and self.noaudio[i]==False: 
                            pipecmd += " decodebin%d. ! audioresample ! audiomix. "  % ( i+1 )

                       if self.uri[i][:8] == "still://":
                         pipecmd += " kfilesrc name=kfilesrc%d location=\"%s\" ! decodebin2 name=decodebin%d decodebin%d. ! ffmpegcolorspace !  %svideoscale name=videoscale%d ! video/x-raw-yuv,width=%d,height=%d ! imagefreeze ! mix.sink_%d " % ( i+1, self.uri[i][8:], i+1, i+1, detectstring, i+1, self.width[i], self.height[i], i+1 );

                       if self.uri[i][:9] == "device://":
                         pipecmd += " v4l2src name=v4l2src%d device=%s ! ffmpegcolorspace ! %s video/x-raw-yuv,width=%d,height=%d ! videoscale name=videoscale%d ! mix.sink_%d " % ( i+1, self.uri[i][9:], detectstring, self.width[i], self.height[i], i+1, i+1 );

                       if self.uri[i][:7] == "http://":
                         pipecmd += " uridecodebin name=decodebin%d uri=\"%s\" decodebin%d. ! ffmpegcolorspace ! %svideoscale name=videoscale%d ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_%d " % ( i+1, self.uri[i], i+1, detectstring, i+1, self.width[i], self.height[i], i+1 );
                         if nosound==False and self.noaudio[i]==False: 
                            pipecmd += " decodebin%d. ! audioresample ! audiomix. " % ( i+1 ) 

                       if self.uri[i][:7] == "rtsp://" or self.uri[i][:7] == "rtmp://":
                         pipecmd += " uridecodebin uri=\"%s\" name=decodebin%d decodebin%d. ! ffmpegcolorspace ! %svideoscale name=videoscale%d ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_%d " % ( self.uri[i], i+1, i+1, detectstring, i+1, self.width[i], self.height[i], i+1 );
                         if nosound==False and self.noaudio[i]==False: 
                            pipecmd += " decodebin%d. ! audioresample ! audiomix. "  % ( i+1 )

                if ( self.icehost != "" ):
                    pipecmd += " oggmux name=mux ! queue ! shout2send ip=%s port=%d mount=/%s password=%s " % ( self.icehost, self.iceport, self.icemount, self.icepass );
                elif ( self.hostname != "" ):
                    pipecmd += " rtpbin.send_rtp_src_0 ! udpsink host=%s port=%d ts-offset=0 name=vrtpsink rtpbin.send_rtcp_src_0 ! udpsink host=%s port=%d sync=false async=false name=vrtcpsink udpsrc port=%d name=vrtpsrc ! rtpbin.recv_rtcp_sink_0 " % ( self.hostname, self.videoport, self.hostname, self.videoport+1, self.videoport+5 )
                    if nosound==False:
                       pipecmd += " rtpbin.send_rtp_src_1 ! udpsink host=%s port=%d ts-offset=0 name=artpsink rtpbin.send_rtcp_src_1 ! udpsink host=%s port=%d sync=false async=false name=artcpsink udpsrc port=%d name=artpsrc ! rtpbin.recv_rtcp_sink_1 " % ( self.hostname, self.audioport, self.hostname, self.audioport+1, self.audioport+5 )

                elif ( self.recfile != "" ):
                    #the real name uses a date in it
                    #not to override previous recordings
                    extension = self.recfile[-3:]
                    basename = self.recfile[:-4]
                    now = datetime.datetime.now()
                    pipecmd += " avimux name=mux ! filesink location=%s-%.2d-%.2d-%.2d:%.2d:%.2d.%s " % ( basename, now.month, now.day, now.hour, now.minute, now.second, extension );

                print pipecmd
                sys.stdout.flush()

		if self.player != None : 
                   self.player.set_state(gst.STATE_NULL)
		self.player = gst.parse_launch ( pipecmd )
                self.starttime = time.time()

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
    global nosound
    global novideo

    def usage():
        sys.stderr.write("usage: %s <nbchannels> <width> <height> [nosound] [novideo]\n" % args[0])
        sys.exit(1)

    if len(args) < 4:
        usage()

    nbchannels = int(args[1])
    gwidth = int(args[2])
    gheight = int(args[3])
    nosound=False
    novideo=False
    if len(args)>=5 and args[4]=='nosound':
      nosound=True 
    if len(args)>=6 and args[5]=='nosound':
      nosound=True 
    if len(args)>=5 and args[4]=='novideo':
      novideo=True 
    if len(args)>=6 and args[5]=='novideo':
      novideo=True 

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

