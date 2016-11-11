const s_port = 8081;
const c_port = 1337;

var net = require('net');

var json_data = {};
var connected = 0 ;
var c_socket ;

var app = require('express')();
var http = require('http').Server(app);
var io = require('socket.io')(http);


app.get('/', function(req, res){
  res.sendFile(__dirname + '/index_p.html');
});

io.on('connection', function(socket){
    connected = 1 ;
  socket.on('ait message', function(msg){
    console.log('AIT'+msg);
  });
});

http.listen(s_port, function(){
  console.log('listening on *:'+s_port);
});


//var server = net.createServer(function(socket) {
//    console.log('Target connected');
//    s_socket = socket ;
//    connected = 1 ;
//});

function isBlank(str) {
    return (!str || /^\s*$/.test(str));
}

var client = net.createServer(function(socket) {
    c_socket = socket ;
    console.log('Client connected');

    //socket.write('Echo server\r\n');
    //socket.pipe(socket);

    socket.on('data', function (data) {
        data = data.toString();
                  console.log('data:'+data);
        var jd = data.split("\n") ;
        for( var i = 0, len = jd.length; i < len; i++ ) {
            var d = jd[i] ;

            if(!isBlank(d)) {
                console.log('split:'+d);

                try {
                    var obj = JSON.parse(d) ;
                    if( obj ) {
                        console.log( "entlCount:", obj.entlCount) ;
                        if( obj.machineName ) {
                            json_data[obj.machineName] = d ;
                            console.log( 'machine['+obj.machineName+'] = '+json_data[obj.machineName]) ;
                            if( connected ) {
                                io.emit('state message', d);
                                //s_socket.write(d+'\n') ;
                                //console.log('Sent data to targert');
                            }
                        }                
                    }
                } catch(e) {
                    console.log('error:'+e) ;
                }

            }


        }

   });

});




//server.listen(port, '127.0.0.1');
//server.listen(s_port);
client.listen(c_port, '127.0.0.1');
