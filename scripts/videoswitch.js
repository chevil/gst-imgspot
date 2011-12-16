var net = require('net');
var child = require('child_process');

var cmd = 'python videoswitch.py /dev/video0 /dev/video1 ../data/images surf 10';
var options = null;

var vswitch = child.exec(cmd, options, null);

while(1) {}
