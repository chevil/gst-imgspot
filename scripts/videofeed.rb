#!/usr/bin/ruby

require 'gst'
require 'readline'

raise "Usage: #{$0} video imgdir [algorithm]" unless(ARGV.length>=2)

video=ARGV[0]
imgdir=ARGV[1]

if ( ARGV.length == 3 )
  algorithm=ARGV[2]
else
  algorithm = 'histogram'
end

Gst::init()
loop=GLib::MainLoop::new(nil,false)
$pip=nil

def start_pipe(video,imgdir,algorithm)

  if ( $pip != nil )
    $pip.stop
  end

  cmd=<<-eof
    filesrc location=#{video}  ! decodebin ! ffmpegcolorspace ! imgspot imgdir=#{imgdir} algorithm=#{algorithm} ! ffmpegcolorspace ! sdlvideosink
  eof

  $pip=Gst::Parse::launch(cmd)

  bus=$pip.bus
  bus.add_watch() do |bus,message|
    case message.type
    when Gst::Message::EOS
      STDERR.puts("End of file")
      loop.quit
    when Gst::Message::ERROR
      p ['ERROR!!',pip,bus,message.parse]
      loop.quit
    end
    true
  end

  $pip.play

end

start_pipe(video,imgdir,algorithm)


while imgdir = Readline.readline('Enter new image directory> ', true)
  start_pipe(video,imgdir,algorithm)
end
