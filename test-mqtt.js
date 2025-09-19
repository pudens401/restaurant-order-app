// test-mqtt.js - Simple MQTT test script
require('dotenv').config();
const mqtt = require('mqtt');

console.log('🧪 Testing MQTT Connection...');

const client = mqtt.connect(process.env.MQTT_BROKER_URL || 'mqtt://localhost:1883', {
  clientId: 'test-client-' + Math.random().toString(16).substr(2, 8)
});

client.on('connect', () => {
  console.log('✅ Connected to MQTT broker');
  
  // Subscribe to order completion topic
  client.subscribe('KY/RESTO/ORDER/DONE', (err) => {
    if (err) {
      console.error('❌ Subscribe error:', err);
    } else {
      console.log('✅ Subscribed to KY/RESTO/ORDER/DONE');
    }
  });

  // Test publishing a new order
  const testOrder = {
    orderId: 'test-order-123',
    tableNumber: 1,
    items: [
      { name: 'Test Burger', quantity: 1 }
    ],
    timestamp: new Date().toISOString()
  };

  setTimeout(() => {
    console.log('📤 Publishing test order...');
    client.publish('KY/RESTO/ORDER/NEW', JSON.stringify(testOrder, null, 2));
  }, 1000);

  // Test order completion after 3 seconds
  setTimeout(() => {
    console.log('📥 Publishing test completion...');
    client.publish('KY/RESTO/ORDER/DONE', 'test-order-123');
  }, 3000);

  // Disconnect after 5 seconds
  setTimeout(() => {
    console.log('🔌 Disconnecting...');
    client.end();
  }, 5000);
});

client.on('message', (topic, message) => {
  console.log(`📨 Received message on ${topic}: ${message.toString()}`);
});

client.on('error', (error) => {
  console.error('❌ MQTT Error:', error);
});

client.on('close', () => {
  console.log('🔌 Connection closed');
  process.exit(0);
});