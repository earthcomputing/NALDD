const s_port = 8081;
const c_port = 1337;

var net = require('net');

var json_data = {};
var connected = 0 ;

var s_socket ;

var server = net.createServer(function(socket) {
    console.log('Target connected');
    s_socket = socket ;
    connected = 1 ;
});

var client = net.createServer(function(socket) {
    console.log('Client connected');

    //socket.write('Echo server\r\n');
    //socket.pipe(socket);

    socket.on('data', function (data) {
        data = data.toString();
        console.log('client sent the folowing string:'+data);
        var obj = JSON.parse(data) ;
        console.log( "entlCount:", obj.entlCount) ;
        if( obj.machineName ) {
            json_data[obj.machineName] = data ;
            console.log( 'machine['+obj.machineName+'] = '+json_data[obj.machineName]) ;
            if( connected ) {
                s_socket.write(data) ;
                console.log('Sent data to targert');
            }
        }
   });

});




//server.listen(port, '127.0.0.1');
server.listen(s_port);
client.listen(c_port);
