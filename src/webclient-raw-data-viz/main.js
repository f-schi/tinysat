var mqtt = require('mqtt');
var express = require('express');
var http = require('http');
var socketIo = require('socket.io');

var options = {
    host: '******',
    port: 8883,
    protocol: 'mqtts',
    username: '******',
    password: '******'
}


// Initialize the MQTT client
var client = mqtt.connect(options);

// Setup the Express server
var app = express();
var server = http.createServer(app);
var io = socketIo(server);

// Serve static files (for index.html)
app.use(express.static(__dirname + '/public'));

// Setup MQTT callbacks
client.on('connect', function () {
    console.log('Connected to MQTT broker');
    client.subscribe('ground/observation');
});

client.on('error', function (error) {
    console.log(error);
});

client.on('message', function (topic, message) {
    console.log('Received message:', topic, message.toString());
    
    // Send data to the frontend via WebSockets
    io.emit('mqtt_message', { topic: topic, message: message.toString() });
});

// Start the server
server.listen(8000, function () {
    console.log('Server is running on http://localhost:8000');
});
