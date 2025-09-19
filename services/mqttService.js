// services/mqttService.js
const mqtt = require('mqtt');
const Order = require('../models/Order');

class MQTTService {
  constructor() {
    this.client = null;
    this.isConnected = false;
    
    // MQTT Topics
    this.topics = {
      NEW_ORDER: 'KY/RESTO/ORDER/NEW',
      ORDER_DONE: 'KY/RESTO/ORDER/DONE'
    };
  }

  // Initialize MQTT connection
  async connect() {
    try {
      const brokerUrl = process.env.MQTT_BROKER_URL || 'mqtt://localhost:1883';
      const options = {
        clientId: `restaurant-server-${Math.random().toString(16).substr(2, 8)}`,
        username: process.env.MQTT_USERNAME || '',
        password: process.env.MQTT_PASSWORD || '',
        keepalive: 60,
        reconnectPeriod: 1000,
        protocolId: 'MQTT',
        protocolVersion: 4,
        clean: true
      };

      console.log(`🔌 Connecting to MQTT broker: ${brokerUrl}`);
      this.client = mqtt.connect(brokerUrl, options);

      this.client.on('connect', () => {
        console.log('✅ MQTT Connected successfully');
        this.isConnected = true;
        this.subscribeToTopics();
      });

      this.client.on('error', (error) => {
        console.error('❌ MQTT Connection error:', error);
        this.isConnected = false;
      });

      this.client.on('close', () => {
        console.log('🔌 MQTT Connection closed');
        this.isConnected = false;
      });

      this.client.on('reconnect', () => {
        console.log('🔄 MQTT Reconnecting...');
      });

      this.client.on('message', (topic, message) => {
        this.handleIncomingMessage(topic, message);
      });

    } catch (error) {
      console.error('❌ Failed to connect to MQTT broker:', error);
    }
  }

  // Subscribe to necessary topics
  subscribeToTopics() {
    if (!this.client || !this.isConnected) {
      console.error('❌ MQTT client not connected');
      return;
    }

    // Subscribe to order completion topic
    this.client.subscribe(this.topics.ORDER_DONE, (error) => {
      if (error) {
        console.error('❌ Failed to subscribe to ORDER_DONE topic:', error);
      } else {
        console.log(`✅ Subscribed to topic: ${this.topics.ORDER_DONE}`);
      }
    });
  }

  // Handle incoming MQTT messages
  async handleIncomingMessage(topic, message) {
    try {
      console.log(`📨 Received MQTT message on topic: ${topic}`);
      
      if (topic === this.topics.ORDER_DONE) {
        await this.handleOrderDone(message);
      }
    } catch (error) {
      console.error('❌ Error handling MQTT message:', error);
    }
  }

  // Handle order completion from IoT device
  async handleOrderDone(message) {
    try {
      const messageStr = message.toString();
      console.log(`🍳 Order completion message: ${messageStr}`);
      
      // Parse the order ID from the message
      let orderId;
      try {
        const parsed = JSON.parse(messageStr);
        orderId = parsed.orderId || parsed.id || parsed._id;
      } catch (parseError) {
        // If not JSON, treat the entire message as order ID
        orderId = messageStr.trim();
      }

      if (!orderId) {
        console.error('❌ No order ID found in completion message');
        return;
      }

      // Update order status to 'served'
      const updatedOrder = await Order.findByIdAndUpdate(
        orderId,
        { status: 'served' },
        { new: true }
      ).populate('items.menuItem');

      if (updatedOrder) {
        console.log(`✅ Order ${orderId} marked as served`);
        console.log(`📋 Table ${updatedOrder.tableNumber} - Order completed`);
      } else {
        console.error(`❌ Order ${orderId} not found`);
      }

    } catch (error) {
      console.error('❌ Error processing order completion:', error);
    }
  }

  // Publish new order to IoT devices
  async publishNewOrder(order) {
    if (!this.client || !this.isConnected) {
      console.error('❌ MQTT client not connected - cannot publish order');
      return false;
    }

    try {
      // Prepare order data for IoT devices
      const orderData = {
        orderId: order._id.toString(),
        tableNumber: order.tableNumber,
        items: order.items.map(item => ({
          name: item.menuItem.name,
          quantity: item.quantity,
          notes: item.notes || ''
        })),
        timestamp: new Date().toISOString(),
        total: order.total
      };

      const message = JSON.stringify(orderData, null, 2);
      
      // Publish to the new order topic
      this.client.publish(this.topics.NEW_ORDER, message, { qos: 1 }, (error) => {
        if (error) {
          console.error('❌ Failed to publish new order:', error);
        } else {
          console.log(`📤 Published new order to ${this.topics.NEW_ORDER}`);
          console.log(`🍽️  Order ID: ${orderData.orderId} | Table: ${orderData.tableNumber}`);
        }
      });

      return true;
    } catch (error) {
      console.error('❌ Error publishing new order:', error);
      return false;
    }
  }

  // Disconnect from MQTT broker
  disconnect() {
    if (this.client) {
      console.log('🔌 Disconnecting MQTT client...');
      this.client.end();
      this.isConnected = false;
    }
  }

  // Check connection status
  isClientConnected() {
    return this.isConnected && this.client && this.client.connected;
  }

  // Publish a test message (for debugging)
  publishTestMessage(topic, message) {
    if (!this.client || !this.isConnected) {
      console.error('❌ MQTT client not connected');
      return;
    }

    this.client.publish(topic, message, { qos: 1 }, (error) => {
      if (error) {
        console.error(`❌ Failed to publish test message to ${topic}:`, error);
      } else {
        console.log(`📤 Test message published to ${topic}: ${message}`);
      }
    });
  }
}

// Create singleton instance
const mqttService = new MQTTService();

module.exports = mqttService;