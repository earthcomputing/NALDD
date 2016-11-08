const s_port = 3000;
const c_port = 1337;

var net = require('net');

var json_data = {};
var connected = 0 ;
var c_socket ;
var express = require('express');
var path = require('path');

var app = express();
//var server = require('http').Server(app);
var bodyParser = require('body-parser');
var http = require('http').Server(app);
var io = require('socket.io')(http);

app.get('/', function(req, res){
  res.sendFile(__dirname + '/demo_cell.html');
});

io.on('connection', function(socket){
    console.log('io connected')
    connected = 1 ;
  socket.on('aitMessage', function(msg){
    var message = msg.message ;
    var port = msg.port ;
    console.log('AIT ' + port + ' ' + message);
    c_socket.write( '{ \"port\": \"' + port + '\", \"message\":\"' + message + '\" }')
  });
});

app.use('/', express.static(path.join(__dirname, '/')));
app.use(bodyParser.json());

http.listen(s_port, function(){
  console.log('listening on *:'+s_port);
});

//var l = io.listen(server);

var data = {"deviceName": "enp6s0",
        "linkState": "UP",
        "entlState":"HELLO",
        "entlCount":"990",
        "AITMessage":"-none-"
       };

//l.sockets.on("connection", function(socket) {
//    console.log('Server connected');
 //   s_socket = socket ;
 //   connected = 1 ;
 //       //    enp2s0Update(socket, data);
//    earthUpdate(socket, data);

//    socket.on("aitMessage", function(data){
//    console.log("in AIT handler");
//    earthAITMessage(socket, data);
//});
//});


//update function for earth-connect update
function earthUpdate (socket, data) {
    console.log(data);
    io.emit("earth-update", data);
} 

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
                  //console.log('data:'+data);
        var jd = data.split("\n") ;
        for( var i = 0, len = jd.length; i < len; i++ ) {
            var d = jd[i] ;

            if(!isBlank(d)) {
                //console.log('split:'+d);

                try {
                    var obj = JSON.parse(d) ;
                    if( obj ) {
                        console.log( "entlCount:", obj.entlCount) ;
                        if( obj.machineName ) {
                            json_data[obj.machineName] = d ;
                            //console.log( 'machine['+obj.machineName+'] = '+json_data[obj.machineName]) ;
                            if( connected ) {
                                io.emit('earth-update', d);
                                //s_socket.write(d+'\n') ;
                                console.log('earth-update '+d);
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
