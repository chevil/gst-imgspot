var querystring = require('querystring');
var http = require('http');
var fs = require('fs');

if ( process.argv.length  < 6 )
{
   console.log( "usage : "+process.argv[1]+" hostname port mountpoint password" );
   process.exit(-1);
}
hostname=process.argv[2];
port=process.argv[3];
mountpoint=process.argv[4];
password=process.argv[5];

var post_data = querystring.stringify({
    'hostname': hostname,
    'port': port,
    'mountpoint': mountpoint,
    'password': password
});

// An object of options to indicate where to post to
var post_options = {
      host: 'localhost',
      port: '9999',
      path: '/outputs/icestream',
      method: 'POST',
      headers: {
          'Content-Type': 'text/plain',
          'Content-Length': post_data.length
      }
};

// Set up the request
var post_req = http.request(post_options, function(res) {
    res.setEncoding('utf8');
    res.on('data', function (chunk) {
        console.log('Response: ' + chunk);
    });
});

// post the data
post_req.write(post_data);
post_req.end();
